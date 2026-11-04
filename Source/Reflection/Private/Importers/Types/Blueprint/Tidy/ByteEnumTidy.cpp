#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/GraphTidy.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_EnumInequality.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionTidyByteEnum, All, All);

namespace {
	UEnum* EnumWiredInto(const UK2Node* Node) {
		if (Node == nullptr) return nullptr;

		for (const UEdGraphPin* Pin : Node->Pins) {
			if (Pin == nullptr || Pin->Direction != EGPD_Input) continue;
			if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Byte) continue;

			for (const UEdGraphPin* Linked : Pin->LinkedTo) {
				if (Linked == nullptr) continue;

				if (UEnum* Held = Cast<UEnum>(Linked->PinType.PinSubCategoryObject.Get())) return Held;
			}
		}

		return nullptr;
	}

	bool CompilesTo(const UClass* Kind, const FName Called, const UClass* Upon) {
		const UK2Node_EnumEquality* Asking = Kind != nullptr ? Cast<UK2Node_EnumEquality>(const_cast<UClass*>(Kind)->GetDefaultObject()) : nullptr;

		if (Asking == nullptr) return false;

		FName Named;
		UClass* Owner = nullptr;

		Asking->GetConditionalFunction(Named, &Owner);

		return Named == Called && (Upon == nullptr || Owner == nullptr || Owner == Upon);
	}

	void RewireByteEnum(UEdGraphPin* From, UEdGraphPin* To) {
		if (From == nullptr || To == nullptr) return;

		for (UEdGraphPin* Held : From->LinkedTo) {
			if (Held != nullptr) To->MakeLinkTo(Held);
		}
	}
}

struct FByteEnumTidy final : FGraphTidy {
	virtual const TCHAR* GetName() const override { return TEXT("ByteEnum"); }

	virtual int32 Apply(UEdGraph* Graph) const override {
		if (Graph == nullptr) return 0;

		int32 Named = 0;

		TArray<UEdGraphNode*> Nodes = Graph->Nodes;

		for (UEdGraphNode* Node : Nodes) {
			UK2Node_CallFunction* Calling = Cast<UK2Node_CallFunction>(Node);

			if (Calling == nullptr || !Calling->IsNodePure()) continue;

			UEnum* Enum = EnumWiredInto(Calling);

			if (Enum == nullptr) continue;

			const FName Called = Calling->FunctionReference.GetMemberName();
			const UClass* Upon = Calling->FunctionReference.GetMemberParentClass();

			UClass* Kind = nullptr;

			if (CompilesTo(UK2Node_EnumEquality::StaticClass(), Called, Upon)) Kind = UK2Node_EnumEquality::StaticClass();
			else if (CompilesTo(UK2Node_EnumInequality::StaticClass(), Called, Upon)) Kind = UK2Node_EnumInequality::StaticClass();

			if (Kind == nullptr) continue;

			UEdGraphPin* First = Calling->FindPin(TEXT("A"), EGPD_Input);
			UEdGraphPin* Second = Calling->FindPin(TEXT("B"), EGPD_Input);
			UEdGraphPin* Answers = Calling->GetReturnValuePin();

			if (First == nullptr || Second == nullptr || Answers == nullptr) continue;

			UK2Node_EnumEquality* Made = NewObject<UK2Node_EnumEquality>(Graph, Kind, NAME_None, RF_Transactional);

			Graph->AddNode(Made, false, false);

			Made->CreateNewGuid();
			Made->PostPlacedNewNode();
			Made->AllocateDefaultPins();

			Made->NodePosX = Calling->NodePosX;
			Made->NodePosY = Calling->NodePosY;

			UEdGraphPin* Takes = Made->GetInput1Pin();
			UEdGraphPin* Against = Made->GetInput2Pin();
			UEdGraphPin* Gives = Made->GetReturnValuePin();

			if (Takes == nullptr || Against == nullptr || Gives == nullptr) {
				Graph->RemoveNode(Made);

				continue;
			}

			RewireByteEnum(First, Takes);
			RewireByteEnum(Second, Against);
			RewireByteEnum(Answers, Gives);

			Made->NotifyPinConnectionListChanged(Takes);
			Made->NotifyPinConnectionListChanged(Against);

			for (const TPair<UEdGraphPin*, UEdGraphPin*>& Pair : { TPair<UEdGraphPin*, UEdGraphPin*>(First, Takes), TPair<UEdGraphPin*, UEdGraphPin*>(Second, Against) }) {
				if (Pair.Key->LinkedTo.Num() > 0 || Pair.Key->DefaultValue.IsEmpty() || !Pair.Key->DefaultValue.IsNumeric()) continue;

				const FString Says = Enum->GetNameStringByValue(FCString::Atoi64(*Pair.Key->DefaultValue));

				if (Says.IsEmpty()) continue;

				Pair.Value->DefaultValue = Says;
			}

			First->BreakAllPinLinks();
			Second->BreakAllPinLinks();
			Answers->BreakAllPinLinks();

			Graph->RemoveNode(Calling);

			Named++;
		}

		if (Named > 0) {
			UE_LOG(LogReflectionTidyByteEnum, Display, TEXT("\"%s\" had %d enum(s) compared as two bytes where the editor draws a node of its own"), *Graph->GetName(), Named);
		}

		return Named;
	}
};

REGISTER_TIDY(FByteEnumTidy)
