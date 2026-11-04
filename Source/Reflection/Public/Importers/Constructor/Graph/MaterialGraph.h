#pragma once

#include "Importers/Constructor/Importer.h"

#if UE4_25_BELOW
class UMaterialFunction;
#endif

class FMaterialSubstituteContext;

REFLECTION_API bool HasMaterialEditorOnlyData(const UObject* Object);

class IMaterialGraph : public IImporter {
public:
	static void SetExpressionParent(UObject* Parent, UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Json);
	static void AddExpressionToParent(UObject* Parent, UMaterialExpression* Expression);
	static FExpressionInput PopulateExpressionInput(const FJsonObject* JsonProperties, UMaterialExpression* Expression, const FString& Type = "Default");
	static FName GetExpressionName(const FJsonObject* JsonProperties, const FString& OverrideParameterName = "Expression");

	void RemapConvertOutput(FExpressionInput& Input) const;

	bool IsConvertSubstitute(UMaterialExpression* Expression) const;

	void ReportMaterialDataMissing() const;
	void ReportNullExpressions() const;
	void ReportCreatedStubs() const;

#if ENGINE_UE4

	TMap<FName, UMaterialFunction*> SubgraphFunctions;
#endif

protected:
	TSharedPtr<FJsonObject> FindMaterialData(const FString& Type, FUObjectExportContainer* Container);

	static bool HasNullExpressions(const TSharedPtr<FJsonObject>& Properties);

	void ConstructExpressions(FUObjectExportContainer* Container);
	UMaterialExpression* CreateEmptyExpression(FUObjectExport* Export, FMaterialSubstituteContext& Context);

	void PropagateExpressions(FUObjectExportContainer* Container);

	static void ClampCustomPrimitiveDataIndex(UMaterialExpression* Expression);

	static void PruneMissingMaterialAttributes(UObject* Parent, UObject* MaterialData = nullptr);

	static UMaterialExpression* CreateSubstituteExpression(FMaterialSubstituteContext& Context);
};
