#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Cloud/Tools/ClothingData.h"

#include "Engine/EngineUtilities.h"
#include "Utilities/JsonHelpers.h"

#if UE4_27_AND_UE5
#include "ClothingAssetBase.h"

#if UE5_3_BEYOND
#include "ClothingAsset.h"
#include "ClothLODData.h"
#else
#include "ClothingSystemRuntimeCommon/Public/ClothingAsset.h"
#endif

#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "SkeletalMeshTypes.h"
#endif

void TClothingData::Process(UObject* Object, const TArray<TSharedPtr<FJsonValue>>& Exports) {
#if UE4_27_AND_UE5
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object);
	if (SkeletalMesh == nullptr) return;

	TSharedPtr<FJsonObject> MeshExport;

	for (const TSharedPtr<FJsonValue>& Export : Exports) {
		if (!Export.IsValid() || !Export->AsObject().IsValid()) continue;

		const TSharedPtr<FJsonObject> JsonObject = Export->AsObject();
		if (!IsProperExportData(JsonObject)) continue;

		if (JsonObject->GetStringField(TEXT("Type")) != "SkeletalMesh") continue;
		if (JsonObject->GetStringField(TEXT("Name")) != Object->GetName()) continue;

		MeshExport = JsonObject;

		break;
	}

	if (!MeshExport.IsValid()) return;

	const TSharedPtr<FJsonObject> MeshProperties = MeshExport->GetObjectField(TEXT("Properties"));

	if (!MeshProperties->HasField(TEXT("MeshClothingAssets"))) {
		AppendNotification(
			FText::FromString("No Clothing Data: " + SkeletalMesh->GetName()),
			FText::FromString("The mesh was exported without any clothing assets."),
			3.5f,
			SNotificationItem::CS_Fail,
			false,
			310.0f
		);

		return;
	}

	FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();

	if (ImportedModel == nullptr || ImportedModel->LODModels.Num() == 0) {
		SpawnPrompt("Missing Imported Model", "'" + SkeletalMesh->GetName() + "' has no imported model, so it has no sections to bind cloth to. Import the mesh itself first, then reflect its clothing.");

		return;
	}

	for (int32 Index = SkeletalMesh->GetMeshClothingAssets().Num() - 1; Index >= 0; --Index) {
		UClothingAssetBase* ClothingAsset = SkeletalMesh->GetMeshClothingAssets()[Index];
		if (ClothingAsset == nullptr) continue;

		ClothingAsset->Modify();
		ClothingAsset->UnbindFromSkeletalMesh(SkeletalMesh);
	}

	SkeletalMesh->GetMeshClothingAssets().Empty();

	GetObjectSerializer()->ExportsToNotDeserialize.Empty();
	GetObjectSerializer()->SetExportForDeserialization(MeshExport, SkeletalMesh);
	GetObjectSerializer()->Parent = SkeletalMesh;

	GetObjectSerializer()->WhitelistedTypes = { TEXT("Cloth") };

	FUObjectExportContainer* Container = new FUObjectExportContainer(Exports);
	GetObjectSerializer()->DeserializeExports(Container);

	TArray<UClothingAssetCommon*> ClothingAssets;
	TArray<TArray<int32>> SourceLodMaps;

	ProcessJsonArrayField(MeshProperties, TEXT("MeshClothingAssets"), [&](const TSharedPtr<FJsonObject>& Reference) {
		const FName ExportName = GetExportNameOfSubobject(Reference->GetStringField(TEXT("ObjectName")));

		UClothingAssetCommon* ClothingAsset = Cast<UClothingAssetCommon>(Container->Find(ExportName)->Object);

		ClothingAssets.Add(ClothingAsset);
		SourceLodMaps.AddDefaulted();

		if (ClothingAsset == nullptr) {
			UE_LOG(LogReflection, Warning, TEXT("Clothing asset '%s' is missing from the exports of '%s'"), *ExportName.ToString(), *SkeletalMesh->GetName());

			return;
		}

		SourceLodMaps.Last() = ClothingAsset->LodMap;
		ClothingAsset->LodMap.Empty();

		if (!ClothingAsset->GetAssetGuid().IsValid()) {
			if (const FStructProperty* AssetGuidProperty = CastField<FStructProperty>(UClothingAssetCommon::StaticClass()->FindPropertyByName(TEXT("AssetGuid")))) {
				*AssetGuidProperty->ContainerPtrToValuePtr<FGuid>(ClothingAsset) = FGuid::NewGuid();
			}
		}

		RebuildParameterMasks(ClothingAsset);

		SkeletalMesh->GetMeshClothingAssets().Add(ClothingAsset);

		ClothingAsset->RefreshBoneMapping(SkeletalMesh);
	});

	int32 BoundSections = 0;

	{
	FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);

	const TArray<TSharedPtr<FJsonValue>>* LodModels;

	if (MeshExport->TryGetArrayField(TEXT("LODModels"), LodModels)) {
		for (int32 MeshLodIndex = 0; MeshLodIndex < LodModels->Num(); ++MeshLodIndex) {
			if (!ImportedModel->LODModels.IsValidIndex(MeshLodIndex)) break;

			const TSharedPtr<FJsonObject> LodModel = (*LodModels)[MeshLodIndex]->AsObject();
			if (!LodModel.IsValid()) continue;

			const TArray<TSharedPtr<FJsonValue>>* Sections;
			if (!LodModel->TryGetArrayField(TEXT("Sections"), Sections)) continue;

			for (int32 SectionIndex = 0; SectionIndex < Sections->Num(); ++SectionIndex) {
				const TSharedPtr<FJsonObject> Section = (*Sections)[SectionIndex]->AsObject();
				if (!Section.IsValid()) continue;

				int32 AssetIndex;
				if (!Section->TryGetNumberField(TEXT("CorrespondClothAssetIndex"), AssetIndex)) continue;

				if (!ClothingAssets.IsValidIndex(AssetIndex) || ClothingAssets[AssetIndex] == nullptr) continue;
				if (!ImportedModel->LODModels[MeshLodIndex].Sections.IsValidIndex(SectionIndex)) continue;

				const TArray<int32>& SourceLodMap = SourceLodMaps[AssetIndex];
				const int32 AssetLodIndex = SourceLodMap.IsValidIndex(MeshLodIndex) ? SourceLodMap[MeshLodIndex] : 0;

				if (AssetLodIndex == INDEX_NONE) continue;

				UClothingAssetCommon* ClothingAsset = ClothingAssets[AssetIndex];
				if (!ClothingAsset->BindToSkeletalMesh(SkeletalMesh, MeshLodIndex, SectionIndex, AssetLodIndex)) continue;

				FSkeletalMeshLODModel& TargetLodModel = ImportedModel->LODModels[MeshLodIndex];
				const FSkelMeshSection& BoundSection = TargetLodModel.Sections[SectionIndex];

				FSkelMeshSourceSectionUserData& SectionUserData = TargetLodModel.UserSectionsData.FindOrAdd(BoundSection.OriginalDataSectionIndex);

				int32 BoundAssetIndex = INDEX_NONE;
				SkeletalMesh->GetMeshClothingAssets().Find(ClothingAsset, BoundAssetIndex);

				SectionUserData.CorrespondClothAssetIndex = static_cast<int16>(BoundAssetIndex);
				SectionUserData.ClothingData.AssetGuid = ClothingAsset->GetAssetGuid();
				SectionUserData.ClothingData.AssetLodIndex = AssetLodIndex;

				BoundSections++;
			}
		}
	}

	for (UClothingAssetCommon* ClothingAsset : ClothingAssets) {
		if (ClothingAsset == nullptr) continue;

#if ENGINE_UE5
		ClothingAsset->InvalidateAllCachedData();
#endif
		ClothingAsset->BuildLodTransitionData();

		ClothingAsset->Modify();
	}

	SkeletalMesh->SetHasActiveClothingAssets(SkeletalMesh->ComputeActiveClothingAssets());

	SkeletalMesh->Modify();
	SkeletalMesh->MarkPackageDirty();

	}

	BrowseToWhenFinished(SkeletalMesh);

	AppendNotification(
		FText::FromString("Reflected Clothing Data: " + SkeletalMesh->GetName()),
		FText::FromString(FString::Printf(TEXT("%d asset(s), %d section(s) bound"), SkeletalMesh->GetMeshClothingAssets().Num(), BoundSections)),
		3.5f,
		FAppStyle::Get().GetBrush("PhysicsAssetEditor.EnableCollision"),
		BoundSections > 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail,
		false,
		310.0f
	);
