#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/AnimationBlueprintImporter.h"
#include "Importers/Types/Blueprint/BlueprintCompile.h"
#include "AnimGraphNode_BlendListByInt.h"
#if !UE4_22_BELOW
#include "AnimGraphNode_CustomProperty.h"
#endif
#include "Kismet/KismetMathLibrary.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#if UE4_23_BELOW
#include "AnimGraphNode_SubInput.h"
#else
#include "AnimGraphNode_LinkedInputPose.h"
#endif
#include "AnimGraphNode_Root.h"
#include "AnimationGraphSchema.h"
#include "Animation/BlendProfile.h"
#include "AnimationGraph.h"

#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "Animation/AnimBlueprint.h"
#include "Importers/Types/Animation/PreviewMesh.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "Importers/Types/Blueprint/Utilities/AnimationBlueprintUtilities.h"
#include "Importers/Types/Blueprint/Utilities/AnimNodeLayoutUtillties.h"
#include "Importers/Types/Blueprint/Utilities/StateMachineUtilities.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Importers/Types/Blueprint/BlueprintUtilities.h"
#include "Importers/Types/Blueprint/BlueprintVariables.h"
#include "Utilities/JsonHelpers.h"

#if ENGINE_UE5
#include "UObject/UnrealTypePrivate.h"
#endif

int32 IAnimationBlueprintImporter::ConstructBlendOptions() {
	if (AnimBlueprint == nullptr) return 0;

	const FUObjectJsonValueExport Data = GetAssetDataAsValue();

	if (!Data.Has(TEXT("GraphBlendOptions"))) return 0;

	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);

	int32 Said = 0;

	for (const FUObjectJsonValueExport& Entry : Data.GetArray(TEXT("GraphBlendOptions"))) {
		if (!Entry.Has(TEXT("Key")) || !Entry.Has(TEXT("Value"))) continue;

		const FString Named = Entry.GetString(TEXT("Key"));

		for (UEdGraph* Graph : Graphs) {
			UAnimationGraph* Layer = Cast<UAnimationGraph>(Graph);

			if (Layer == nullptr || Layer->GetName() != Named) continue;

			const TSharedPtr<FJsonObject> Says = MakeShared<FJsonObject>();

			Says->SetObjectField(TEXT("BlendOptions"), Entry.GetObject(TEXT("Value")).JsonObject);

			GetObjectSerializer()->DeserializeObjectProperties(Says, Layer);

			Said++;

			break;
		}
	}

	return Said;
}

