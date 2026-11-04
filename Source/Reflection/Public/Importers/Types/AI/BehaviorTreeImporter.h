#pragma once

#include "Importers/Constructor/ObjectImporter.h"

class IBehaviorTreeImporter final : public IObjectImporter {
public:
	virtual void Repair(UObject* Asset) const override;
	virtual void Validate(UObject* Asset) const override;

private:
	static int32 PruneEmptyChildren(class UBTCompositeNode* Composite);
};

REGISTER_IMPORTER(IBehaviorTreeImporter, TArray<FString>{
	TEXT("BehaviorTree")
}, TEXT("AI Assets"));
