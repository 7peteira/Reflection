#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Materials/MaterialImporter.h"
#include "Importers/Types/Materials/MaterialStubs.h"

#if (ENGINE_UE5 && ENGINE_MINOR_VERSION < 3) || ENGINE_UE4
#include "Materials/Material.h"
#else
#include "MaterialDomain.h"
#endif

#if !UE4_24_BELOW
#include "MaterialCachedData.h"
#endif
#include "Factories/MaterialFactoryNew.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Engine/Texture.h"
#include "Settings/ReflectionSettings.h"

#if ENGINE_UE5
class UMaterialAccessor final : public UMaterial {
public:
	FMaterialCachedExpressionData* GetCachedExpressionDataRef() const {
		return CachedExpressionData.Get();
	}
};
#endif

UObject* IMaterialImporter::CreateAsset(UObject* CreatedAsset) {
	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	UMaterial* Material = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(UMaterial::StaticClass(), GetPackage(), *GetAssetName(), RF_Standalone | RF_Public, nullptr, GWarn));

	return IImporter::CreateAsset(Material);
}

bool IMaterialImporter::Import() {
	UMaterial* Material = Create<UMaterial>();

#if UE5_1_BEYOND
	Material->GetExpressionCollection().Empty();
#else
	Material->Expressions.Empty();
#endif

	FUObjectExportContainer* ExpressionContainer = new FUObjectExportContainer();
	TSharedPtr<FJsonObject> Props = FindMaterialData(GetAssetType(), ExpressionContainer);

	if (HasNullExpressions(Props)) {
		ReportNullExpressions();

		return false;
	}

	ConstructExpressions(ExpressionContainer);

	const UReflectionSettings* Settings = GetSettings();

	if (ExpressionContainer->Num() == 0) {
#if ENGINE_UE5
		if (GetModdingAssetSettings().Material.Stubs) {
			CreateStubs(this);
			ReportCreatedStubs();
		}
		else {
#endif
			ReportMaterialDataMissing();
#if ENGINE_UE5
			return false;
		}
#endif
	}

	PropagateExpressions(ExpressionContainer);

	if (AssetExport->GetJsonObject().Has("CachedExpressionData")) {
		const TSharedPtr<FJsonObject> Cached = AssetExport->GetJsonObject().GetObject("CachedExpressionData").JsonObject;
		const TArray<TSharedPtr<FJsonValue>>* Referenced = nullptr;

		if (Cached.IsValid() && Cached->TryGetArrayField(TEXT("ReferencedTextures"), Referenced) && Referenced->Num() == 1) {
			TArray<UMaterialExpressionTextureBase*> Sampling;

#if UE5_1_BEYOND
			for (UMaterialExpression* Expression : Material->GetExpressions()) {
#else
			for (UMaterialExpression* Expression : Material->Expressions) {
#endif
				if (UMaterialExpressionTextureBase* TextureExpression = Cast<UMaterialExpressionTextureBase>(Expression)) {
					Sampling.Add(TextureExpression);
				}
			}

			if (Sampling.Num() == 1 && Sampling[0]->Texture == nullptr) {
				const TSharedPtr<FJsonObject> Reference = (*Referenced)[0]->AsObject();
				TObjectPtr<UTexture> Texture = nullptr;

				LoadExport<UTexture>(&Reference, Texture);

				if (Texture != nullptr) {
					Sampling[0]->Texture = Texture;
				}
			}
		}
	}

#if UE5_1_BEYOND
	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
#else
	UMaterial* EditorOnlyData = Material;
#endif

	TArray<FString> IgnoredProperties = TArray<FString> {
		"ParameterGroupData",
		"ExpressionCollection",
		"CustomizedUVs"
	};

	const TSharedPtr<FJsonObject> RawConnectionData = TSharedPtr<FJsonObject>(Props);
	for (FString Property : IgnoredProperties) {
		if (RawConnectionData->HasField(Property)) {
			RawConnectionData->RemoveField(Property);
		}
	}

	GetObjectSerializer()->DeserializeObjectProperties(RawConnectionData, EditorOnlyData);

	const TArray<TSharedPtr<FJsonValue>>* InputsPtr;

	if (Props->TryGetArrayField(TEXT("CustomizedUVs"), InputsPtr)) {
		int i = 0;
		for (const auto& InputValue : *InputsPtr) {
			FJsonObject* InputObject = InputValue->AsObject().Get();
			FName InputExpressionName = GetExpressionName(InputObject);

			if (ExpressionContainer->Contains(InputExpressionName)) {
				FExpressionInput Input = PopulateExpressionInput(InputObject, ExpressionContainer->Find<UMaterialExpression>(InputExpressionName));
				RemapConvertOutput(Input);

				EditorOnlyData->CustomizedUVs[i] = *reinterpret_cast<FVector2MaterialInput*>(&Input);
			}
			i++;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* StringParameterGroupData;
	if (Props->TryGetArrayField(TEXT("ParameterGroupData"), StringParameterGroupData)) {
		TArray<FParameterGroupData> ParameterGroupData;

		for (const auto& ParameterGroupDataObject : *StringParameterGroupData) {
			if (ParameterGroupDataObject->IsNull()) continue;
			FParameterGroupData GroupData;

			FString GroupName;
			if (ParameterGroupDataObject->AsObject()->TryGetStringField(TEXT("GroupName"), GroupName)) GroupData.GroupName = GroupName;
			int GroupSortPriority;
			if (ParameterGroupDataObject->AsObject()->TryGetNumberField(TEXT("GroupSortPriority"), GroupSortPriority)) GroupData.GroupSortPriority = GroupSortPriority;

			ParameterGroupData.Add(GroupData);
		}

		EditorOnlyData->ParameterGroupData = ParameterGroupData;
	}

	PruneMissingMaterialAttributes(Material, EditorOnlyData);

	if (!OnAssetCreation(Material)) return false;

	const TSharedPtr<FJsonObject>* ShadingModelsPtr;

	if (GetAssetData()->TryGetObjectField(TEXT("ShadingModels"), ShadingModelsPtr)) {
		int ShadingModelField;

		if (ShadingModelsPtr->Get()->TryGetNumberField(TEXT("ShadingModelField"), ShadingModelField)) {
#if UE5_1_BEYOND
			Material->GetShadingModels().SetShadingModelField(ShadingModelField);
#else
#endif
		}
	}

	GetObjectSerializer()->DeserializeObjectProperties(GetAssetData(), Material);

#if ENGINE_UE5
	if (AssetExport->GetJsonObject().Has("CachedExpressionData")) {
		FUObjectJsonValueExport CachedExpressionData = AssetExport->GetJsonObject().GetObject("CachedExpressionData");

		UMaterialAccessor* Accessor = Cast<UMaterialAccessor>(Material);
		FMaterialCachedExpressionData* CachedData = Accessor->GetCachedExpressionDataRef();

	}
#endif

	UMaterialExpression* PositionalExpression = EditorOnlyData->BaseColor.Expression;
	if (UMaterialExpression* MaterialAttributes = EditorOnlyData->MaterialAttributes.Expression) {
		if (Material->bUseMaterialAttributes) {
			PositionalExpression = MaterialAttributes;
		}
	}

	if (!PositionalExpression) {
		PositionalExpression = EditorOnlyData->EmissiveColor.Expression;
	}

	if (PositionalExpression) {
		Material->EditorX = PositionalExpression->MaterialExpressionEditorX + PositionalExpression->GetWidth() * 2.5;
		Material->EditorY = PositionalExpression->MaterialExpressionEditorY;
	}

#if !UE4_24_BELOW
	Material->UpdateCachedExpressionData();
#endif

	FMaterialUpdateContext MaterialUpdateContext;
	MaterialUpdateContext.AddMaterial(Material);

	Material->ForceRecompileForRendering();

	Material->PostEditChange();
	Material->MarkPackageDirty();
	Material->PreEditChange(nullptr);

	Save();

	return true;
}