bool IAnimationBlueprintImporter::Import() {
	AnimBlueprint = GetSelectedAsset<UAnimBlueprint>(true);

	if (!AnimBlueprint && GetPackage()) {
		UBlueprint* ExistingBlueprint = FindObject<UBlueprint>(GetPackage(), *GetAssetName());
		if (!ExistingBlueprint) ExistingBlueprint = LoadObject<UBlueprint>(nullptr, *GetPackage()->GetPathName());

		if (ExistingBlueprint) {
			AnimBlueprint = Cast<UAnimBlueprint>(ExistingBlueprint);

			if (!AnimBlueprint) {
				FImportIssues::Report(
					EImportIssue::Failed,
					TEXT("The asset name is already taken"),
					FString::Printf(TEXT("'%s' already exists and is not an Animation Blueprint. Rename or delete it before reflecting."), *GetAssetName())
				);

				return false;
			}
		}
	}

	if (!AnimBlueprint) {
		TSharedPtr<FJsonObject> Comes = GetSuperStructJsonObject(GetAssetData());

		if (!Comes.IsValid()) {
			Comes = GetSuperStructJsonObject(GetAssetExport());
		}

		UClass* ParentClass = Comes.IsValid() ? LoadClass(Comes) : nullptr;

		if (ParentClass == nullptr) {
			FImportIssues::Report(
				EImportIssue::MissingClass,
				TEXT("Couldn't resolve the parent class"),
				FString::Printf(TEXT("'%s' is built on a class this build does not carry, so there is nothing to make it from."), *GetAssetName())
			);

			return false;
		}

		AnimBlueprint = CreateAnimBlueprint(ParentClass);
	}

	if (!AnimBlueprint) return false;

	const TSharedPtr<FJsonObject> RootAnimNodeDefaults = GetExportStartingWith("Default__", "Name", GetContainer()->JsonObjects);
	if (!RootAnimNodeDefaults.IsValid()) return false;

	RootAnimNodeProperties = RootAnimNodeDefaults->GetObjectField(TEXT("Properties"));
	if (!RootAnimNodeProperties.IsValid()) return false;

	if (FBlueprintVariables::Construct(AnimBlueprint, FBlueprintVariables::GetDeclared(GetAssetExport(), GetContainer()), GetContainer()) > 0) {
		CompileBlueprintGuarded(AnimBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	}

#if UE4_24_BELOW
	UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(AnimBlueprint->GeneratedClass);
#else
	const UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(AnimBlueprint->GeneratedClass);
#endif
	GetObjectSerializer()->Exports = GetContainer()->JsonObjects;
	GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(RootAnimNodeProperties, {
		"RootComponent"
	}), GeneratedClass->GetDefaultObject());

	GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(GetAssetData(), {
		"FuncMap",
		"bCooked",
		"Children",
		"RootAnimNodeIndex",
		"UberGraphFunction",
		"UberGraphFramePointerProperty",
		"SuperStruct"
	}), AnimBlueprint);

	if (RootAnimNodeDefaults->HasField(TEXT("SerializedSparseClassData"))) {
		SerializedSparseClassData = RootAnimNodeDefaults->GetObjectField(TEXT("SerializedSparseClassData"));
	}

	if (const TSharedPtr<FJsonObject>* Held = nullptr; RootAnimNodeProperties->TryGetObjectField(TEXT("__AnimBlueprintMutables"), Held)) {
		AnimMutables = *Held;
	}

	if (GetAssetData()->HasField(TEXT("SyncGroupNames"))) {
		for (const auto& SyncGroupNameValue : GetAssetData()->GetArrayField(TEXT("SyncGroupNames"))) {
			SyncGroupNames.Add(SyncGroupNameValue->AsString());
		}
	}

	FilterAnimGraphNodeProperties(RootAnimNodeProperties);
	ProcessEvaluateGraphExposedInputs(RootAnimNodeProperties);

	for (const auto& Pair : RootAnimNodeProperties->Values) {
		NodesKeys.Add(JsonKeyToString(Pair.Key));
	}

	ReversedNodesKeys = NodesKeys;
	Algo::Reverse(ReversedNodesKeys);

	AnimNodeContainers.Empty();
	Borrowed.Empty();

	ReadFolded(RootAnimNodeProperties, NodesKeys);
	ReadFolded(RootAnimNodeProperties, NodesKeys);
	Unfold(RootAnimNodeProperties, NodesKeys);
	ReadBindings(RootAnimNodeProperties, NodesKeys);

	for (const FString& Key : NodesKeys) {
		TSharedPtr<FJsonValue> NodeValue = RootAnimNodeProperties->Values.FindChecked(StringToJsonKey(Key));
		if (!NodeValue.IsValid()) continue;

		ReplaceLinkID(NodeValue, NodesKeys);
		RootAnimNodeProperties->Values[StringToJsonKey(Key)] = NodeValue;
	}

	if (GetAssetData()->HasField(TEXT("BakedStateMachines"))) {
		BakedStateMachines = GetAssetData()->GetArrayField(TEXT("BakedStateMachines"));

		for (const TSharedPtr<FJsonValue>& MachineValue : BakedStateMachines) {
			const TSharedPtr<FJsonObject> MachineObject = MachineValue->AsObject();
			const TArray<TSharedPtr<FJsonValue>> States = MachineObject->GetArrayField(TEXT("States"));
			const FString MachineName = MachineObject->GetStringField(TEXT("MachineName"));

			for (const TSharedPtr<FJsonValue>& StateValue : States) {
				const TSharedPtr<FJsonObject> StateObject = StateValue->AsObject();
				const int32 StateRootNodeIndex = StateObject->GetIntegerField(TEXT("StateRootNodeIndex"));

				if (StateRootNodeIndex == -1 || !ReversedNodesKeys.IsValidIndex(StateRootNodeIndex)) {
					continue;
				}

				const FString StartKey = ReversedNodesKeys[StateRootNodeIndex];
				HarvestAndTagConnectedStateMachineNodes(StartKey, StateObject->GetStringField(TEXT("StateName")), MachineName, RootAnimNodeProperties->Values);
			}
		}
	}

	const TSharedPtr<FJsonObject> RootGraphAnimProperties = MakeShared<FJsonObject>(); {
		for (const FString& Key : NodesKeys) {
			const TSharedPtr<FJsonValue> NodeValue = RootAnimNodeProperties->Values.FindChecked(StringToJsonKey(Key));

			if (NodeValue->Type == EJson::Object) {
				const TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();

				if (!NodeObject->HasField(TEXT("State")) && !NodeObject->HasField(TEXT("Machine"))) {
					RootGraphAnimProperties->SetObjectField(Key, NodeObject);
				}
			}
		}
	}

	Blueprint = AnimBlueprint;

	if (const int32 Answered = ConstructInterfaces(); Answered > 0) {
		UE_LOG(LogReflection, Display, TEXT("%d interface(s) answered for"), Answered);
	}

	TMap<FString, FString> Belongs; {
		for (const FString& Key : NodesKeys) {
			const TSharedPtr<FJsonValue> NodeValue = RootAnimNodeProperties->Values.FindChecked(StringToJsonKey(Key));

			if (NodeValue->Type != EJson::Object) continue;

			FString Named;

			if (NodeValue->AsObject()->TryGetStringField(TEXT("Graph"), Named) && !Named.IsEmpty()) {
				Belongs.Add(Key, Named);
			}
		}

		const FUObjectJsonValueExport Data = GetAssetDataAsValue();

		for (const TCHAR* Field : { TEXT("OrderedSavedPoseIndicesMap"), TEXT("GraphAssetPlayerInformation") }) {
			if (!Data.Has(Field)) continue;

			for (const FUObjectJsonValueExport& Entry : Data.GetArray(Field)) {
				if (!Entry.Has(TEXT("Key")) || !Entry.Has(TEXT("Value"))) continue;

				FString Named = Entry.GetString(TEXT("Key"));

				if (Named == UEdGraphSchema_K2::GN_AnimGraph.ToString()) Named = FString();

				const FUObjectJsonValueExport Held = Entry.GetObject(TEXT("Value"));

				for (const TCHAR* Listed : { TEXT("OrderedSavedPoseNodeIndices"), TEXT("PlayerNodeIndices") }) {
					if (!Held.Has(Listed)) continue;

					const TArray<TSharedPtr<FJsonValue>>* Listing = nullptr;

					if (!Held.JsonObject.IsValid() || !Held.JsonObject->TryGetArrayField(Listed, Listing)) continue;

					for (const TSharedPtr<FJsonValue>& Which : *Listing) {
						const int32 Index = Which.IsValid() ? static_cast<int32>(Which->AsNumber()) : INDEX_NONE;

						if (ReversedNodesKeys.IsValidIndex(Index)) Belongs.Add(ReversedNodesKeys[Index], Named);
					}
				}
			}
		}
	}

	auto Borrows = [](const FString& Property) {
		return Property == TEXT("LinkToCachingNode");
	};

	TFunction<void(const FString&, TSet<FString>&)> Gather = [&](const FString& Key, TSet<FString>& Into) {
		if (Into.Contains(Key) || !RootGraphAnimProperties->HasField(Key)) return;

		Into.Add(Key);

		for (const TPair<FString, FString>& Link : FindLinkIDs(MakeShared<FJsonValueObject>(RootGraphAnimProperties->GetObjectField(Key)), Key)) {
			if (Borrows(Link.Key)) continue;

			Gather(Link.Value, Into);
		}
	};

	TMap<FString, TSharedPtr<FJsonObject>> Made;

	{
		TSet<FString> Fed;

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RootGraphAnimProperties->Values) {
			for (const TPair<FString, FString>& Link : FindLinkIDs(Pair.Value, JsonKeyToString(Pair.Key))) {
				if (Borrows(Link.Key)) continue;

				Fed.Add(Link.Value);
			}
		}

		TMap<FString, FString> Claimed = Belongs;

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RootGraphAnimProperties->Values) {
			const FString Key = JsonKeyToString(Pair.Key);

			if (Fed.Contains(Key)) continue;

			TSet<FString> Owned;
			Gather(Key, Owned);

			FString Named;

			for (const FString& Held : Owned) {
				if (const FString* Says = Belongs.Find(Held)) {
					Named = *Says;

					break;
				}
			}

			if (Named.IsEmpty()) {
				for (const FString& Held : Owned) {
					for (const TPair<FString, FString>& Link : FindLinkIDs(MakeShared<FJsonValueObject>(RootGraphAnimProperties->GetObjectField(Held)), Held)) {
						if (!Borrows(Link.Key)) continue;

						if (const FString* Says = Belongs.Find(Link.Value)) {
							Named = *Says;

							break;
						}
					}

					if (!Named.IsEmpty()) break;
				}
			}

			for (const FString& Held : Owned) {
				if (!Claimed.Contains(Held)) Claimed.Add(Held, Named);
			}
		}

		for (const TPair<FString, FString>& One : Claimed) {
			if (!RootGraphAnimProperties->HasField(One.Key)) continue;

			TSharedPtr<FJsonObject>& Into = Made.FindOrAdd(One.Value);

			if (!Into.IsValid()) Into = MakeShared<FJsonObject>();

			Into->SetObjectField(One.Key, RootGraphAnimProperties->GetObjectField(One.Key));
		}
	}

	const auto SettleOutputPose = [](UEdGraph* Graph) {
		if (Graph == nullptr || Graph->GetSchema() == nullptr) return;

		TArray<UAnimGraphNode_Root*> Poses;
		Graph->GetNodesOfClass<UAnimGraphNode_Root>(Poses);

		if (Poses.Num() == 0) {
			Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);

			return;
		}

		const auto IsFed = [](const UAnimGraphNode_Root* Pose) {
			for (const UEdGraphPin* Pin : Pose->Pins) {
				if (Pin != nullptr && Pin->LinkedTo.Num() > 0) return true;
			}

			return false;
		};

		int32 Kept = 0;

		for (int32 At = 1; At < Poses.Num(); ++At) {
			if (IsFed(Poses[At]) && !IsFed(Poses[Kept])) Kept = At;
		}

		for (int32 At = Poses.Num() - 1; At >= 0; --At) {
			if (At == Kept) continue;

			UE_LOG(LogReflection, Display, TEXT("\"%s\" was left with an output pose too many, so one was taken out"), *Graph->GetName());

			Graph->RemoveNode(Poses[At]);
		}
	};

	UEdGraph* AnimGraph = FindAnimGraph(AnimBlueprint);

	if (AnimGraph == nullptr && AnimBlueprint->BlueprintType != BPTYPE_Interface) {
		AnimGraph = FBlueprintEditorUtils::CreateNewGraph(AnimBlueprint, UEdGraphSchema_K2::GN_AnimGraph, UAnimationGraph::StaticClass(), UAnimationGraphSchema::StaticClass());

		if (AnimGraph != nullptr) {
			FBlueprintEditorUtils::AddDomainSpecificGraph(AnimBlueprint, AnimGraph);

			AnimGraph->bAllowDeletion = false;

			UE_LOG(LogReflection, Display, TEXT("\"%s\" had no animation graph, so it was given one"), *GetAssetName());
		}
	}

	if (AnimGraph) {
		AnimGraph->SubGraphs.Empty();
	}

	if (const TSharedPtr<FJsonObject>* Main = Made.Find(FString()); Main != nullptr && Main->IsValid()) {
		CreateGraph(*Main, AnimGraph, RootAnimNodeContainer);
	} else {
		CreateGraph(RootGraphAnimProperties, AnimGraph, RootAnimNodeContainer);
	}

	SettleOutputPose(AnimGraph);

	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);

	for (const TPair<FString, TSharedPtr<FJsonObject>>& One : Made) {
		if (One.Key.IsEmpty() || !One.Value.IsValid()) continue;

		UEdGraph* Layer = nullptr;

		for (UEdGraph* Candidate : Graphs) {
			if (Candidate != nullptr && Candidate->GetName() == One.Key) {
				Layer = Candidate;

				break;
			}
		}

		if (Layer == nullptr) {
			FImportIssues::Report(
				EImportIssue::MissingClass,
				TEXT("A layer with nowhere to lay it out"),
				FString::Printf(TEXT("'%s' has nodes for the layer '%s', and no graph of that name to put them in."), *GetAssetName(), *One.Key)
			);

			continue;
		}

		Layer->SubGraphs.Empty();

		FString Had;

		for (const UEdGraphNode* Node : Layer->Nodes) {
			if (Node != nullptr) Had += FString::Printf(TEXT("%s "), *Node->GetName());
		}

		FString Adding;

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : One.Value->Values) {
			Adding += FString::Printf(TEXT("%s "), *JsonKeyToString(Pair.Key));
		}

		CreateGraph(One.Value, Layer, new FUObjectExportContainer());

		SettleOutputPose(Layer);
		SettleInputPoses(Layer);

		UE_LOG(LogReflection, Display, TEXT("layer \"%s\" laid out: it already had [%s], and was given [%s]"),
			*One.Key, Had.IsEmpty() ? TEXT("nothing") : *Had, *Adding);
	}

	SettleCachedPoses();

	Blueprint = AnimBlueprint;

	ConstructBody();

	if (const int32 Blended = ConstructBlendOptions(); Blended > 0) {
		UE_LOG(LogReflection, Display, TEXT("%d graph(s) blend the way they were set to"), Blended);
	}

	FPreviewMesh::FromFolder(AnimBlueprint, AnimBlueprint->TargetSkeleton);

	return OnAssetCreation(AnimBlueprint);
}

UAnimBlueprint* IAnimationBlueprintImporter::CreateAnimBlueprint(UClass* ParentClass) {
	const EBlueprintType BlueprintType = GetBlueprintTypeSaid(GetAssetData(), ParentClass);

	if (UBlueprint* Made = CreateBlueprintGuarded(ParentClass, GetPackage(), FName(*GetAssetName()), BlueprintType, UAnimBlueprint::StaticClass(), UAnimBlueprintGeneratedClass::StaticClass())) {
		return Cast<UAnimBlueprint>(IImporter::CreateAsset(Made));
	}

	return nullptr;
}

