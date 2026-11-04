#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Materials/MaterialFunctionImporter.h"
#include "Factories/MaterialFunctionFactoryNew.h"

#if UE4_25_BELOW
#include "Materials/MaterialFunction.h"
#endif

UObject* IMaterialFunctionImporter::CreateAsset(UObject* CreatedAsset) {
	return IImporter::CreateAsset(Cast<UMaterialFunction>(
		NewObject<UMaterialFunctionFactoryNew>()->FactoryCreateNew(
			UMaterialFunction::StaticClass(),
			GetPackage(),
			*GetAssetName(),
			RF_Standalone | RF_Public,
			nullptr,
			GWarn)
	));
}

bool IMaterialFunctionImporter::Import() {
	UMaterialFunction* MaterialFunction = Create<UMaterialFunction>();

#if UE5_1_BEYOND
	MaterialFunction->GetExpressionCollection().Empty();
#else
	MaterialFunction->FunctionExpressions.Empty();
#endif

	if (!OnAssetCreation(MaterialFunction)) return false;

	FUObjectExportContainer* ExpressionContainer = new FUObjectExportContainer();
	const TSharedPtr<FJsonObject> Props = FindMaterialData(GetAssetType(), ExpressionContainer);

	ConstructExpressions(ExpressionContainer);

	if (ExpressionContainer->Num() == 0) {
		ReportMaterialDataMissing();

		return false;
	}

	PropagateExpressions(ExpressionContainer);

	GetObjectSerializer()->DeserializeObjectProperties(GetAssetData(), MaterialFunction);

	PruneMissingMaterialAttributes(MaterialFunction);

	if (HasMaterialEditorOnlyData(MaterialFunction)) {
		MaterialFunction->UpdateInputOutputTypes();
#if !UE4_24_BELOW
		MaterialFunction->UpdateDependentFunctionCandidates();
#endif
	} else {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The function has no editor data"),
			FString::Printf(
				TEXT("'%s' came out without the object its expressions live on, so it has no inputs, no outputs, and nothing that uses it can compile against it."),
				*GetAssetName())
		);
	}

	MaterialFunction->PreEditChange(nullptr);
	MaterialFunction->PostEditChange();

	Save();

	return true;
}
