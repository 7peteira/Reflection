#pragma once

#include "Importers/Constructor/Importer.h"

class ICurveExpressionImporter : public IImporter {
public:
	virtual UObject* CreateAsset(UObject* CreatedAsset = nullptr) override;
	virtual bool Import() override;

private:
	TArray<TSharedPtr<FJsonValue>> FetchExpressions();

	static FString GetAssignments(const TArray<TSharedPtr<FJsonValue>>& Expressions, TArray<FName>& OutTargets);

	bool ImportAsControlRig(const TArray<TSharedPtr<FJsonValue>>& Expressions);
};

REGISTER_IMPORTER(ICurveExpressionImporter, {
	TEXT("CurveExpressionsDataAsset")
}, "Data Assets");
