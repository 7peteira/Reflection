#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/GraphTidy.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_SwitchInteger.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionTidySwitchInteger, All, All);

namespace {
	struct FIntegerRung {
		UK2Node_IfThenElse* Asks = nullptr;
		UK2Node* Compares = nullptr;
		int32 Case = 0;
		TArray<UEdGraphPin*> Matches;
	};

	bool IntegerSidesOf(UK2Node* Node, UEdGraphPin*& OutFirst, UEdGraphPin*& OutSecond) {
		UK2Node_CallFunction* Calling = Cast<UK2Node_CallFunction>(Node);

		if (Calling == nullptr || !Calling->IsNodePure()) return false;
		if (Calling->FunctionReference.GetMemberName() != TEXT("NotEqual_IntInt")) return false;

		OutFirst = Calling->FindPin(TEXT("A"), EGPD_Input);
		OutSecond = Calling->FindPin(TEXT("B"), EGPD_Input);

		return OutFirst != nullptr && OutSecond != nullptr;
	}

	UEdGraphPin* IntegerFedBy(const UEdGraphPin* Pin) {
		return Pin != nullptr && Pin->LinkedTo.Num() == 1 ? Pin->LinkedTo[0] : nullptr;
	}

	UK2Node_IfThenElse* NextIntegerRung(const UK2Node_IfThenElse* Asks) {
		UEdGraphPin* Carries = Asks != nullptr ? Asks->GetThenPin() : nullptr;

		if (Carries == nullptr || Carries->LinkedTo.Num() != 1) return nullptr;

		return Cast<UK2Node_IfThenElse>(Carries->LinkedTo[0]->GetOwningNode());
	}
}

struct FSwitchIntegerTidy final : FGraphTidy {
	virtual const TCHAR* GetName() const override { return TEXT("SwitchInteger"); }

	virtual int32 Apply(UEdGraph* Graph) const override {
		if (Graph == nullptr) return 0;

		int32 Put = 0;

		TArray<UEdGraphNode*> Nodes = Graph->Nodes;

		TSet<UK2Node_IfThenElse*> Spent;

		for (UEdGraphNode* Node : Nodes) {
			UK2Node_IfThenElse* Head = Cast<UK2Node_IfThenElse>(Node);

			if (Head == nullptr || Spent.Contains(Head)) continue;

			UEdGraphPin* Asked = nullptr;

			TArray<FIntegerRung> Ladder;

			for (UK2Node_IfThenElse* Asks = Head; Asks != nullptr; Asks = NextIntegerRung(Asks)) {
				if (Spent.Contains(Asks)) break;

				UEdGraphPin* Says = IntegerFedBy(Asks->GetConditionPin());
				UK2Node* Compares = Says != nullptr ? Cast<UK2Node>(Says->GetOwningNode()) : nullptr;

				UEdGraphPin* First = nullptr;
				UEdGraphPin* Second = nullptr;

				if (!IntegerSidesOf(Compares, First, Second)) break;

				UEdGraphPin* Over = IntegerFedBy(First);

				if (Over == nullptr) break;
				if (Asked != nullptr && Over != Asked) break;

				if (Second->LinkedTo.Num() > 0 || !Second->DefaultValue.IsNumeric()) break;

				const int32 Case = FCString::Atoi(*Second->DefaultValue);

				if (Ladder.Num() > 0 && Case != Ladder.Last().Case + 1) break;

				UEdGraphPin* Matches = Asks->GetElsePin();

				if (Matches == nullptr) break;

				Asked = Over;

				FIntegerRung Rung;

				Rung.Asks = Asks;
				Rung.Compares = Compares;
				Rung.Case = Case;
				Rung.Matches = Matches->LinkedTo;

				Ladder.Add(Rung);
			}

			if (Ladder.Num() < 2 || Asked == nullptr) continue;

			UEdGraphPin* Falls = Ladder.Last().Asks->GetThenPin();

			UK2Node_SwitchInteger* Switch = NewObject<UK2Node_SwitchInteger>(Graph, UK2Node_SwitchInteger::StaticClass(), NAME_None, RF_Transactional);

			Switch->bHasDefaultPin = true;
			Switch->StartIndex = Ladder[0].Case;

			Graph->AddNode(Switch, false, false);

			Switch->CreateNewGuid();
			Switch->PostPlacedNewNode();

			if (Switch->Pins.Num() == 0) Switch->AllocateDefaultPins();

			for (int32 Index = 0; Index < Ladder.Num(); ++Index) {
				Switch->AddPinToSwitchNode();
			}

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

			for (const FIntegerRung& Rung : Ladder) {
				UEdGraphPin* Way = Switch->FindPin(FName(*LexToString(Rung.Case)), EGPD_Output);

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

			for (const FIntegerRung& Rung : Ladder) {
				Spent.Add(Rung.Asks);

				Graph->RemoveNode(Rung.Asks);

				if (Rung.Compares != nullptr) Graph->RemoveNode(Rung.Compares);
			}

			Put++;
		}

		if (Put > 0) {
			UE_LOG(LogReflectionTidySwitchInteger, Display, TEXT("\"%s\" had %d integer switch(es) written as a ladder of comparisons"), *Graph->GetName(), Put);
		}

		return Put;
	}
};

REGISTER_TIDY(FSwitchIntegerTidy)
