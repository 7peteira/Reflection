#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/GraphTidy.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionTidyBranch, All, All);

struct FBooleanBranchTidy final : FGraphTidy {
	virtual const TCHAR* GetName() const override { return TEXT("Boolean Branch"); }

	virtual int32 Apply(UEdGraph* Graph) const override {
		if (Graph == nullptr) return 0;

		int32 PutBack = 0;

		TArray<UEdGraphNode*> Nodes = Graph->Nodes;

		for (UEdGraphNode* Node : Nodes) {
			UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Node);

			if (Branch == nullptr) continue;

			UEdGraphPin* Condition = Branch->GetConditionPin();

			if (Condition == nullptr || Condition->LinkedTo.Num() != 1) continue;

			UEdGraphPin* Read = Condition->LinkedTo[0];
			UK2Node_VariableGet* Get = Cast<UK2Node_VariableGet>(Read->GetOwningNode());

			if (Get == nullptr || !Get->IsNodePure()) continue;

			if (Read->PinType.PinCategory != UEdGraphSchema_K2::PC_Boolean) continue;

			if (Read->LinkedTo.Num() != 1) continue;

			UEdGraphPin* Entered = Branch->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);

			const TArray<UEdGraphPin*> Enters = Entered != nullptr ? Entered->LinkedTo : TArray<UEdGraphPin*>();
			const TArray<UEdGraphPin*> WhenTrue = Branch->GetThenPin() != nullptr ? Branch->GetThenPin()->LinkedTo : TArray<UEdGraphPin*>();
			const TArray<UEdGraphPin*> WhenFalse = Branch->GetElsePin() != nullptr ? Branch->GetElsePin()->LinkedTo : TArray<UEdGraphPin*>();

			if (!SetVariableGetVariation(Get, TEXT("Branch"))) continue;

			if (UEdGraphPin* In = Get->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input)) {
				for (UEdGraphPin* Enter : Enters) Enter->MakeLinkTo(In);
			}

			if (UEdGraphPin* True = Get->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output)) {
				for (UEdGraphPin* Next : WhenTrue) True->MakeLinkTo(Next);
			}

			if (UEdGraphPin* False = Get->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output)) {
				for (UEdGraphPin* Next : WhenFalse) False->MakeLinkTo(Next);
			}

			Graph->RemoveNode(Branch);

			PutBack++;
		}

		if (PutBack > 0) {
			UE_LOG(LogReflectionTidyBranch, Display, TEXT("\"%s\" had %d boolean(s) read and branched on at once"), *Graph->GetName(), PutBack);
		}

		return PutBack;
	}
};

REGISTER_TIDY(FBooleanBranchTidy)
