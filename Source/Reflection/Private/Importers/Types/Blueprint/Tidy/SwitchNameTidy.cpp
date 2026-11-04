#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/GraphTidy.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_SwitchName.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionTidySwitchName, All, All);

namespace {
	struct FNameRung {
		UK2Node_IfThenElse* Asks = nullptr;
		UK2Node* Compares = nullptr;
		FName Case;
		TArray<UEdGraphPin*> Matches;
	};

	bool NameSidesOf(UK2Node* Node, UEdGraphPin*& OutFirst, UEdGraphPin*& OutSecond) {
		UK2Node_CallFunction* Calling = Cast<UK2Node_CallFunction>(Node);

		if (Calling == nullptr || !Calling->IsNodePure()) return false;
		if (Calling->FunctionReference.GetMemberName() != TEXT("NotEqual_NameName")) return false;

		OutFirst = Calling->FindPin(TEXT("A"), EGPD_Input);
		OutSecond = Calling->FindPin(TEXT("B"), EGPD_Input);

		return OutFirst != nullptr && OutSecond != nullptr;
	}

	UEdGraphPin* NameFedBy(const UEdGraphPin* Pin) {
		return Pin != nullptr && Pin->LinkedTo.Num() == 1 ? Pin->LinkedTo[0] : nullptr;
	}

	UK2Node_IfThenElse* NextNameRung(const UK2Node_IfThenElse* Asks) {
		UEdGraphPin* Carries = Asks != nullptr ? Asks->GetThenPin() : nullptr;

		if (Carries == nullptr || Carries->LinkedTo.Num() != 1) return nullptr;

		return Cast<UK2Node_IfThenElse>(Carries->LinkedTo[0]->GetOwningNode());
	}
}

struct FSwitchNameTidy final : FGraphTidy {
	virtual const TCHAR* GetName() const override { return TEXT("SwitchName"); }

	virtual int32 Apply(UEdGraph* Graph) const override {
		if (Graph == nullptr) return 0;

		int32 Put = 0;

		TArray<UEdGraphNode*> Nodes = Graph->Nodes;

		TSet<UK2Node_IfThenElse*> Spent;

		for (UEdGraphNode* Node : Nodes) {
			UK2Node_IfThenElse* Head = Cast<UK2Node_IfThenElse>(Node);

			if (Head == nullptr || Spent.Contains(Head)) continue;

			UEdGraphPin* Asked = nullptr;

			TArray<FNameRung> Ladder;
			TSet<FName> Already;

			for (UK2Node_IfThenElse* Asks = Head; Asks != nullptr; Asks = NextNameRung(Asks)) {
				if (Spent.Contains(Asks)) break;

				UEdGraphPin* Says = NameFedBy(Asks->GetConditionPin());
				UK2Node* Compares = Says != nullptr ? Cast<UK2Node>(Says->GetOwningNode()) : nullptr;

				UEdGraphPin* First = nullptr;
				UEdGraphPin* Second = nullptr;

				if (!NameSidesOf(Compares, First, Second)) break;

				UEdGraphPin* Over = NameFedBy(First);

				if (Over == nullptr) break;
				if (Asked != nullptr && Over != Asked) break;

				if (Second->LinkedTo.Num() > 0) break;

				const FName Case(*Second->DefaultValue);

				if (Already.Contains(Case)) break;

				UEdGraphPin* Matches = Asks->GetElsePin();

				if (Matches == nullptr) break;

				Asked = Over;

				FNameRung Rung;

				Rung.Asks = Asks;
				Rung.Compares = Compares;
				Rung.Case = Case;
				Rung.Matches = Matches->LinkedTo;

				Ladder.Add(Rung);
				Already.Add(Case);
			}

			if (Ladder.Num() < 2 || Asked == nullptr) continue;

			UEdGraphPin* Falls = Ladder.Last().Asks->GetThenPin();

			UK2Node_SwitchName* Switch = NewObject<UK2Node_SwitchName>(Graph, UK2Node_SwitchName::StaticClass(), NAME_None, RF_Transactional);

			Switch->bHasDefaultPin = true;

			for (const FNameRung& Rung : Ladder) {
				Switch->PinNames.Add(Rung.Case);
			}

			Graph->AddNode(Switch, false, false);

			Switch->CreateNewGuid();
			Switch->PostPlacedNewNode();

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

			for (const FNameRung& Rung : Ladder) {
				UEdGraphPin* Way = Switch->FindPin(Rung.Case, EGPD_Output);

				if (Way == nullptr || Way->LinkedTo.Num() > 0) continue;

				for (UEdGraphPin* To : Rung.Matches) Way->MakeLinkTo(To);

				Wired++;
			}

			if (UEdGraphPin* Otherwise = Switch->GetDefaultPin()) {
				for (UEdGraphPin* To : Falls->LinkedTo) {
					if (To->GetOwningNode() != Ladder.Last().Asks) Otherwise->MakeLinkTo(To);
				}
			}

			if (Wired != Ladder.Num()) {
				Graph->RemoveNode(Switch);

				continue;
			}

			const int32 HeadAt = Graph->Nodes.IndexOfByKey(Head);

			if (HeadAt != INDEX_NONE && Graph->Nodes.Remove(Switch) > 0) {
				Graph->Nodes.Insert(Switch, HeadAt);
			}

			for (const FNameRung& Rung : Ladder) {
				Spent.Add(Rung.Asks);

				Graph->RemoveNode(Rung.Asks);

				if (Rung.Compares != nullptr) Graph->RemoveNode(Rung.Compares);
			}

			Put++;
		}

		if (Put > 0) {
			UE_LOG(LogReflectionTidySwitchName, Display, TEXT("\"%s\" had %d name switch(es) written as a ladder of comparisons"), *Graph->GetName(), Put);
		}

		return Put;
	}
};

REGISTER_TIDY(FSwitchNameTidy)
