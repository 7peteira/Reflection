#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/GraphTidy.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionTidyContext, All, All);

namespace {
	UClass* ContextNodeClass() {
		static UClass* Kind = FindObject<UClass>(nullptr, TEXT("/Script/BlueprintContextEditor.K2Node_GetBlueprintContext"));

		return Kind;
	}

	UClass* SubsystemNodeClass() {
		static UClass* Kind = FindObject<UClass>(nullptr, TEXT("/Script/BlueprintGraph.K2Node_GetSubsystem"));

		return Kind;
	}

	bool IsBlueprintContext(const UClass* Class) {
		static UClass* Kind = FindObject<UClass>(nullptr, TEXT("/Script/BlueprintContext.BlueprintContextBase"));

		return Class != nullptr && Kind != nullptr && Class->IsChildOf(Kind);
	}

	bool IsSubsystem(const UClass* Class) {
		static UClass* Kind = FindObject<UClass>(nullptr, TEXT("/Script/Engine.Subsystem"));

		return Class != nullptr && Kind != nullptr && Class->IsChildOf(Kind);
	}

	UEdGraphPin* CastResult(UK2Node_DynamicCast* Cast) {
		for (UEdGraphPin* Pin : Cast->Pins) {
			if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object) return Pin;
		}

		return nullptr;
	}
}

struct FBlueprintContextTidy final : FGraphTidy {
	virtual const TCHAR* GetName() const override { return TEXT("BlueprintContext"); }

	virtual int32 Apply(UEdGraph* Graph) const override {
		if (Graph == nullptr) return 0;

		int32 Put = 0;

		TArray<UEdGraphNode*> Nodes = Graph->Nodes;

		for (UEdGraphNode* Node : Nodes) {
			UK2Node_CallFunction* Calling = Cast<UK2Node_CallFunction>(Node);

			if (Calling == nullptr) continue;

			const UClass* Library = Calling->FunctionReference.GetMemberParentClass();
			const FName Called = Calling->FunctionReference.GetMemberName();

			UClass* Kind = nullptr;

			if (Library != nullptr && Called == TEXT("GetContext") && Library->GetName() == TEXT("BlueprintContextLibrary")) Kind = ContextNodeClass();
			else if (Library != nullptr && Called == TEXT("GetLocalPlayerSubsystem") && Library->GetName() == TEXT("SubsystemBlueprintLibrary")) Kind = SubsystemNodeClass();

			if (Kind == nullptr) continue;

			UEdGraphPin* Gives = Calling->GetReturnValuePin();

			if (Gives == nullptr || Gives->LinkedTo.Num() == 0) continue;

			TArray<UK2Node_DynamicCast*> Casts;

			bool bOnlyCasts = true;

			for (UEdGraphPin* Reader : Gives->LinkedTo) {
				UK2Node_DynamicCast* Held = Reader != nullptr ? Cast<UK2Node_DynamicCast>(Reader->GetOwningNode()) : nullptr;

				if (Held == nullptr || Held->TargetType == nullptr || !Held->IsNodePure() || CastResult(Held) == nullptr) {
					bOnlyCasts = false;

					break;
				}

				UEdGraphPin* Worked = Held->GetBoolSuccessPin();

				if (Worked != nullptr && Worked->LinkedTo.Num() > 0) {
					bOnlyCasts = false;

					break;
				}

				Casts.Add(Held);
			}

			if (!bOnlyCasts || Casts.Num() == 0) continue;

			int32 Stood = 0;

			TMap<UClass*, UEdGraphPin*> Standing;

			for (UK2Node_DynamicCast* Casting : Casts) {
				UClass* Wanted = Casting->TargetType.Get();

				if (UEdGraphPin* const* Already = Standing.Find(Wanted)) {
					if (UEdGraphPin* Reached = CastResult(Casting)) {
						for (UEdGraphPin* Reader : Reached->LinkedTo) {
							(*Already)->MakeLinkTo(Reader);
						}

						Reached->BreakAllPinLinks();

						Graph->RemoveNode(Casting);

						Stood++;
					}

					continue;
				}

				FObjectProperty* Says = FindFProperty<FObjectProperty>(Kind, TEXT("CustomClass"));

				if (Says == nullptr) continue;

				UEdGraphNode* Made = NewObject<UEdGraphNode>(Graph, Kind, NAME_None, RF_Transactional);

				Graph->AddNode(Made, false, false);

				Made->CreateNewGuid();
				Made->PostPlacedNewNode();

				Made->NodePosX = Calling->NodePosX;
				Made->NodePosY = Casting->NodePosY;

				Says->SetObjectPropertyValue_InContainer(Made, Wanted);

				Made->AllocateDefaultPins();

				if (Says->GetObjectPropertyValue_InContainer(Made) != Wanted) {
					Graph->RemoveNode(Made);

					UE_LOG(LogReflectionTidyContext, Warning, TEXT("\"%s\" asks for a \"%s\", which %s will not hold, so it was left as the call it was written as"),
						*Graph->GetName(), *Wanted->GetName(), *Kind->GetName());

					continue;
				}

				UEdGraphPin* Hands = nullptr;

				for (UEdGraphPin* Pin : Made->Pins) {
					if (Pin->Direction == EGPD_Output) { Hands = Pin; break; }
				}

				UEdGraphPin* Reached = CastResult(Casting);

				if (Hands == nullptr || Reached == nullptr) {
					Graph->RemoveNode(Made);

					continue;
				}

				for (UEdGraphPin* Reader : Reached->LinkedTo) {
					Hands->MakeLinkTo(Reader);
				}

				Reached->BreakAllPinLinks();

				Graph->RemoveNode(Casting);

				Standing.Add(Wanted, Hands);

				Stood++;
			}

			if (Stood == Casts.Num()) Graph->RemoveNode(Calling);

			Put += Stood;
		}

		if (Put > 0) {
			UE_LOG(LogReflectionTidyContext, Display, TEXT("\"%s\" had %d context(s) asked for and cast in two nodes where the editor draws one"), *Graph->GetName(), Put);
		}

		return Put;
	}
};

REGISTER_TIDY(FBlueprintContextTidy)
