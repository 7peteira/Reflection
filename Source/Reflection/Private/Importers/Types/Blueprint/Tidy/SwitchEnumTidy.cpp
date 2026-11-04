#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/GraphTidy.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_EnumInequality.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_VariableGet.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionTidySwitchEnum, All, All);

namespace {
	struct FRung {
		UK2Node_IfThenElse* Asks = nullptr;
		UK2Node* Compares = nullptr;
		int32 Index = INDEX_NONE;
		TArray<UEdGraphPin*> Matches;
	};

	bool SidesOf(UK2Node* Node, UEdGraphPin*& OutFirst, UEdGraphPin*& OutSecond) {
		if (Node == nullptr || !Node->IsNodePure()) return false;

		if (UK2Node_EnumInequality* Against = Cast<UK2Node_EnumInequality>(Node)) {
			OutFirst = Against->GetInput1Pin();
			OutSecond = Against->GetInput2Pin();

			return OutFirst != nullptr && OutSecond != nullptr;
		}

		UK2Node_CallFunction* Calling = Cast<UK2Node_CallFunction>(Node);

		if (Calling == nullptr || Calling->FunctionReference.GetMemberName() != TEXT("NotEqual_ByteByte")) return false;

		OutFirst = Calling->FindPin(TEXT("A"), EGPD_Input);
		OutSecond = Calling->FindPin(TEXT("B"), EGPD_Input);

		return OutFirst != nullptr && OutSecond != nullptr;
	}

	UEdGraphPin* FedBy(const UEdGraphPin* Pin) {
		return Pin != nullptr && Pin->LinkedTo.Num() == 1 ? Pin->LinkedTo[0] : nullptr;
	}

	bool SameValue(const UEdGraphPin* One, const UEdGraphPin* Other) {
		if (One == Other) return true;
		if (One == nullptr || Other == nullptr || One->PinName != Other->PinName) return false;

		const UK2Node_VariableGet* OneGet = Cast<UK2Node_VariableGet>(One->GetOwningNode());
		const UK2Node_VariableGet* OtherGet = Cast<UK2Node_VariableGet>(Other->GetOwningNode());

		if (OneGet == nullptr || OtherGet == nullptr) return false;
		if (OneGet->VariableReference.GetMemberName() != OtherGet->VariableReference.GetMemberName()) return false;

		const UEdGraphPin* OneSelf = OneGet->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
		const UEdGraphPin* OtherSelf = OtherGet->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);

		const UEdGraphPin* OneOn = OneSelf != nullptr ? FedBy(OneSelf) : nullptr;
		const UEdGraphPin* OtherOn = OtherSelf != nullptr ? FedBy(OtherSelf) : nullptr;

		return (OneOn == nullptr && OtherOn == nullptr) || (OneOn != nullptr && OtherOn != nullptr && SameValue(OneOn, OtherOn));
	}

	int32 EntryOf(const UEnum* Enum, const UEdGraphPin* Pin) {
		if (Enum == nullptr || Pin == nullptr || Pin->LinkedTo.Num() > 0 || Pin->DefaultValue.IsEmpty()) return INDEX_NONE;

		if (Pin->DefaultValue.IsNumeric()) return Enum->GetIndexByValue(FCString::Atoi64(*Pin->DefaultValue));

		return Enum->GetIndexByNameString(Pin->DefaultValue);
	}

	UK2Node_IfThenElse* NextRung(const UK2Node_IfThenElse* Asks) {
		UEdGraphPin* Carries = Asks != nullptr ? Asks->GetThenPin() : nullptr;

		if (Carries == nullptr || Carries->LinkedTo.Num() != 1) return nullptr;

		return Cast<UK2Node_IfThenElse>(Carries->LinkedTo[0]->GetOwningNode());
	}
}

struct FSwitchEnumTidy final : FGraphTidy {
	virtual const TCHAR* GetName() const override { return TEXT("SwitchEnum"); }