void IAnimationBlueprintImporter::ReadFolded(const TSharedPtr<FJsonObject>& Nodes, const TArray<FString>& Keys) {
	if (!Nodes.IsValid()) return;

	const FUObjectJsonValueExport Data = GetAssetDataAsValue();

	if (!Data.Has(TEXT("AnimNodeData")) || !Data.Has(TEXT("NodeTypeMap"))) return;

	Constants.Empty();
	Mutables.Empty();
	MutableAt.Empty();
	Slots.Empty();
	Folded.Empty();
	Kinds.Empty();

	if (SerializedSparseClassData.IsValid()) {
		for (const TPair<FString, TSharedPtr<FJsonValue>>& One : SerializedSparseClassData->Values) {
			Constants.Add(One.Value);
		}
	}

	if (AnimMutables.IsValid()) {
		for (const TPair<FString, TSharedPtr<FJsonValue>>& One : AnimMutables->Values) {
			FString Named = JsonKeyToString(One.Key);

			Named.RemoveFromStart(TEXT("__"));

			MutableAt.Add(Named, Mutables.Num());

			Mutables.Add(One.Value);
		}
	}

	for (const FUObjectJsonValueExport& One : Data.GetArray(TEXT("NodeTypeMap"))) {
		FString Kind = One.GetString(TEXT("Key"));

		if (FString Rest; Kind.Split(TEXT("."), nullptr, &Rest, ESearchCase::IgnoreCase, ESearchDir::FromEnd)) {
			Kind = Rest;
		}

		Kind.RemoveFromEnd(TEXT("'"));

		if (Kind.IsEmpty()) continue;

		TMap<FString, int32>& Named = Slots.FindOrAdd(Kind);

		for (const FUObjectJsonValueExport& Says : One.GetObject(TEXT("Value")).GetArray(TEXT("NameToIndexMap"))) {
			Named.Add(Says.GetString(TEXT("Key")), Says.GetInteger(TEXT("Value"), INDEX_NONE));
		}
	}

	for (const FUObjectJsonValueExport& One : Data.GetArray(TEXT("AnimNodeData"))) {
		const int32 Which = One.GetInteger(TEXT("NodeIndex"), INDEX_NONE);

		if (Which == INDEX_NONE || !One.Has(TEXT("Entries"))) continue;

		TArray<uint32> Says;

		for (const FUObjectJsonValueExport& Entry : One.GetArray(TEXT("Entries"))) {
			Says.Add(Entry.Value.IsValid() ? static_cast<uint32>(Entry.Value->AsNumber()) : MAX_uint32);
		}

		Folded.Add(Which, Says);
	}

	for (const FString& Key : Keys) {
		FString NodeType, Rest;

		Key.Split(TEXT("_"), &NodeType, &Rest, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

		if (NodeType == TEXT("AnimGraphNode") || NodeType.IsEmpty()) NodeType = Key;

		UClass* Class = FindClassByType(NodeType);

		if (Class == nullptr) continue;

		const UAnimGraphNode_Base* Says = Cast<UAnimGraphNode_Base>(Class->GetDefaultObject());

		if (const FStructProperty* Inside = GetNodeStructProperty(Says); Inside != nullptr && Inside->Struct != nullptr) {
			Kinds.Add(Key, Inside->Struct->GetName());
		}
	}

	UE_LOG(LogReflection, Display, TEXT("the class keeps %d folded value(s) and %d that change, over %d node(s) of %d kind(s)"),
		Constants.Num(), Mutables.Num(), Folded.Num(), Slots.Num());

}

void IAnimationBlueprintImporter::Unfold(const TSharedPtr<FJsonObject>& Nodes, const TArray<FString>& Keys) const {
	if (!Nodes.IsValid() || (Constants.Num() == 0 && Mutables.Num() == 0)) return;

	int32 Put = 0;

	for (int32 Which = 0; Which < Keys.Num(); ++Which) {
		const TArray<uint32>* Entries = Folded.Find(Which);
		const FString* Kind = Kinds.Find(Keys[Which]);

		if (Entries == nullptr || Kind == nullptr) continue;

		const TMap<FString, int32>* Named = Slots.Find(*Kind);

		const TSharedPtr<FJsonObject>* Node = nullptr;

		if (Named == nullptr || !Nodes->TryGetObjectField(StringToJsonKey(Keys[Which]), Node)) continue;

		for (const TPair<FString, int32>& One : *Named) {
			if (!Entries->IsValidIndex(One.Value)) continue;

			if ((*Node)->HasField(One.Key)) continue;

			const uint32 Entry = (*Entries)[One.Value];

			if (Entry == MAX_uint32) continue;

			const TArray<TSharedPtr<FJsonValue>>& From = (Entry & 0x80000000u) != 0 ? Mutables : Constants;

			const int32 At = static_cast<int32>(Entry & 0x7FFFFFFFu);

			if (!From.IsValidIndex(At)) continue;

			(*Node)->SetField(One.Key, From[At]);

			Put++;
		}
	}

	if (Put > 0) {
		UE_LOG(LogReflection, Display, TEXT("%d folded value(s) put back on the nodes that hold them"), Put);
	}
}

FName IAnimationBlueprintImporter::MemberAt(const FString& Key, const int32 Which, const uint32 Entry) const {
	const TArray<uint32>* Entries = Folded.Find(Which);
	const FString* Kind = Kinds.Find(Key);

	if (Entries == nullptr || Kind == nullptr) return NAME_None;

	const TMap<FString, int32>* Named = Slots.Find(*Kind);

	if (Named == nullptr) return NAME_None;

	for (const TPair<FString, int32>& One : *Named) {
		if (Entries->IsValidIndex(One.Value) && (*Entries)[One.Value] == Entry) return FName(*One.Key);
	}

	return NAME_None;
}

FString IAnimationBlueprintImporter::NodeAt(const TArray<FString>& Keys, const uint32 Entry, int32& Which) const {
	for (const TPair<int32, TArray<uint32>>& One : Folded) {
		if (!Keys.IsValidIndex(One.Key)) continue;

		for (const uint32 Held : One.Value) {
			if (Held != Entry) continue;

			Which = One.Key;

			return Keys[One.Key];
		}
	}

	Which = INDEX_NONE;

	return FString();
}

void IAnimationBlueprintImporter::ReadBindings(const TSharedPtr<FJsonObject>& Nodes, const TArray<FString>& Keys) {
	if (!SerializedSparseClassData.IsValid()) return;

	Bindings.Empty();

	TMap<FString, TPair<FString, FName>> Handed;

	if (Nodes.IsValid()) {
		for (const FString& Key : Keys) {
			const TSharedPtr<FJsonObject>* Node = nullptr;

			if (!Nodes->TryGetObjectField(StringToJsonKey(Key), Node)) continue;

			const TArray<TSharedPtr<FJsonValue>>* Fed = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Called = nullptr;

			if (!(*Node)->TryGetArrayField(TEXT("SourcePropertyNames"), Fed)) continue;
			if (!(*Node)->TryGetArrayField(TEXT("DestPropertyNames"), Called)) continue;

			for (int32 At = 0; At < Fed->Num() && At < Called->Num(); ++At) {
				if (!(*Fed)[At].IsValid() || !(*Called)[At].IsValid()) continue;

				Handed.Add((*Fed)[At]->AsString(), TPair<FString, FName>(Key, FName(*(*Called)[At]->AsString())));
			}
		}
	}

	const TSharedPtr<FJsonObject>* Access = nullptr;

	if (!SerializedSparseClassData->TryGetObjectField(TEXT("AnimBlueprintExtension_PropertyAccess"), Access)) return;

	const TSharedPtr<FJsonObject>* Library = nullptr;

	if (!(*Access)->TryGetObjectField(TEXT("Library"), Library)) return;

	const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Sources = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Destinations = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Batches = nullptr;

	if (!(*Library)->TryGetArrayField(TEXT("PathSegments"), Steps)) return;
	if (!(*Library)->TryGetArrayField(TEXT("SrcPaths"), Sources)) return;
	if (!(*Library)->TryGetArrayField(TEXT("DestPaths"), Destinations)) return;
	if (!(*Library)->TryGetArrayField(TEXT("CopyBatchArray"), Batches)) return;

	TArray<TSharedPtr<FJsonObject>> Copies;

	for (const TSharedPtr<FJsonValue>& Batch : *Batches) {
		const TArray<TSharedPtr<FJsonValue>>* Held = nullptr;

		if (Batch.IsValid() && Batch->AsObject().IsValid() && Batch->AsObject()->TryGetArrayField(TEXT("Copies"), Held)) {
			for (const TSharedPtr<FJsonValue>& One : *Held) Copies.Add(One->AsObject());
		}
	}

	TMap<int32, FString> Doing;
	TMap<int32, int32> Whose;

	if (const TSharedPtr<FJsonObject>* Base = nullptr; SerializedSparseClassData->TryGetObjectField(TEXT("AnimBlueprintExtension_Base"), Base)) {
		const TArray<TSharedPtr<FJsonValue>>* Handlers = nullptr;

		if ((*Base)->TryGetArrayField(TEXT("ExposedValueHandlers"), Handlers)) {
			for (int32 Which = 0; Which < Handlers->Num(); ++Which) {
				const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;

				if (!(*Handlers)[Which]->AsObject()->TryGetArrayField(TEXT("CopyRecords"), Records)) continue;

				for (const TSharedPtr<FJsonValue>& Record : *Records) {
					const TSharedPtr<FJsonObject> Says = Record->AsObject();

					if (!Says.IsValid()) continue;

					const int32 Index = Says->GetIntegerField(TEXT("CopyIndex"));

					Whose.Add(Index, Which);

					if (FString Done; Says->TryGetStringField(TEXT("PostCopyOperation"), Done)) Doing.Add(Index, Done);
				}
			}
		}
	}

	const auto Walk = [Steps](const TSharedPtr<FJsonObject>& Path, TArray<FString>& Into) {
		if (!Path.IsValid()) return;

		const int32 From = Path->GetIntegerField(TEXT("PathSegmentStartIndex"));
		const int32 Many = Path->GetIntegerField(TEXT("PathSegmentCount"));

		for (int32 At = From; At < From + Many; ++At) {
			if (!Steps->IsValidIndex(At)) return;

			FString Named;

			if ((*Steps)[At]->AsObject()->TryGetStringField(TEXT("Name"), Named)) Into.Add(Named);
		}
	};

	int32 Made = 0;

	for (int32 Index = 0; Index < Copies.Num(); ++Index) {
		const TSharedPtr<FJsonObject>& Copy = Copies[Index];

		if (!Copy.IsValid()) continue;

		TArray<FString> From;
		TArray<FString> Onto;

		if (const int32 At = Copy->GetIntegerField(TEXT("AccessIndex")); Sources->IsValidIndex(At)) {
			Walk((*Sources)[At]->AsObject(), From);
		}

		if (const int32 At = Copy->GetIntegerField(TEXT("DestAccessStartIndex")); Destinations->IsValidIndex(At)) {
			Walk((*Destinations)[At]->AsObject(), Onto);
		}

		if (From.Num() == 0 || Onto.Num() == 0) continue;

		FString Key;
		FName Member;

		if (Onto[0] == TEXT("__AnimBlueprintMutables")) {
			FString Slotted = Onto.Last();

			Slotted.RemoveFromStart(TEXT("__"));

			const int32* Slot = MutableAt.Find(Slotted);

			if (Slot == nullptr) {
				FImportIssues::Report(
					EImportIssue::Data,
					TEXT("A hand-over to a slot the class does not keep"),
					FString::Printf(TEXT("'%s' hands \"%s\" through \"%s\", and the class keeps %d slot(s), none of them under that name."),
						*GetAssetName(), *FString::Join(From, TEXT(".")), *Onto.Last(), MutableAt.Num())
				);

				continue;
			}

			const uint32 Entry = 0x80000000u | static_cast<uint32>(*Slot);

			const int32* Told = Whose.Find(Index);

			int32 Which = Told != nullptr ? *Told : INDEX_NONE;

			Key = Keys.IsValidIndex(Which) ? Keys[Which] : NodeAt(Keys, Entry, Which);

			if (Key.IsEmpty() || Which == INDEX_NONE) {
				FImportIssues::Report(
					EImportIssue::Data,
					TEXT("A hand-over with nowhere to land"),
					FString::Printf(TEXT("'%s' hands \"%s\" through \"%s\", which is slot %d, and no node reads that slot."),
						*GetAssetName(), *FString::Join(From, TEXT(".")), *Onto.Last(), *Slot)
				);

				continue;
			}

			Member = MemberAt(Key, Which, Entry);

			if (Member.IsNone()) continue;
		} else if (Onto[0].StartsWith(TEXT("__"))) {
			const TPair<FString, FName>* Says = Handed.Find(Onto[0]);

			if (Says == nullptr) continue;

			Key = Says->Key;
			Member = Says->Value;
		} else {
			Key = Onto[0];
			Member = FName(*Onto.Last());
		}

		if (!Kinds.Contains(Key)) continue;

		bool bTurned = false;

		if (const FString* Done = Doing.Find(Index); Done != nullptr && !Done->EndsWith(TEXT("::None"))) {
			if (!Done->EndsWith(TEXT("::LogicalNegateBool")) || From.Num() != 1) {
				FImportIssues::Report(
					EImportIssue::Data,
					TEXT("A binding that does something to the value on the way"),
					FString::Printf(TEXT("'%s' hands \"%s\" to %s.%s as %s, which a pin binding has no way of saying."),
						*GetAssetName(), *FString::Join(From, TEXT(".")), *Key, *Member.ToString(), **Done)
				);

				continue;
			}

			bTurned = true;
		}

		Bindings.FindOrAdd(Key).Add(FHandedOver{ Member, From, bTurned });

		Made++;
	}

	if (Made > 0) {
		UE_LOG(LogReflection, Display, TEXT("%d pin(s) are handed a variable rather than fed by a wire"), Made);
	}
}

void IAnimationBlueprintImporter::Draw(UAnimGraphNode_Base* Node, const FName Member, const FString& Named, const bool bTurned, const int32 Row) const {
	UEdGraph* Into = Node->GetGraph();

	UEdGraphPin* Onto = Node->FindPin(Member, EGPD_Input);

	if (Onto == nullptr && Expose(Node, Member)) {
		Node->ReconstructNode();

		Onto = Node->FindPin(Member, EGPD_Input);
	}

	if (Into == nullptr || Onto == nullptr) {
		if (Onto == nullptr) {
			UE_LOG(LogReflection, Warning, TEXT("\"%s\" is handed to %s.%s, which has no pin and will not take one"),
				*Named, *Node->GetName(), *Member.ToString());
		}

		return;
	}

	if (Onto->LinkedTo.Num() > 0) return;

	UK2Node_VariableGet* Reads = NewObject<UK2Node_VariableGet>(Into, UK2Node_VariableGet::StaticClass(), NAME_None, RF_Transactional);

	Into->AddNode(Reads, false, false);

	Reads->CreateNewGuid();
	Reads->VariableReference.SetSelfMember(FName(*Named));
	Reads->AllocateDefaultPins();
	Reads->PostPlacedNewNode();

	UEdGraphPin* Held = Reads->FindPin(FName(*Named), EGPD_Output);

	if (Held == nullptr) {
		UE_LOG(LogReflection, Warning, TEXT("\"%s\" is read for %s and hands nothing out"), *Named, *Member.ToString());

		return;
	}

	if (bTurned) {
		UK2Node_CallFunction* Turns = NewObject<UK2Node_CallFunction>(Into, UK2Node_CallFunction::StaticClass(), NAME_None, RF_Transactional);

		Into->AddNode(Turns, false, false);

		Turns->CreateNewGuid();
		Turns->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Not_PreBool), UKismetMathLibrary::StaticClass());
		Turns->AllocateDefaultPins();
		Turns->PostPlacedNewNode();

		UEdGraphPin* Takes = Turns->FindPin(TEXT("A"), EGPD_Input);
		UEdGraphPin* Gives = Turns->GetReturnValuePin();

		if (Takes == nullptr || Gives == nullptr) {
			UE_LOG(LogReflection, Warning, TEXT("\"%s\" turned round for %s has nowhere to go"), *Named, *Member.ToString());

			return;
		}

		Held->MakeLinkTo(Takes);
		Gives->MakeLinkTo(Onto);

		Turns->NodePosX = Node->NodePosX - 220;
		Turns->NodePosY = Node->NodePosY + Row * 90;

		Reads->NodePosX = Turns->NodePosX - 220;
		Reads->NodePosY = Turns->NodePosY;

		Into->NotifyGraphChanged();

		return;
	}

	Held->MakeLinkTo(Onto);

	Reads->NodePosX = Node->NodePosX - 220;
	Reads->NodePosY = Node->NodePosY + Row * 90;

	Into->NotifyGraphChanged();
}

