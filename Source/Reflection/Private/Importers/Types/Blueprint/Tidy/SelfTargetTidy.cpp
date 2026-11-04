#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/GraphTidy.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Self.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionTidySelf, All, All);

struct FSelfTargetTidy final : FGraphTidy {
	virtual const TCHAR* GetName() const override { return TEXT("SelfTarget"); }

	virtual int32 Apply(UEdGraph* Graph) const override {
		if (Graph == nullptr) return 0;

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

		int32 Dropped = 0;

		TArray<UEdGraphNode*> Nodes = Graph->Nodes;

		for (UEdGraphNode* Node : Nodes) {
			UK2Node_CallFunction* Calling = Cast<UK2Node_CallFunction>(Node);

			if (Calling == nullptr) continue;

			UEdGraphPin* Target = Schema->FindSelfPin(*Calling, EGPD_Input);

			if (Target == nullptr || Target->LinkedTo.Num() != 1) continue;

			UK2Node_Self* Itself = Cast<UK2Node_Self>(Target->LinkedTo[0]->GetOwningNode());

			if (Itself == nullptr) continue;

			if (Target->LinkedTo[0]->LinkedTo.Num() != 1) continue;

			Graph->RemoveNode(Itself);

			Dropped++;
		}

		if (Dropped > 0) {
			UE_LOG(LogReflectionTidySelf, Display, TEXT("\"%s\" had %d call(s) made against itself in so many words"), *Graph->GetName(), Dropped);
		}

		return Dropped;
	}
};

REGISTER_TIDY(FSelfTargetTidy)
