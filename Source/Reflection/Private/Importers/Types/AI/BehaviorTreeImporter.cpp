#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/AI/BehaviorTreeImporter.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BlackboardData.h"
#include "Engine/EngineUtilities.h"

int32 IBehaviorTreeImporter::PruneEmptyChildren(UBTCompositeNode* Composite) {
	if (Composite == nullptr) return 0;

	int32 Removed = 0;

	for (int32 Index = Composite->Children.Num() - 1; Index >= 0; --Index) {
		FBTCompositeChild& Child = Composite->Children[Index];

		if (Child.ChildComposite == nullptr && Child.ChildTask == nullptr) {
			Composite->Children.RemoveAt(Index);

			Removed++;

			continue;
		}

		Removed += PruneEmptyChildren(Child.ChildComposite);
	}

	return Removed;
}

void IBehaviorTreeImporter::Repair(UObject* Asset) const {
	UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset);
	if (Tree == nullptr) return;

	const int32 Removed = PruneEmptyChildren(Cast<UBTCompositeNode>(Tree->RootNode));

	if (Removed > 0) {
		FImportIssues::Report(
			EImportIssue::MissingClass,
			FString::Printf(TEXT("%d branch(es) of the tree came across empty"), Removed),
			FString::Printf(
				TEXT("'%s' names nodes whose class this build hasn't got, so those branches are gone and the rest of the tree is kept. Left in place they crash the behavior tree editor as it lays the graph out."),
				*GetAssetName()
			)
		);

		UE_LOG(LogReflection, Warning, TEXT("\"%s\" dropped %d empty branch(es)"), *GetAssetName(), Removed);
	}
}

void IBehaviorTreeImporter::Validate(UObject* Asset) const {
	const UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset);
	if (Tree == nullptr) return;

	if (Tree->RootNode == nullptr) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The behavior tree has no root node"),
			FString::Printf(
				TEXT("'%s' came across without the composite everything else hangs off, so there is no tree to run. A node class this engine hasn't got is the usual reason."),
				*GetAssetName()
			)
		);
	}

	if (Tree->BlackboardAsset == nullptr) {
		FImportIssues::Report(
			EImportIssue::MissingAsset,
			TEXT("The behavior tree names no blackboard"),
			FString::Printf(
				TEXT("'%s' has no blackboard, so any node reading or writing a key has nothing to read or write."),
				*GetAssetName()
			)
		);
	}

	UE_LOG(LogReflection, Display, TEXT("\"%s\" imported with %s and %s blackboard"),
		*GetAssetName(),
		Tree->RootNode != nullptr ? TEXT("a root node") : TEXT("NO root node"),
		Tree->BlackboardAsset != nullptr ? *Tree->BlackboardAsset->GetName() : TEXT("no"));

	UE_LOG(LogReflection, Display,
		TEXT("\"%s\" has no editor graph: cooking drops it, so the canvas opens empty while the nodes themselves are there"),
		*GetAssetName());
}