void IAnimationBlueprintImporter::Hands(const FString& Key, const FName Member, const TArray<FString>& Path, const bool bTurned) {
	if (Key.IsEmpty() || Member.IsNone() || Path.Num() == 0) return;

	TArray<FHandedOver>& Says = Bindings.FindOrAdd(Key);

	for (const FHandedOver& One : Says) {
		if (One.Member == Member) return;
	}

	Says.Add(FHandedOver{ Member, Path, bTurned });
}

void IAnimationBlueprintImporter::ApplyBindings(const FString& Key, UAnimGraphNode_Base* Node) const {
	if (Node == nullptr || Key.IsEmpty()) return;

	const TArray<FHandedOver>* Says = Bindings.Find(Key);

	if (Says == nullptr) return;

	int32 Row = 0;

	for (const FHandedOver& One : *Says) {
		if (Expose(Node, One.Member)) {
			Node->ReconstructNode();
		}

		if (One.Path.Num() == 1) {
			Draw(Node, One.Member, One.Path[0], One.bTurned, Row++);

			continue;
		}

#if !UE4_25_BELOW
		FAnimGraphNodePropertyBinding Binding;

		Binding.PropertyName = One.Member;
		Binding.PropertyPath = One.Path;
		Binding.PathAsText = FText::FromString(FString::Join(One.Path, TEXT(".")));
		Binding.bIsBound = true;

		if (const UEdGraphPin* Pin = Node->FindPin(One.Member, EGPD_Input)) {
			Binding.PinType = Pin->PinType;
		}

		if (!AddPropertyBinding(Node, One.Member, Binding)) {
			UE_LOG(LogReflection, Warning, TEXT("\"%s\" would be handed to %s.%s, and there is no pin of that name"),
				*Binding.PathAsText.ToString(), *Node->GetClass()->GetName(), *One.Member.ToString());
		}
#endif
	}
}