#endif
}

void TClothingData::RebuildParameterMasks(UClothingAssetCommon* ClothingAsset) {
#if UE4_27_AND_UE5 && WITH_EDITORONLY_DATA
	const UEnum* TargetEnum = FindEnumByType(TEXT("EChaosWeightMapTarget"));

	if (TargetEnum == nullptr) {
		TargetEnum = FindEnumByType(TEXT("EWeightMapTargetCommon"));
	}

	for (FClothLODDataCommon& LodData : ClothingAsset->LodData) {
		LodData.PointWeightMaps.Empty();

		for (const TPair<uint32, FPointWeightMap>& WeightMap : LodData.PhysicalMeshData.WeightMaps) {
			if (WeightMap.Value.Values.Num() == 0) continue;

			FPointWeightMap& Mask = LodData.PointWeightMaps.Add_GetRef(WeightMap.Value);

			Mask.CurrentTarget = static_cast<uint8>(WeightMap.Key);
			Mask.bEnabled = true;

			const FString TargetName = TargetEnum != nullptr ? TargetEnum->GetNameStringByValue(WeightMap.Key) : FString();

			Mask.Name = TargetName.IsEmpty() ? FName(*FString::FromInt(WeightMap.Key)) : FName(*TargetName);
		}
	}
#endif
}
