#pragma once

#include "Importers/Types/Blueprint/AnimationBlueprintImporter.h"
#include "Importers/Types/Blueprint/BlueprintImporter.h"
#include "AnimationBlueprintUtilities.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "EdGraphUtilities.h"
#include "Animation/AnimNode_TransitionResult.h"
#include "Serializers/ObjectSerializer.h"

inline void AutoLayoutStateMachineGraph(UAnimationStateMachineGraph* StateMachineGraph)  {
    if (!StateMachineGraph) { 
        return; 
    }

    UAnimStateNodeBase* InitialState = nullptr; {
        if (StateMachineGraph->EntryNode) {
            const UEdGraphPin* EntryPin = StateMachineGraph->EntryNode->FindPin(TEXT("Entry"));

            if (EntryPin && EntryPin->LinkedTo.Num() > 0) { 
                InitialState = Cast<UAnimStateNodeBase>(EntryPin->LinkedTo[0]->GetOwningNode()); 
            }
        }
    }

    if (!InitialState) {
        return;
    }

    TMap<UAnimStateNodeBase*, int32> StateLevels;
    TQueue<UAnimStateNodeBase*> NodesToProcess;

    StateLevels.Add(InitialState, 0);
    NodesToProcess.Enqueue(InitialState);

    TMap<UAnimStateNodeBase*, TArray<UAnimStateNodeBase*>> OutgoingTransitions;

    for (UEdGraphNode* Node : StateMachineGraph->Nodes) {
        const UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
        if (!TransitionNode) { 
            continue; 
        }

        const UEdGraphPin* InPin = TransitionNode->GetInputPin();
        const UEdGraphPin* OutPin = TransitionNode->GetOutputPin();

        if (InPin && OutPin && InPin->LinkedTo.Num() > 0 && OutPin->LinkedTo.Num() > 0) {
            UAnimStateNodeBase* FromState = Cast<UAnimStateNodeBase>(InPin->LinkedTo[0]->GetOwningNode());
            UAnimStateNodeBase* ToState = Cast<UAnimStateNodeBase>(OutPin->LinkedTo[0]->GetOwningNode());

            if (FromState && ToState) { 
                OutgoingTransitions.FindOrAdd(FromState).AddUnique(ToState); 
            }
        }
    }

    while (!NodesToProcess.IsEmpty()) {
        UAnimStateNodeBase* Current;
        NodesToProcess.Dequeue(Current);

        const int32 CurrentLevel = StateLevels.FindChecked(Current);

        if (OutgoingTransitions.Contains(Current)) {
            for (UAnimStateNodeBase* NextState : OutgoingTransitions[Current]) {
                const int32 NextLevel = CurrentLevel + 1;

                if (!StateLevels.Contains(NextState) || NextLevel < StateLevels[NextState]) {
                    StateLevels.Add(NextState, NextLevel);
                    NodesToProcess.Enqueue(NextState);
                }
            }
        }
    }

    TMap<int32, TArray<UAnimStateNodeBase*>> StatesByLevel;
    for (auto& Pair : StateLevels) {
        UAnimStateNodeBase* State = Pair.Key;
        int32 Level = Pair.Value;

        StatesByLevel.FindOrAdd(Level).Add(State);
    }

    constexpr float HorizontalSpacing = 400.0f;
    constexpr float VerticalSpacing = 200.0f;

    for (auto& LevelGroup : StatesByLevel) {
        int32 Level = LevelGroup.Key;
        TArray<UAnimStateNodeBase*>& NodesInLevel = LevelGroup.Value;

        NodesInLevel.Sort([](const UAnimStateNodeBase& A, const UAnimStateNodeBase& B) {
            return A.GetName() < B.GetName();
        });

        const float X = Level * HorizontalSpacing;
        float StartY = -((NodesInLevel.Num() - 1) * VerticalSpacing) * 0.5f;

        for (int32 i = 0; i < NodesInLevel.Num(); ++i) {
            UAnimStateNodeBase* Node = NodesInLevel[i];
            Node->NodePosX = X;
            Node->NodePosY = StartY + i * VerticalSpacing;
        }
    }

    for (UEdGraphNode* Node : StateMachineGraph->Nodes) {
        UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
        if (!TransitionNode) { 
            continue; 
        }

        const UEdGraphPin* InputPin = TransitionNode->GetInputPin();
        const UEdGraphPin* OutputPin = TransitionNode->GetOutputPin();

        if (InputPin && OutputPin && InputPin->LinkedTo.Num() > 0 && OutputPin->LinkedTo.Num() > 0) {
            UAnimStateNodeBase* FromState = Cast<UAnimStateNodeBase>(InputPin->LinkedTo[0]->GetOwningNode());
            UAnimStateNodeBase* ToState = Cast<UAnimStateNodeBase>(OutputPin->LinkedTo[0]->GetOwningNode());

            if (FromState && ToState) {
                TransitionNode->NodePosX = (FromState->NodePosX + ToState->NodePosX) * 0.5f;
                TransitionNode->NodePosY = (FromState->NodePosY + ToState->NodePosY) * 0.5f;
            }
        }
    }

    if (StateMachineGraph->EntryNode) {
        StateMachineGraph->EntryNode->NodePosX = InitialState->NodePosX;
        StateMachineGraph->EntryNode->NodePosY = InitialState->NodePosY - 120;
    }

    StateMachineGraph->NotifyGraphChanged();
}