void IAnimationBlueprintImporter::ExposeHandedOver(UAnimGraphNode_Base* Node, const TArray<FString>& Named) const {
	const FArrayProperty* Exposed = FindFProperty<FArrayProperty>(Node->GetClass(), TEXT("CustomPinProperties"));

	if (Exposed == nullptr) return;

	const FStructProperty* Inner = CastField<FStructProperty>(Exposed->Inner);

	if (Inner == nullptr || Inner->Struct != FOptionalPinFromProperty::StaticStruct()) return;

	FScriptArrayHelper Held(Exposed, Exposed->ContainerPtrToValuePtr<void>(Node));

	for (const FString& One : Named) {
		const FName Which(*One);

		bool bAlready = false;

		for (int32 At = 0; At < Held.Num(); ++At) {
			FOptionalPinFromProperty* Says = reinterpret_cast<FOptionalPinFromProperty*>(Held.GetRawPtr(At));

			if (Says->PropertyName != Which) continue;

			Says->bShowPin = true;
			bAlready = true;

			break;
		}

		if (bAlready) continue;

		FOptionalPinFromProperty* Says = reinterpret_cast<FOptionalPinFromProperty*>(Held.GetRawPtr(Held.AddValue()));

		Says->PropertyName = Which;
		Says->bCanToggleVisibility = true;
		Says->bShowPin = true;
	}

	UE_LOG(LogReflection, Display, TEXT("\"%s\" hands over %d value(s), which it is given pins for"), *Node->GetName(), Named.Num());
}

namespace {
	bool ExposeNamed(UAnimGraphNode_Base* Node, const FName Member) {
		for (FOptionalPinFromProperty& Held : Node->ShowPinForProperties) {
			if (Held.PropertyName != Member) continue;

			if (Held.bShowPin) return false;

			Held.bShowPin = true;

			return true;
		}

		const FArrayProperty* Exposed = FindFProperty<FArrayProperty>(Node->GetClass(), TEXT("CustomPinProperties"));

		if (Exposed == nullptr) return false;

		const FStructProperty* Inner = CastField<FStructProperty>(Exposed->Inner);

		if (Inner == nullptr || Inner->Struct != FOptionalPinFromProperty::StaticStruct()) return false;

		FScriptArrayHelper Holds(Exposed, Exposed->ContainerPtrToValuePtr<void>(Node));

		for (int32 At = 0; At < Holds.Num(); ++At) {
			FOptionalPinFromProperty* Says = reinterpret_cast<FOptionalPinFromProperty*>(Holds.GetRawPtr(At));

			if (Says->PropertyName != Member) continue;

			if (Says->bShowPin) return false;

			Says->bShowPin = true;

			return true;
		}

		return false;
	}
}

bool IAnimationBlueprintImporter::Expose(UAnimGraphNode_Base* Node, const FName Member) const {
	if (ExposeNamed(Node, Member)) return true;

	FString Named = Member.ToString();

	int32 Last = INDEX_NONE;

	if (!Named.FindLastChar(TEXT('_'), Last)) return false;

	const FString Index = Named.Mid(Last + 1);

	if (Index.IsEmpty() || !Index.IsNumeric()) return false;

	return ExposeNamed(Node, FName(*Named.Left(Last)));
}

void IAnimationBlueprintImporter::AnswerThrough(FUObjectExportContainer* Container) {
	if (Container == nullptr) return;

	for (const FUObjectExport* NodeExport : Container->Exports) {
		UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(NodeExport->Object);

		if (Node == nullptr) continue;

		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin == nullptr || Pin->Direction != EGPD_Input) continue;

			if (UAnimationGraphSchema::IsPosePin(Pin->PinType)) continue;

			Answers(NodeExport->GetName().ToString(), Pin->PinName, Pin);
		}
	}
}

void IAnimationBlueprintImporter::DrawBindings(FUObjectExportContainer* Container) const {
	if (Container == nullptr) return;

	for (const FUObjectExport* NodeExport : Container->Exports) {
		if (UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(NodeExport->Object)) {
			ApplyBindings(NodeExport->GetName().ToString(), Node);
		}
	}
}

void IAnimationBlueprintImporter::ShowBoundPins(const TSharedPtr<FJsonObject>& AnimNodeProperties, FUObjectExportContainer* Container) const {
	if (Container == nullptr) return;

	for (const FUObjectExport* NodeExport : Container->Exports) {
		UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(NodeExport->Object);

		if (Node == nullptr) continue;

		TArray<FName> Says;

#if !UE4_25_BELOW
		Says = BoundPinNames(Node);
#endif

		for (const FHandedOver& One : Bindings.FindRef(NodeExport->GetName().ToString())) {
			Says.AddUnique(One.Member);
		}

		if (Says.Num() == 0) continue;

		bool bAny = false;

		for (const FName One : Says) {
			bAny |= Expose(Node, One);
		}

		if (bAny) {
			Node->ReconstructNode();
		}
	}
}

void IAnimationBlueprintImporter::CreateGraph(const TSharedPtr<FJsonObject>& AnimNodeProperties, UEdGraph* AnimGraph, FUObjectExportContainer* Container) {
	if (AnimGraph == nullptr) {
		const int32 Waiting = AnimNodeProperties.IsValid() ? AnimNodeProperties->Values.Num() : 0;

		if (Waiting > 0) {
			FImportIssues::Report(
				EImportIssue::Data,
				TEXT("Animation nodes with no graph to lay them out in"),
				FString::Printf(TEXT("'%s' carries %d animation node(s) and has no animation graph to put them in."), *GetAssetName(), Waiting)
			);
		}

		return;
	}

	AnimNodeContainers.AddUnique(Container);

	if (AnimGraph) {
		for (UEdGraphNode* Node : AnimGraph->Nodes) {
			if (Node) {
				Node->BreakAllNodeLinks();
				Node->ConditionalBeginDestroy();
			}
		}

		AnimGraph->Nodes.Empty();
		AnimGraph->SubGraphs.Empty();
	}

	CreateAnimGraphNodes(AnimGraph, AnimNodeProperties, *Container);
	AddNodesToGraph(AnimGraph, Container);

	HandleNodeDeserialization(Container);
	ShowBoundPins(AnimNodeProperties, Container);
	ConnectAnimGraphNodes(Container, AnimGraph);
	AutoLayoutAnimGraphNodes(Container->Exports);
	DrawBindings(Container);

	for (const FUObjectExport* ExportNode : Container->Exports) {
		const TSharedPtr<FJsonObject> ExportJsonObject = ExportNode->JsonObject;

		if (UAnimGraphNode_StateMachine* StateMachine = Cast<UAnimGraphNode_StateMachine>(ExportNode->Object)) {
			UAnimationStateMachineGraph* EditorStateMachineGraph = CastChecked<UAnimationStateMachineGraph>(FBlueprintEditorUtils::CreateNewGraph(StateMachine, NAME_None, UAnimationStateMachineGraph::StaticClass(), UAnimationStateMachineSchema::StaticClass()));
			EditorStateMachineGraph->OwnerAnimGraphNode = StateMachine;

			const TSharedPtr<FJsonObject> StateMachineObject = BakedStateMachines[ExportJsonObject->GetIntegerField(TEXT("StateMachineIndexInClass"))]->AsObject();

			FString MachineName = StateMachineObject->GetStringField(TEXT("MachineName"));
			EditorStateMachineGraph->Rename(*MachineName);

			const UEdGraphSchema* Schema = EditorStateMachineGraph->GetSchema();
			Schema->CreateDefaultNodesForGraph(*EditorStateMachineGraph);

			UEdGraph* ParentGraph = StateMachine->GetGraph();

			if(ParentGraph->SubGraphs.Find(EditorStateMachineGraph) == INDEX_NONE) {
				ParentGraph->Modify();
				ParentGraph->SubGraphs.Add(EditorStateMachineGraph);
			}

			StateMachine->EditorStateMachineGraph = EditorStateMachineGraph;
			CreateStateMachineGraph(EditorStateMachineGraph, StateMachineObject, GetObjectSerializer(), RootAnimNodeContainer, ReversedNodesKeys, this, AnimBlueprint);

			if (!StateMachineObject->HasField(TEXT("States"))) continue;

			TArray<TSharedPtr<FJsonValue>> States = StateMachineObject->GetArrayField(TEXT("States"));

			for (const TSharedPtr<FJsonValue>& StateValue : States) {
				const TSharedPtr<FJsonObject> StateObject = StateValue->AsObject();
				FString StateName = StateObject->GetStringField(TEXT("StateName"));

				UAnimationStateGraph* Graph = nullptr;

				for (UEdGraph* SubGraph : EditorStateMachineGraph->SubGraphs) {
					if (SubGraph->GetName() == StateName) {
						Graph = Cast<UAnimationStateGraph>(SubGraph);
					}
				}

				TSharedPtr<FJsonObject> StateMachineAnimNodeProperties = MakeShared<FJsonObject>();

				for (const auto& Pair : RootAnimNodeProperties->Values) {
					const FString Key = JsonKeyToString(Pair.Key);
					const TSharedPtr<FJsonObject> Value = Pair.Value->AsObject();

					if (!Value.IsValid()) continue;

					if (Value->HasField(TEXT("State")) && Value->HasField(TEXT("Machine"))) {
						const FString NodeStateName = Value->GetStringField(TEXT("State"));
						const FString NodeMachineName = Value->GetStringField(TEXT("Machine"));

						if (StateName == NodeStateName && NodeMachineName == MachineName) {
							StateMachineAnimNodeProperties->SetObjectField(Key, Value);
						}
					}
				}

				if (Graph) {
					FUObjectExportContainer* StateMachineContainer = new FUObjectExportContainer();
					CreateGraph(StateMachineAnimNodeProperties, Graph, StateMachineContainer);

					if (Graph->MyResultNode) {
						Graph->MyResultNode->BreakAllNodeLinks();
						Graph->RemoveNode(Graph->MyResultNode);
						Graph->MyResultNode->ConditionalBeginDestroy();
						Graph->MyResultNode = nullptr;
					}

					for (const FUObjectExport* StateMachineExport : StateMachineContainer->Exports) {
						if (UAnimGraphNode_StateResult* StateResult = Cast<UAnimGraphNode_StateResult>(StateMachineExport->Object)) {
							Graph->MyResultNode = StateResult;
						}
					}
				}
			}
		}
	}
}