	virtual int32 Apply(UEdGraph* Graph) const override {
		if (Graph == nullptr) return 0;

		int32 Put = 0;

		TArray<UEdGraphNode*> Nodes = Graph->Nodes;

		TSet<UK2Node_IfThenElse*> Spent;

		for (UEdGraphNode* Node : Nodes) {
			UK2Node_IfThenElse* Head = Cast<UK2Node_IfThenElse>(Node);

			if (Head == nullptr || Spent.Contains(Head)) continue;

			UEnum* Enum = nullptr;
			UEdGraphPin* Asked = nullptr;

			TArray<FRung> Ladder;

			TSet<int32> Already;

			for (UK2Node_IfThenElse* Asks = Head; Asks != nullptr; Asks = NextRung(Asks)) {
				if (Spent.Contains(Asks)) break;

				UEdGraphPin* Says = FedBy(Asks->GetConditionPin());

				UK2Node* Compares = Says != nullptr ? Cast<UK2Node>(Says->GetOwningNode()) : nullptr;

				UEdGraphPin* First = nullptr;
				UEdGraphPin* Second = nullptr;

				if (!SidesOf(Compares, First, Second)) break;

				UEdGraphPin* Over = FedBy(First);

				if (Over == nullptr) break;
				if (Asked != nullptr && !SameValue(Over, Asked)) break;

				UEnum* Held = Cast<UEnum>(Over->PinType.PinSubCategoryObject.Get());

				if (Held == nullptr || (Enum != nullptr && Held != Enum)) break;

				const int32 Entry = EntryOf(Held, Second);

				if (Entry == INDEX_NONE) break;

				if (Already.Contains(Entry)) break;

				UEdGraphPin* Matches = Asks->GetElsePin();

				if (Matches == nullptr) break;

				Enum = Held;
				Asked = Over;

				FRung Rung;

				Rung.Asks = Asks;
				Rung.Compares = Compares;
				Rung.Index = Entry;
				Rung.Matches = Matches->LinkedTo;

				Ladder.Add(Rung);

				Already.Add(Entry);
			}

			if (Ladder.Num() < 2 || Enum == nullptr || Asked == nullptr) continue;

			UEdGraphPin* Falls = Ladder.Last().Asks->GetThenPin();

			UK2Node_SwitchEnum* Switch = NewObject<UK2Node_SwitchEnum>(Graph, UK2Node_SwitchEnum::StaticClass(), NAME_None, RF_Transactional);

			Switch->bHasDefaultPin = Falls != nullptr && Falls->LinkedTo.Num() > 0;

			Graph->AddNode(Switch, false, false);

			Switch->CreateNewGuid();
			Switch->PostPlacedNewNode();

			Switch->Enum = Enum;
			Switch->EnumEntries.Empty();
			Switch->EnumFriendlyNames.Empty();

			for (int32 Entry = 0; Entry < Enum->NumEnums() - 1; ++Entry) {
				if (Enum->HasMetaData(TEXT("Hidden"), Entry) || Enum->HasMetaData(TEXT("Spacer"), Entry)) continue;

				Switch->EnumEntries.Add(FName(*Enum->GetNameStringByIndex(Entry)));
				Switch->EnumFriendlyNames.Add(Enum->GetDisplayNameTextByIndex(Entry));
			}

			if (Switch->Pins.Num() == 0) Switch->AllocateDefaultPins();

			Switch->NodePosX = Head->NodePosX;
			Switch->NodePosY = Head->NodePosY;

			UEdGraphPin* Over = Switch->GetSelectionPin();
			UEdGraphPin* In = Switch->GetExecPin();

			if (Over == nullptr || In == nullptr) {
				Graph->RemoveNode(Switch);

				continue;
			}

			Over->MakeLinkTo(Asked);

			if (UEdGraphPin* Entered = Head->GetExecPin()) {
				for (UEdGraphPin* From : Entered->LinkedTo) In->MakeLinkTo(From);
			}

			int32 Wired = 0;

			for (const FRung& Rung : Ladder) {
				UEdGraphPin* Way = Switch->FindPin(FName(*Enum->GetNameStringByIndex(Rung.Index)), EGPD_Output);

				if (Way == nullptr || Way->LinkedTo.Num() > 0) continue;

				for (UEdGraphPin* To : Rung.Matches) Way->MakeLinkTo(To);

				Wired++;
			}

			if (UEdGraphPin* Otherwise = Switch->GetDefaultPin()) {
				for (UEdGraphPin* To : Falls->LinkedTo) {
					if (To->GetOwningNode() != Ladder.Last().Asks) Otherwise->MakeLinkTo(To);
				}
			}

			if (Wired == 0) {
				Graph->RemoveNode(Switch);

				continue;
			}

			for (const FRung& Rung : Ladder) {
				Spent.Add(Rung.Asks);

				Graph->RemoveNode(Rung.Asks);
			}

			Put++;
		}

		if (Put > 0) {
			UE_LOG(LogReflectionTidySwitchEnum, Display, TEXT("\"%s\" had %d switch(es) written as a ladder of comparisons"), *Graph->GetName(), Put);
		}

		return Put;
	}
};

REGISTER_TIDY(FSwitchEnumTidy)