inline void CreateStateMachineGraph(
	UAnimationStateMachineGraph* StateMachineGraph,
	const TSharedPtr<FJsonObject>& StateMachineJsonObject,
	UObjectSerializer* ObjectSerializer,
	FUObjectExportContainer* RootContainer,
	TArray<FString> ReversedNodesKeys,
	IAnimationBlueprintImporter* Importer,
	UAnimBlueprint* AnimBlueprint
) {
	if (!StateMachineGraph || !StateMachineJsonObject.IsValid()) {
		return;
	}

	const UAnimationStateMachineSchema* Schema = Cast<UAnimationStateMachineSchema>(StateMachineGraph->GetSchema());
	StateMachineGraph->Modify();

	const TArray<TSharedPtr<FJsonValue>>& States = StateMachineJsonObject->GetArrayField(TEXT("States"));

	FUObjectExportContainer Container;

	for (int32 i = 0; i < States.Num(); ++i) {
		const TSharedPtr<FJsonObject> StateObject = States[i]->AsObject();
		const FString StateName = StateObject->GetStringField(TEXT("StateName"));

		const bool bAlwaysResetOnEntry = StateObject->GetBoolField(TEXT("bAlwaysResetOnEntry"));

		const bool bIsAConduit = StateObject->GetBoolField(TEXT("bIsAConduit"));
		const int EntryRuleNodeIndex = StateObject->GetIntegerField(TEXT("EntryRuleNodeIndex"));

		UAnimStateNodeBase* Node;
		UEdGraph* BoundGraph = nullptr;

		if (bIsAConduit) {
			UAnimStateConduitNode* ConduitNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateConduitNode>(
				StateMachineGraph,
				NewObject<UAnimStateConduitNode>(StateMachineGraph, UAnimStateConduitNode::StaticClass(), *StateName, RF_Transactional)
			);

			BoundGraph = ConduitNode->BoundGraph;

			if (!BoundGraph) {
				BoundGraph = NewObject<UAnimationTransitionGraph>(ConduitNode, UAnimationTransitionGraph::StaticClass(), *StateName, RF_Transactional);
				ConduitNode->BoundGraph = BoundGraph;
			}

			Node = ConduitNode;

			if (EntryRuleNodeIndex != -1 && ReversedNodesKeys.IsValidIndex(EntryRuleNodeIndex)) {
				FString DelegateExportName = ReversedNodesKeys[EntryRuleNodeIndex];

				UAnimationTransitionGraph* TransGraph = CastChecked<UAnimationTransitionGraph>(BoundGraph);

				if (UAnimGraphNode_TransitionResult* ResultNode = TransGraph->GetResultNode()) {
					UEdGraphPin* Decides = ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input);

					if (const FUObjectExport* Rule = RootContainer->Find(DelegateExportName); Rule != nullptr && Rule->IsJsonValid()) {
						if (bool bEnters; Rule->JsonObject->TryGetBoolField(TEXT("bCanEnterTransition"), bEnters)) {
							ResultNode->Node.bCanEnterTransition = bEnters;

							if (Decides != nullptr) {
								Decides->DefaultValue = bEnters ? TEXT("true") : TEXT("false");
							}
						}
					}

					if (Decides != nullptr) {
						Importer->Answers(DelegateExportName, TEXT("bCanEnterTransition"), Decides);
					}

					Importer->ApplyBindings(DelegateExportName, ResultNode);
				}
			}
		} else {
			UAnimStateNode* StateNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>(
				StateMachineGraph,
				NewObject<UAnimStateNode>(StateMachineGraph, UAnimStateNode::StaticClass(), *StateName, RF_Transactional)
			);

			BoundGraph = StateNode->BoundGraph;

			if (!BoundGraph) {
				BoundGraph = NewObject<UAnimationStateGraph>(StateNode, UAnimationStateGraph::StaticClass(), *StateName, RF_Transactional);
				StateNode->BoundGraph = BoundGraph;
			}

			StateNode->bAlwaysResetOnEntry = bAlwaysResetOnEntry;

			Node = StateNode;
		}

		FEdGraphUtilities::RenameGraphToNameOrCloseToName(BoundGraph, *StateName);

		Container.Exports.Add(
			new FUObjectExport(
				FName(*StateName),
				NAME_None,
				NAME_None,
				StateObject,
				Node,
				nullptr
			)
		);
	}

	const TArray<TSharedPtr<FJsonValue>>& Transitions = StateMachineJsonObject->GetArrayField(TEXT("Transitions"));

	for (int32 TransitionIndex = 0; TransitionIndex < Transitions.Num(); ++TransitionIndex) {
	    const TSharedPtr<FJsonObject>& TransitionObject = Transitions[TransitionIndex]->AsObject();

	    const int32 PreviousStateIndex = TransitionObject->GetIntegerField(TEXT("PreviousState"));
	    const int32 NextStateIndex = TransitionObject->GetIntegerField(TEXT("NextState"));

		FUObjectExport* PreviousStateExport = Container.Exports[PreviousStateIndex];
		FUObjectExport* NextStateExport = Container.Exports[NextStateIndex];
		if (!PreviousStateExport->Object || !NextStateExport->Object) continue;

		TSharedPtr<FJsonObject> PreviousStateObject = PreviousStateExport->JsonObject;

		UAnimStateNodeBase* const FromNode = Cast<UAnimStateNodeBase>(PreviousStateExport->Object);
		UAnimStateNodeBase* const ToNode = Cast<UAnimStateNodeBase>(NextStateExport->Object);

		if (!FromNode || !ToNode) continue;

        UAnimStateTransitionNode* const TransitionNode = NewObject<UAnimStateTransitionNode>(StateMachineGraph);
        TransitionNode->SetFlags(RF_Transactional);
        TransitionNode->CreateNewGuid();

        TransitionNode->PostPlacedNewNode();

        TransitionNode->AllocateDefaultPins();
        StateMachineGraph->Nodes.Add(TransitionNode);

        UAnimationTransitionGraph* AnimationTransitionGraph = Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph);
        if (!AnimationTransitionGraph || !AnimationTransitionGraph->MyResultNode) {
            continue;
        }

		UAnimGraphNode_TransitionResult* TransitionResult = AnimationTransitionGraph->MyResultNode;
        FAnimNode_TransitionResult& ResultStruct = TransitionResult->Node;

        ResultStruct.bCanEnterTransition = TransitionObject->GetBoolField(TEXT("bDesiredTransitionReturnValue"));

		TSharedPtr<FJsonObject> TransitionStateObject;

		if (PreviousStateObject->HasField(TEXT("Transitions"))) {
			const TArray<TSharedPtr<FJsonValue>> StateTransitions = PreviousStateObject->GetArrayField(TEXT("Transitions"));

			for (const TSharedPtr<FJsonValue>& StateTransValue : StateTransitions) {
				const TSharedPtr<FJsonObject> StateTransitionObject = StateTransValue->AsObject();

				if (StateTransitionObject.IsValid()) {
					const int32 StateTransitionIndex = StateTransitionObject->GetIntegerField(TEXT("TransitionIndex"));

					if (TransitionIndex == StateTransitionIndex) {
						TransitionStateObject = StateTransitionObject;
						break;
					}
				}
			}
		}

		TransitionNode->bAutomaticRuleBasedOnSequencePlayerInState = TransitionStateObject->GetBoolField(TEXT("bAutomaticRemainingTimeRule"));
		ObjectSerializer->DeserializeObjectProperties(TransitionObject, TransitionNode);

		const int CanTakeDelegateIndex = TransitionStateObject->GetIntegerField(TEXT("CanTakeDelegateIndex"));

		if (CanTakeDelegateIndex != -1 &&
			!TransitionNode->bAutomaticRuleBasedOnSequencePlayerInState
		) {
			FString DelegateExportName = ReversedNodesKeys[CanTakeDelegateIndex];

			FUObjectExport* DelegateExport = RootContainer->Find(DelegateExportName);

			HandlePropertyBinding(DelegateExport, Importer->GetContainer()->JsonObjects, TransitionResult, Importer, AnimBlueprint);

			if (UEdGraphPin* Decides = TransitionResult->FindPin(TEXT("bCanEnterTransition"), EGPD_Input)) {
				Importer->Answers(DelegateExportName, TEXT("bCanEnterTransition"), Decides);
			}

			Importer->ApplyBindings(DelegateExportName, TransitionResult);
		}

        if (UEdGraphPin* const FromOutput = FindOutputPin(FromNode)) {
            if (UEdGraphPin* const TransitionIn = TransitionNode->GetInputPin()) {
                FromOutput->MakeLinkTo(TransitionIn);
            }
        }
        if (UEdGraphPin* const TransitionOut = TransitionNode->GetOutputPin()) {
            if (UEdGraphPin* const ToInput = FindInputPin(ToNode)) {
                TransitionOut->MakeLinkTo(ToInput);
            }
	    }
	}

	if (!StateMachineGraph->EntryNode) return;

	const int InitialState = StateMachineJsonObject->GetIntegerField(TEXT("InitialState"));
	if (!States.IsValidIndex(InitialState)) return;

	const TSharedPtr<FJsonObject> InitialStateObject = States[InitialState]->AsObject();
	const FString InitialStateName = InitialStateObject->GetStringField(TEXT("StateName"));

	UAnimStateNodeBase* InitialStateNode = Cast<UAnimStateNodeBase>(Container.Find(InitialStateName)->Object);
	if (!InitialStateNode) return;

	UEdGraphPin* EntryOutputPin = StateMachineGraph->EntryNode->FindPin(TEXT("Entry"));

	if (InitialStateNode) {
		InitialStateNode->AllocateDefaultPins();

		UEdGraphPin* InitialInputPin = nullptr;
		for (UEdGraphPin* Pin : InitialStateNode->Pins) {
			if(Pin && Pin->Direction == EGPD_Input) {
				InitialInputPin = Pin;
				break;
			}
		}

		if (EntryOutputPin && InitialInputPin) {
			if (Schema) {
				Schema->TryCreateConnection(EntryOutputPin, InitialInputPin);
				StateMachineGraph->NotifyGraphChanged();
			}
		}
	}

	AutoLayoutStateMachineGraph(StateMachineGraph);
}