void inline LinkPoseInputPin(const FString& PinName, UAnimGraphNode_Base* Node, UAnimGraphNode_Base* TargetNode, UEdGraph* AnimGraph) {
	UEdGraphPin* InputPin = Node->FindPin(PinName, EGPD_Input);
	UEdGraphPin* OutputPin = GetFirstOutputPin(TargetNode);

	if (InputPin && OutputPin) {
		InputPin->MakeLinkTo(OutputPin);
		InputPin->DefaultValue.Reset();

		Node->Modify();
		TargetNode->Modify();
		AnimGraph->Modify();

		return;
	}

	UE_LOG(LogReflection, Warning, TEXT("\"%s\" was to feed %s.%s: %s"),
		*TargetNode->GetName(), *Node->GetName(), *PinName,
		InputPin == nullptr
			? (OutputPin == nullptr ? TEXT("neither end has a pin for it") : TEXT("it has no pin of that name"))
			: TEXT("what feeds it hands nothing out"));
}

void IAnimationBlueprintImporter::UpdateBlendListByEnumVisibleEntries(FUObjectExport* NodeExport, FUObjectExportContainer* Container, UEdGraph* AnimGraph) {
	TSharedPtr<FJsonObject> NodeJsonObject = NodeExport->JsonObject;
	UAnimGraphNode_BlendListByEnum* BlendListByEnum = Cast<UAnimGraphNode_BlendListByEnum>(NodeExport->Object);

    if (!BlendListByEnum || !NodeJsonObject) {
        return;
    }

    const UEnum* BoundEnum = BlendListByEnum->GetEnum();
    if (!BoundEnum) {
        return;
    }

	TArray<TSharedPtr<FJsonValue>> EnumToPoseIndexArray = NodeJsonObject->GetArrayField(TEXT("EnumToPoseIndex"));

	TArray<FName> VisibleEnumEntries;

    for (int32 i = 0; i < EnumToPoseIndexArray.Num(); i++) {
        const int32 Value = static_cast<int32>(EnumToPoseIndexArray[i]->AsNumber());

        if (Value != 0) {
            if (BoundEnum->NumEnums() > i) {
                FString EnumEntryName = BoundEnum->GetNameByIndex(i).ToString();
                if (EnumEntryName.Contains("::")) {
                    EnumEntryName.Split("::", nullptr, &EnumEntryName);
                }

                VisibleEnumEntries.Add(FName(*EnumEntryName));
            }
        }
    }

    if (const FArrayProperty* VisEnumArrayProp = FindFProperty<FArrayProperty>(BlendListByEnum->GetClass(), TEXT("VisibleEnumEntries"))) {
        const void* ArrayPtr = VisEnumArrayProp->ContainerPtrToValuePtr<void>(BlendListByEnum);
        FScriptArrayHelper ArrayHelper(VisEnumArrayProp, ArrayPtr);

        ArrayHelper.Resize(0);

        const FNameProperty* NameProp = CastField<FNameProperty>(VisEnumArrayProp->Inner);
        if (!NameProp) {
            return;
        }

        for (const FName& Entry : VisibleEnumEntries) {
            const int32 NewIdx = ArrayHelper.AddValue();
            void* ElemPtr = ArrayHelper.GetRawPtr(NewIdx);

            NameProp->SetPropertyValue(ElemPtr, Entry);
        }
    }

	BlendListByEnum->ReconstructNode();

	TArray<TSharedPtr<FJsonValue>> BlendPoseArray = NodeJsonObject->GetArrayField(TEXT("BlendPose"));

	int BlendPoseIndex = 0;
	if (BlendPoseArray.IsValidIndex(0)) {
		FString LinkID = BlendPoseArray[0]->AsObject()->GetStringField(TEXT("LinkID"));
		const FString IndexedPinName = FString::Printf(TEXT("BlendPose_%d"), 0);

		FUObjectExport* TargetNodeExport = Container->Find(LinkID);
		UAnimGraphNode_Base* TargetNode = Cast<UAnimGraphNode_Base>(TargetNodeExport->Object);

		LinkPoseInputPin(IndexedPinName, BlendListByEnum, TargetNode, AnimGraph);

		BlendPoseIndex++;
	}

	for (int32 EnumIndex = 0; EnumIndex < BoundEnum->NumEnums() - 1; ++EnumIndex) {
		if (EnumIndex < EnumToPoseIndexArray.Num()) {
			const int PoseIndex = EnumToPoseIndexArray[EnumIndex]->AsNumber();

			if (PoseIndex == 0) continue;

			if (PoseIndex < BlendPoseArray.Num()) {
				FString LinkID = BlendPoseArray[PoseIndex]->AsObject()->GetStringField(TEXT("LinkID"));
                const FString IndexedPinName = FString::Printf(TEXT("BlendPose_%d"), BlendPoseIndex);

				FUObjectExport* TargetNodeExport = Container->Find(LinkID);
				UAnimGraphNode_Base* TargetNode = Cast<UAnimGraphNode_Base>(TargetNodeExport->Object);

				LinkPoseInputPin(IndexedPinName, BlendListByEnum, TargetNode, AnimGraph);
			}
		}

		BlendPoseIndex++;
	}
}

