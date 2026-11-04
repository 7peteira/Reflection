#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/GraphTidy.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_DynamicCast.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionTidyPureCast, All, All);

struct FPureCastTidy final : FGraphTidy {
	virtual const TCHAR* GetName() const override { return TEXT("PureCast"); }

	virtual int32 Apply(UEdGraph* Graph) const override {
		if (Graph == nullptr) return 0;

		int32 Turned = 0;

		TArray<UEdGraphNode*> Nodes = Graph->Nodes;

		for (UEdGraphNode* Node : Nodes) {
			UK2Node_DynamicCast* Casting = Cast<UK2Node_DynamicCast>(Node);

			if (Casting == nullptr || Casting->IsNodePure()) continue;

			UEdGraphPin* Worked = Casting->GetBoolSuccessPin();

			if (Worked == nullptr || Worked->LinkedTo.Num() == 0) continue;

			UEdGraphPin* Failed = Casting->GetInvalidCastPin();

			if (Failed != nullptr && Failed->LinkedTo.Num() > 0) continue;

			UEdGraphPin* Into = Casting->GetExecPin();
			UEdGraphPin* OnFrom = Casting->GetValidCastPin();

			TArray<UEdGraphPin*> Before = Into != nullptr ? Into->LinkedTo : TArray<UEdGraphPin*>();
			TArray<UEdGraphPin*> After = OnFrom != nullptr ? OnFrom->LinkedTo : TArray<UEdGraphPin*>();

			Casting->SetPurity(true);

			for (UEdGraphPin* From : Before) {
				for (UEdGraphPin* To : After) {
					if (From != nullptr && To != nullptr) From->MakeLinkTo(To);
				}
			}

			Turned++;
		}

		if (Turned > 0) {
			UE_LOG(LogReflectionTidyPureCast, Display, TEXT("\"%s\" had %d cast(s) standing in the run that nothing ever went the other way out of"), *Graph->GetName(), Turned);
		}

		return Turned;
	}
};

REGISTER_TIDY(FPureCastTidy)