void IAnimationBlueprintImporter::CreateAnimGraphNodes(UEdGraph* AnimGraph, const TSharedPtr<FJsonObject>& AnimNodeProperties, FUObjectExportContainer& OutContainer) {
	for (const auto& Pair : AnimNodeProperties->Values) {
		FString Key = JsonKeyToString(Pair.Key);

		TSharedPtr<FJsonObject> Value = Pair.Value->AsObject();

		FString NodeType, NodeStringGUID; {
			Key.Split(TEXT("_"), &NodeType, &NodeStringGUID, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

			if (Key.Contains("[")) {
				FString CleanKey = Key.Left(Key.Find("["));

				TArray<FString> Parts; {
					CleanKey.ParseIntoArray(Parts, TEXT("_"));
				}

				NodeType = Parts.Num() >= 2 ? Parts[0] + TEXT("_") + Parts[1] : CleanKey;
				NodeStringGUID.Empty();
			}
		}

		if (NodeType == "AnimGraphNode") {
			NodeType = Key;
		}

		if (NodeType == "AnimGraphNode_SubInput") {
			NodeType = "AnimGraphNode_LinkedInputPose";
		}

		if (NodeType == "AnimGraphNode_TransitionResult") {
			OutContainer.Exports.Add(
				new FUObjectExport(
					FName(*Key),
					FName(*NodeType),
					StringToName(AnimGraph->GetName()),
					Value,
					nullptr,
					nullptr
				)
			);

			continue;
		}

		FGuid NodeGuid; {
			FGuid::Parse(NodeStringGUID, NodeGuid);

			if (!NodeGuid.IsValid()) NodeGuid = FGuid::NewGuid();
		}

		const UClass* Class = FindClassByType(NodeType);

		if (!Class) {
			UE_LOG(LogReflection, Warning, TEXT("\"%s\" is a %s, which this build has no node for"), *Key, *NodeType);

			continue;
		}

		UAnimGraphNode_Base* Node = NewObject<UAnimGraphNode_Base>(AnimGraph, ToNewObjectClass(Class), NAME_None, RF_Transactional);
		Node->NodeGuid = NodeGuid;

		OutContainer.Exports.Add(
			new FUObjectExport(
				FName(*Key),
				FName(*NodeType),
				StringToName(AnimGraph->GetName()),
				Value,
				Node,
				AnimGraph
			)
		);
	}
}

void IAnimationBlueprintImporter::AddNodesToGraph(UEdGraph* AnimGraph, FUObjectExportContainer* Container) {
    for (const FUObjectExport* Export : Container->Exports) {
        if (!IsValid(Export->Object) || !Export->JsonObject.IsValid()) {
            continue;
        }

        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(Export->Object);

        Node->Rename(nullptr, AnimGraph);
        AnimGraph->Nodes.Add(Node);
        Node->Modify();
    }
}

void IAnimationBlueprintImporter::HandleNodeDeserialization(FUObjectExportContainer* Container) {
	GetObjectSerializer()->GetPropertySerializer()->BlacklistedPropertyNames.Add(TEXT("LinkID"));

	for (FUObjectExport* NodeExport : Container->Exports) {
		if (NodeExport->Object == nullptr) continue;

		UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(NodeExport->Object);
		TSharedPtr<FJsonObject> NodeProperties = NodeExport->JsonObject;

		if (NodeProperties->HasField(TEXT("GroupRole")) && NodeProperties->HasField(TEXT("GroupIndex"))) {
			if (const int32 Which = NodeProperties->GetIntegerField(TEXT("GroupIndex")); SyncGroupNames.IsValidIndex(Which)) {
				const FStructProperty* Inside = GetNodeStructProperty(Node);
				const UScriptStruct* Holds = Inside != nullptr ? Inside->Struct : nullptr;

				if (Holds != nullptr && Holds->FindPropertyByName(TEXT("GroupName")) != nullptr) {
					NodeProperties->SetStringField(TEXT("GroupName"), SyncGroupNames[Which]);

					if (Holds->FindPropertyByName(TEXT("Method")) != nullptr && !NodeProperties->HasField(TEXT("Method"))) {
						NodeProperties->SetStringField(TEXT("Method"), TEXT("EAnimSyncMethod::SyncGroup"));
					}
				} else {
					TSharedPtr<FJsonObject> SyncGroup = MakeShared<FJsonObject>();

					SyncGroup->SetStringField(TEXT("GroupName"), SyncGroupNames[Which]);
					SyncGroup->SetStringField(TEXT("GroupRole"), NodeProperties->GetStringField(TEXT("GroupRole")));

					NodeProperties->SetObjectField(TEXT("SyncGroup"), SyncGroup);
				}
			}
		}

#if ENGINE_UE4
		if (NodeProperties->HasField(TEXT("PhysicsBodyDefinitions"))) {
			TSharedPtr<FJsonObject> PhysicsBodyDefinition = NodeProperties->GetArrayField(TEXT("PhysicsBodyDefinitions"))[0]->AsObject();
			if (PhysicsBodyDefinition.IsValid()) {
				for (const auto& Pair : PhysicsBodyDefinition->Values) {
					NodeExport->JsonObject->SetField(Pair.Key, Pair.Value);
				}
			}
		}
#else
		if (NodeExport->HasProperty("BoxExtents")) {
			TSharedPtr<FJsonObject> PhysicsBodyDefinition = MakeShared<FJsonObject>();
			auto& RootValues = NodeExport->JsonObject->Values;

			auto MoveField = [&](const FString& Key) {
				if (RootValues.Contains(StringToJsonKey(Key))) {
					if (Key == TEXT("LocalJointOffset")) {
						TSharedPtr<FJsonObject> Original = RootValues[StringToJsonKey(Key)]->AsObject();

						if (Original.IsValid()) {
							TSharedPtr<FJsonObject> VecObj = MakeShared<FJsonObject>(*Original);

							for (const auto& Pair : Original->Values) {
								if (Pair.Value->Type == EJson::Number && Pair.Value->AsNumber() != 0.0) {
									VecObj->SetNumberField(Pair.Key, -Pair.Value->AsNumber());
								}
								else {
									VecObj->SetField(Pair.Key, Pair.Value);
								}
							}

							PhysicsBodyDefinition->SetObjectField(Key, VecObj);
							return;
						}
					}

					PhysicsBodyDefinition->SetField(Key, RootValues[StringToJsonKey(Key)]);
				}
			};

			MoveField(TEXT("BoundBone"));
			MoveField(TEXT("BoxExtents"));
			MoveField(TEXT("LocalJointOffset"));
			MoveField(TEXT("ConstraintSetup"));
			MoveField(TEXT("CollisionType"));
			MoveField(TEXT("SphereCollisionRadius"));

			TArray<TSharedPtr<FJsonValue>> PhysicsBodyDefinitionsArray;
			PhysicsBodyDefinitionsArray.Add(MakeShared<FJsonValueObject>(PhysicsBodyDefinition));

			NodeExport->JsonObject->SetArrayField(TEXT("PhysicsBodyDefinitions"), PhysicsBodyDefinitionsArray);
		}
#endif

#if ENGINE_UE4
		if (GReflectionRuntime.IsUE5OrNewer()) {
			if (NodeProperties->HasField(TEXT("LocalJointOffset"))) {
				auto LocalJointOffset = NodeProperties->GetObjectField(TEXT("LocalJointOffset"));
				LocalJointOffset->SetNumberField("X", -LocalJointOffset->GetNumberField(TEXT("X")));
				LocalJointOffset->SetNumberField("Y", -LocalJointOffset->GetNumberField(TEXT("Y")));
				LocalJointOffset->SetNumberField("Z", -LocalJointOffset->GetNumberField(TEXT("Z")));
			}
		}
#endif

		TArray<FString> Handing;

#if !UE4_22_BELOW
		if (Cast<UAnimGraphNode_CustomProperty>(Node) != nullptr) {
			if (const TArray<TSharedPtr<FJsonValue>>* Listed = nullptr; NodeProperties->TryGetArrayField(TEXT("DestPropertyNames"), Listed)) {
				for (const TSharedPtr<FJsonValue>& One : *Listed) {
					if (One.IsValid()) Handing.Add(One->AsString());
				}
			}

			NodeProperties->RemoveField(TEXT("SourcePropertyNames"));
			NodeProperties->RemoveField(TEXT("DestPropertyNames"));
		}
#endif

		for (const TPair<FString, FString>& Says : {
			TPair<FString, FString>(TEXT("ControlRigAssetReference"), TEXT("ControlRigClass")),
			TPair<FString, FString>(TEXT("DefaultControlRigAssetReference"), TEXT("DefaultControlRigClass"))
		}) {
			if (!NodeProperties->HasField(Says.Key)) continue;

			const FStructProperty* Inside = GetNodeStructProperty(Node);

			if (Inside == nullptr || Inside->Struct == nullptr) continue;
			if (Inside->Struct->FindPropertyByName(FName(*Says.Key)) != nullptr) continue;
			if (Inside->Struct->FindPropertyByName(FName(*Says.Value)) == nullptr) continue;

			if (const TSharedPtr<FJsonObject>* Held = nullptr; NodeProperties->TryGetObjectField(Says.Key, Held)) {
				if (const TSharedPtr<FJsonObject>* Named = nullptr; (*Held)->TryGetObjectField(TEXT("BlueprintRigClass"), Named)) {
					NodeProperties->SetObjectField(Says.Value, *Named);

					UE_LOG(LogReflection, Display, TEXT("\"%s\" names its rig through %s, which this build reads as %s"),
						*NodeExport->GetName().ToString(), *Says.Key, *Says.Value);
				}
			}

			NodeProperties->RemoveField(Says.Key);
		}

		GetObjectSerializer()->DeserializeObjectProperties(NodeProperties, Node);

		if (Handing.Num() > 0) {
			ExposeHandedOver(Node, Handing);
		}

		if (UAnimGraphNode_SaveCachedPose* SaveCachedPose = Cast<UAnimGraphNode_SaveCachedPose>(Node)) {
			SaveCachedPose->CacheName = NodeProperties->GetStringField(TEXT("CachePoseName"));
		}

		if (UAnimGraphNode_UseCachedPose* UseCachedPose = Cast<UAnimGraphNode_UseCachedPose>(Node)) {
			if (FString Cached; NodeProperties->TryGetStringField(TEXT("CachePoseName"), Cached)) {
				if (FStrProperty* Under = FindFProperty<FStrProperty>(UAnimGraphNode_UseCachedPose::StaticClass(), TEXT("NameOfCache"))) {
					Under->SetPropertyValue_InContainer(UseCachedPose, Cached);
				}
			}

			if (NodeProperties->HasField(TEXT("LinkToCachingNode"))) {
				const TSharedPtr<FJsonObject> LinkToCachingNode = NodeProperties->GetObjectField(TEXT("LinkToCachingNode"));

				if (LinkToCachingNode->HasField(TEXT("LinkID"))) {
					Borrowed.Add(TPair<TWeakObjectPtr<UAnimGraphNode_UseCachedPose>, FString>(UseCachedPose, LinkToCachingNode->GetStringField(TEXT("LinkID"))));
				}
			}
		}

		HandlePropertyBinding(NodeExport, GetContainer()->JsonObjects, Node, this, AnimBlueprint);

		ApplyBindings(NodeExport->GetName().ToString(), Node);

		if (const UAnimGraphNode_BlendListByEnum* ByEnum = Cast<UAnimGraphNode_BlendListByEnum>(Node); ByEnum != nullptr && ByEnum->GetEnum() == nullptr) {
			if (UEdGraph* Into = Node->GetGraph()) {
				UAnimGraphNode_BlendListByInt* ByNumber = NewObject<UAnimGraphNode_BlendListByInt>(Into);

				Into->AddNode(ByNumber, false, false);

				ByNumber->CreateNewGuid();

				GetObjectSerializer()->DeserializeObjectProperties(NodeProperties, ByNumber);

				FString Said = TEXT("was blended by an enum this build has not got");

				if (FString Named; NodeProperties->TryGetStringField(TEXT("BoundEnum"), Named) && !Named.IsEmpty()) {
					Said = FString::Printf(TEXT("was blended by \"%s\", which this build has not got"), *Named);
				}

				ByNumber->NodeComment = Said;
				ByNumber->bCommentBubbleVisible = true;

				Into->RemoveNode(Node);

				NodeExport->Object = ByNumber;

				Node = ByNumber;

				UE_LOG(LogReflection, Warning, TEXT("\"%s\" %s, so it blends by number"), *NodeExport->GetName().ToString(), *Said);
			}
		}

		const UReflectionSettings* Settings = GetSettings();
		if (Settings->AssetSettings.AnimationBlueprint.NodeIDComments) {
			Node->NodeComment = NodeExport->GetName().ToString();
			Node->bCommentBubbleVisible = true;
		}

		Node->AllocateDefaultPins();
		Node->Modify();

		int32 Poses = INDEX_NONE;

		if (const TArray<TSharedPtr<FJsonValue>>* Listed = nullptr; NodeProperties->TryGetArrayField(TEXT("BlendPose"), Listed)) {
			Poses = Listed->Num();
		}

		Node->PostPlacedNewNode();

		if (Poses != INDEX_NONE) {
			if (const FStructProperty* Inside = GetNodeStructProperty(Node); Inside != nullptr && Inside->Struct != nullptr) {
				void* Held = Inside->ContainerPtrToValuePtr<void>(Node);

				bool bTrimmed = false;

				for (const TCHAR* Named : { TEXT("BlendPose"), TEXT("BlendTime") }) {
					const FArrayProperty* Listed = CastField<FArrayProperty>(Inside->Struct->FindPropertyByName(Named));

					if (Listed == nullptr) continue;

					FScriptArrayHelper Holds(Listed, Listed->ContainerPtrToValuePtr<void>(Held));

					if (Holds.Num() <= Poses) continue;

					Holds.Resize(Poses);

					bTrimmed = true;
				}

				if (bTrimmed) {
					Node->ReconstructNode();

					for (int32 At = Node->Pins.Num() - 1; At >= 0; --At) {
						UEdGraphPin* Pin = Node->Pins[At];

						if (Pin == nullptr || !Pin->bOrphanedPin) continue;

						Pin->BreakAllPinLinks();

						Node->RemovePin(Pin);
					}
				}
			}
		}
	}
}

void IAnimationBlueprintImporter::ConnectAnimGraphNodes(FUObjectExportContainer* Container, UEdGraph* AnimGraph) {
    for (FUObjectExport* Export : Container->Exports) {
        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(Export->Object);
        const TSharedPtr<FJsonObject> Json = Export->JsonObject;

        if (Cast<UAnimGraphNode_BlendListByEnum>(Node)) {
            UpdateBlendListByEnumVisibleEntries(Export, Container, AnimGraph);
        	continue;
        }

        for (const auto& Pair : Json->Values) {
            const FString Key = JsonKeyToString(Pair.Key);
            const TSharedPtr<FJsonValue>& Value = Pair.Value;

            if (Value->Type == EJson::Array) {
                const TArray<TSharedPtr<FJsonValue>>& JsonArray = Value->AsArray();

                for (int32 Index = 0; Index < JsonArray.Num(); ++Index) {
                    const TSharedPtr<FJsonValue>& Elem = JsonArray[Index];

                    if (!Elem.IsValid() || !Elem->AsObject().IsValid()) {
                        continue;
                    }

                    const TSharedPtr<FJsonObject>& Obj = Elem->AsObject();
                    if (!Obj->HasField(TEXT("LinkID"))) {
                        continue;
                    }

                    const FString LinkID = Obj->GetStringField(TEXT("LinkID"));
                    UAnimGraphNode_Base* TargetNode = Cast<UAnimGraphNode_Base>(Container->Find(LinkID)->Object);

                    if (!TargetNode) {
                        continue;
                    }

                    const FStructProperty* NodeProp = GetNodeStructProperty(Node);
                    if (!NodeProp) {
                        continue;
                    }

                    for (TFieldIterator<FProperty> It(NodeProp->Struct); It; ++It) {
                        FProperty* Property = *It;

                        if (Property->GetName() != JsonKeyToString(Pair.Key)) {
                            continue;
                        }

                        if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property)) {
                            const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner);

                            if (!InnerStruct || !InnerStruct->Struct->IsChildOf(FPoseLinkBase::StaticStruct())) {
                                continue;
                            }

                            const FString IndexedPinName = FString::Printf(TEXT("%s_%d"), *Pair.Key, Index);
                            LinkPoseInputPin(IndexedPinName, Node, TargetNode, AnimGraph);
                        }
                    }
                }
            }

            if (Value->Type == EJson::Object && Value->AsObject()->HasTypedField<EJson::String>(TEXT("LinkID"))) {
                if (Key == TEXT("LinkToCachingNode")) {
                    continue;
                }

                const FString LinkID = Value->AsObject()->GetStringField(TEXT("LinkID"));
                UAnimGraphNode_Base* TargetNode = Cast<UAnimGraphNode_Base>(Container->Find(LinkID)->Object);

                if (!TargetNode) {
                    UE_LOG(LogReflection, Warning, TEXT("\"%s\" feeds %s.%s, and is not in the same graph as it"), *LinkID, *Export->GetName().ToString(), *Key);

                    continue;
                }

                const FStructProperty* NodeProp = GetNodeStructProperty(Node);

                if (!NodeProp) {
                    continue;
                }

                for (TFieldIterator<FProperty> It(NodeProp->Struct); It; ++It) {
                    const FProperty* Property = *It;

                    if (Property->GetName() != JsonKeyToString(Pair.Key)) {
                        continue;
                    }

                    LinkPoseInputPin(Key, Node, TargetNode, AnimGraph);
                }
            }
        }
    }
}

void IAnimationBlueprintImporter::ProcessEvaluateGraphExposedInputs(const TSharedPtr<FJsonObject>& AnimNodeProperties) const {
	if (!GetAssetData()->HasField(TEXT("EvaluateGraphExposedInputs"))) return;
	TArray<TSharedPtr<FJsonValue>> EvaluateInputs = GetAssetData()->GetArrayField(TEXT("EvaluateGraphExposedInputs"));

	for (const auto& Value : EvaluateInputs) {
		TSharedPtr<FJsonObject> InputObj = Value->AsObject();

		FString NodeName = InputObj->GetObjectField(TEXT("ValueHandlerNodeProperty"))->GetStringField(TEXT("ObjectName")); {
			NodeName.Split(":", nullptr, &NodeName);
			NodeName = NodeName.Replace(TEXT("'"), TEXT(""));	
		}

		AnimNodeProperties->GetObjectField(NodeName)->SetObjectField(TEXT("EvaluateGraphExposedInputs"), InputObj);
	}
}

void IAnimationBlueprintImporter::SettleInputPoses(UEdGraph* Layer) const {
	if (Layer == nullptr || AnimBlueprint == nullptr) return;

	TArray<UAnimGraphNode_LinkedInputPose*> Taking;
	Layer->GetNodesOfClass<UAnimGraphNode_LinkedInputPose>(Taking);

	if (Taking.Num() == 0) return;

	UClass* Declaring = nullptr;
	UFunction* Signature = nullptr;

	for (const FBPInterfaceDescription& Implemented : AnimBlueprint->ImplementedInterfaces) {
		UClass* Interface = Implemented.Interface;

		if (Interface == nullptr) continue;

		if (UFunction* Found = Interface->FindFunctionByName(Layer->GetFName())) {
			Declaring = Interface;
			Signature = Found;

			break;
		}
	}

	if (Signature == nullptr) return;

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	int32 Which = 0;

	for (TFieldIterator<FProperty> It(Signature); It && (It->PropertyFlags & CPF_Parm); ++It) {
		FProperty* Parameter = *It;

		if (Parameter->HasAnyPropertyFlags(CPF_OutParm) && !Parameter->HasAnyPropertyFlags(CPF_ReferenceParm)) continue;

		FEdGraphPinType Type;

		if (!Schema->ConvertPropertyToPinType(Parameter, Type)) continue;

		if (!UAnimationGraphSchema::IsPosePin(Type)) continue;

#if !UE4_23_BELOW
		for (UAnimGraphNode_LinkedInputPose* One : Taking) {
			if (One->Node.Name != Parameter->GetFName()) continue;

			One->InputPoseIndex = Which;

			One->FunctionReference.SetExternalMember(Layer->GetFName(), Declaring);
			One->Modify();
		}
#endif

		Which++;
	}
}

void IAnimationBlueprintImporter::SettleCachedPoses() {
	for (const TPair<TWeakObjectPtr<UAnimGraphNode_UseCachedPose>, FString>& One : Borrowed) {
		UAnimGraphNode_UseCachedPose* Borrowing = One.Key.Get();

		if (Borrowing == nullptr) continue;

		UAnimGraphNode_SaveCachedPose* Saved = nullptr;

		for (FUObjectExportContainer* Held : AnimNodeContainers) {
			if (Held == nullptr) continue;

			const FUObjectExport* Export = Held->Find(One.Value);

			if (!Export->IsJsonAndObjectValid()) continue;

			Saved = Cast<UAnimGraphNode_SaveCachedPose>(Export->Object);

			if (Saved != nullptr) break;
		}

		if (Saved != nullptr && (!IsValid(Saved) || Saved->Pins.Num() == 0 || Saved->GetGraph() == nullptr)) {
			Saved = nullptr;
		}

		if (Saved == nullptr) {
			FImportIssues::Report(
				EImportIssue::Data,
				TEXT("A borrowed pose that nothing saved"),
				FString::Printf(TEXT("'%s' borrows the cached pose '%s', and there is no node of that name still standing to have saved one."), *GetAssetName(), *One.Value)
			);

			continue;
		}

		Borrowing->SaveCachedPoseNode = Saved;
		Borrowing->Modify();
		Saved->Modify();
	}

	Borrowed.Empty();
}

UEdGraph* IAnimationBlueprintImporter::FindAnimGraph(UAnimBlueprint* AnimBlueprint) {
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs) {
		if (Graph && Graph->GetName() == TEXT("AnimGraph")) {
			return Graph;
		}
	}

	return nullptr;
}
