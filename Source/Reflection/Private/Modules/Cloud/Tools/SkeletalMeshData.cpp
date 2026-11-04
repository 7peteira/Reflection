#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Cloud/Tools/SkeletalMeshData.h"

#include "Engine/EngineUtilities.h"

#include "Dom/JsonObject.h"
#include "Animation/AnimSequence.h"

#include "Engine/SkeletalMeshSocket.h"

#if UE4_27_AND_UE5
#if UE5_8_BEYOND
#include "Rendering/SkinWeightProfile.h"
#else
#include "Animation/SkinWeightProfile.h"
#endif
#include "GPUSkinPublicDefs.h"
#include "Modules/Cloud/Cloud.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#endif

#if UE5_1_BEYOND
#include "Engine/SkinnedAssetCommon.h"
#endif

#include "EditorFramework/AssetImportData.h"
#include "Importers/Constructor/Importer.h"
#include "Utilities/JsonHelpers.h"

#if ENGINE_UE5
#include "Animation/AnimData/IAnimationDataController.h"
#if ENGINE_MINOR_VERSION >= 4
#include "Animation/AnimData/IAnimationDataModel.h"
#endif
#include "AnimDataController.h"
#endif

void TSkeletalMeshData::Process(UObject* Object, const TArray<TSharedPtr<FJsonValue>>& Exports) {
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object);
	if (SkeletalMesh == nullptr) return;

	for (const TSharedPtr<FJsonValue>& Export : Exports) {
		if (!Export.IsValid() || !Export->AsObject().IsValid()) {
			continue;
		}

		const TSharedPtr<FJsonObject> JsonObject = Export->AsObject();
		if (!IsProperExportData(JsonObject)) continue;

		TSharedPtr<FJsonObject> Properties = JsonObject->GetObjectField(TEXT("Properties"));
		const FString Type = JsonObject->GetStringField(TEXT("Type"));
		const FString Name = JsonObject->GetStringField(TEXT("Name"));

		if (Name != Object->GetName()) continue;

		if (Type == "SkeletalMesh") {
			TArray<TSharedPtr<FJsonValue>> SkeletalMaterials = JsonObject->GetArrayField(TEXT("SkeletalMaterials"));

			TArray<FSkeletalMaterial>& Materials = GetMaterials(SkeletalMesh);

			int SkeletalMaterialIndex = 0;

			IImporter Importer;

			for (const TSharedPtr<FJsonValue>& SkeletalMaterialExport : SkeletalMaterials) {
				if (!SkeletalMaterialExport.IsValid() || !SkeletalMaterialExport->AsObject().IsValid()) {
					continue;
				}

				const TSharedPtr<FJsonObject> SkeletalMaterialObject = SkeletalMaterialExport->AsObject();

				if (Materials.IsValidIndex(SkeletalMaterialIndex)) {
					FSkeletalMaterial& MaterialSlot = Materials[SkeletalMaterialIndex];

					const FString ExportedSlotName = SkeletalMaterialObject->GetStringField(TEXT("MaterialSlotName"));

					if (!ExportedSlotName.IsEmpty() && ExportedSlotName != TEXT("None")) {
						MaterialSlot.MaterialSlotName = FName(*ExportedSlotName);
						MaterialSlot.ImportedMaterialSlotName = MaterialSlot.MaterialSlotName;
					}

					TSharedPtr<FJsonObject> SkeletalMaterial = SkeletalMaterialObject->GetObjectField(TEXT("Material"));

					TObjectPtr<UObject> LoadedObject;
					Importer.LoadExport<UObject>(&SkeletalMaterial, LoadedObject);

					if (IsObjectPtrValid(LoadedObject)) MaterialSlot.MaterialInterface = Cast<UMaterialInterface>(LoadedObject.Get());
				} else break;

				SkeletalMaterialIndex++;
			}

			if (Properties->HasField(TEXT("LODInfo"))) {
				TArray<TSharedPtr<FJsonValue>> LODInfo = Properties->GetArrayField(TEXT("LODInfo"));

				for (const TSharedPtr<FJsonValue>& LOD : LODInfo) {
					if (!LOD.IsValid() || !LOD->AsObject().IsValid()) {
						continue;
					}

					const TSharedPtr<FJsonObject> LODObject = LOD->AsObject();
					if (!LODObject->HasField(TEXT("SourceImportFilename"))) continue;

					FString SourceImportFilename = LODObject->GetStringField(TEXT("SourceImportFilename"));
					if (SourceImportFilename.IsEmpty()) continue;

					SetAssetImportData(SkeletalMesh, NewObject<UAssetImportData>(SkeletalMesh, TEXT("AssetImportData")));
					GetAssetImportData(SkeletalMesh)->SourceData.SourceFiles.Add(SourceImportFilename);
				}
			}

			GetObjectSerializer()->ExportsToNotDeserialize.Empty();
			GetObjectSerializer()->SetExportForDeserialization(JsonObject, SkeletalMesh);
			GetObjectSerializer()->Parent = SkeletalMesh;

			SkeletalMesh->GetMeshOnlySocketList().Empty();

			FUObjectExportContainer* Container = new FUObjectExportContainer(Exports);
			GetObjectSerializer()->DeserializeExports(Container);

			for (const FUObjectExport* UObjectExport : GetObjectSerializer()->GetPropertySerializer()->ExportsContainer->Exports) {
				if (USkeletalMeshSocket* Socket = Cast<USkeletalMeshSocket>(UObjectExport->Object)) {
					SkeletalMesh->GetMeshOnlySocketList().Add(Socket);
				}
			}

			GetObjectSerializer()->DeserializeObjectProperties(KeepPropertiesShared(Properties, {
				"PhysicsAsset",
				"PostProcessAnimBlueprint",
				"ShadowPhysicsAsset",
				"PositiveBoundsExtension",
				"NegativeBoundsExtension",
				"MorphTargets",

				"Sockets",
				"SamplingInfo",
				"LODModels",
				"NaniteResources",

				"SkinWeightProfiles",
			}), SkeletalMesh);

			const int32 SectionsChanged = ApplySectionUserData(SkeletalMesh, JsonObject);
			const int32 SkinWeightProfiles = ApplySkinWeightProfiles(SkeletalMesh);

			ReportSkinWeightProfiles(SkeletalMesh);

			SkeletalMesh->Modify();

			if (SectionsChanged > 0 || Properties->HasField(TEXT("SkinWeightProfiles"))) {
				if (SkinWeightProfiles > 0) {
					InvalidateSkeletalMeshDerivedData(SkeletalMesh);
				}

				SkeletalMesh->PostEditChange();
			}

			BrowseToWhenFinished(SkeletalMesh);
		}
	}
}

int32 TSkeletalMeshData::ApplySectionUserData(USkeletalMesh* Mesh, const TSharedPtr<FJsonObject>& MeshExport) {
#if UE4_27_AND_UE5 && WITH_EDITORONLY_DATA
	FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
	if (ImportedModel == nullptr) return 0;

	const TArray<TSharedPtr<FJsonValue>>* LodModels;
	if (!MeshExport->TryGetArrayField(TEXT("LODModels"), LodModels)) return 0;

	int32 AppliedSections = 0;

	for (int32 LodIndex = 0; LodIndex < LodModels->Num(); ++LodIndex) {
		if (!ImportedModel->LODModels.IsValidIndex(LodIndex)) break;

		const TSharedPtr<FJsonObject> LodModelJson = (*LodModels)[LodIndex]->AsObject();
		if (!LodModelJson.IsValid()) continue;

		const TArray<TSharedPtr<FJsonValue>>* Sections;
		if (!LodModelJson->TryGetArrayField(TEXT("Sections"), Sections)) continue;

		FSkeletalMeshLODModel& LodModel = ImportedModel->LODModels[LodIndex];

		for (int32 SectionIndex = 0; SectionIndex < Sections->Num(); ++SectionIndex) {
			if (!LodModel.Sections.IsValidIndex(SectionIndex)) break;

			const TSharedPtr<FJsonObject> SectionJson = (*Sections)[SectionIndex]->AsObject();
			if (!SectionJson.IsValid()) continue;

			FSkelMeshSection& Section = LodModel.Sections[SectionIndex];

			FSkelMeshSourceSectionUserData& SectionUserData = LodModel.UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);

			bool bFlag = false;

			if (SectionJson->TryGetBoolField(TEXT("bDisabled"), bFlag)) {
				Section.bDisabled = bFlag;
				SectionUserData.bDisabled = bFlag;
			}

			if (SectionJson->TryGetBoolField(TEXT("bCastShadow"), bFlag)) {
				Section.bCastShadow = bFlag;
				SectionUserData.bCastShadow = bFlag;
			}

			if (SectionJson->TryGetBoolField(TEXT("bRecomputeTangent"), bFlag)) {
				Section.bRecomputeTangent = bFlag;
				SectionUserData.bRecomputeTangent = bFlag;
			}

#if UE5_1_BEYOND
			if (SectionJson->TryGetBoolField(TEXT("bVisibleInRayTracing"), bFlag)) {
				Section.bVisibleInRayTracing = bFlag;
				SectionUserData.bVisibleInRayTracing = bFlag;
			}
#endif

			int32 GenerateUpToLodIndex = INDEX_NONE;

			if (SectionJson->TryGetNumberField(TEXT("GenerateUpToLodIndex"), GenerateUpToLodIndex)) {
				Section.GenerateUpToLodIndex = GenerateUpToLodIndex;
				SectionUserData.GenerateUpToLodIndex = GenerateUpToLodIndex;
			}

			AppliedSections++;
		}
	}

	return AppliedSections;
#else
	return 0;
#endif
}

int32 TSkeletalMeshData::ApplySkinWeightProfiles(USkeletalMesh* Mesh) {
#if UE4_27_AND_UE5 && WITH_EDITORONLY_DATA
	FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
	if (ImportedModel == nullptr) return 0;

	const TArray<TSharedPtr<FJsonValue>> Profiles = Cloud::Export::GetSkinWeightsBlocking(Mesh->GetPathName());
	if (Profiles.Num() == 0) return 0;

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	int32 AppliedProfiles = 0;

	for (const TSharedPtr<FJsonValue>& ProfileValue : Profiles) {
		const TSharedPtr<FJsonObject> Profile = ProfileValue.IsValid() ? ProfileValue->AsObject() : nullptr;
		if (!Profile.IsValid() || !Profile->HasField(TEXT("Name"))) continue;

		const FName ProfileName = FName(*Profile->GetStringField(TEXT("Name")));

		const TArray<TSharedPtr<FJsonValue>>* Lods;
		if (!Profile->TryGetArrayField(TEXT("Lods"), Lods)) continue;

		bool bAppliedAnyLod = false;

		for (const TSharedPtr<FJsonValue>& LodValue : *Lods) {
			const TSharedPtr<FJsonObject> Lod = LodValue.IsValid() ? LodValue->AsObject() : nullptr;
			if (!Lod.IsValid()) continue;

			const int32 LodIndex = Lod->GetIntegerField(TEXT("Index"));
			if (!ImportedModel->LODModels.IsValidIndex(LodIndex)) continue;

			FSkeletalMeshLODModel& LodModel = ImportedModel->LODModels[LodIndex];

			const int32 SourceVertices = Lod->GetIntegerField(TEXT("Vertices"));

			if (SourceVertices != LodModel.NumVertices) {
				UE_LOG(LogReflection, Warning, TEXT("Skin weight profile '%s' was cooked against %d vertices at LOD%d, and '%s' has %d there. Skipped, as the weights are keyed by vertex index."), *ProfileName.ToString(), SourceVertices, LodIndex, *Mesh->GetName(), LodModel.NumVertices);

				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* BoneNames;
			if (!Lod->TryGetArrayField(TEXT("Bones"), BoneNames)) continue;

			TArray<int32> BoneIndices;
			BoneIndices.Reserve(BoneNames->Num());

			for (const TSharedPtr<FJsonValue>& BoneName : *BoneNames) {
				BoneIndices.Add(RefSkeleton.FindBoneIndex(FName(*BoneName->AsString())));
			}

			FImportedSkinWeightProfileData& ProfileData = LodModel.SkinWeightProfiles.FindOrAdd(ProfileName);

			ProfileData.SkinWeights.Reset();
			ProfileData.SkinWeights.AddDefaulted(LodModel.NumVertices);

			ProfileData.SourceModelInfluences.Reset();

			for (const FSkelMeshSection& Section : LodModel.Sections) {
				for (int32 VertexIndex = 0; VertexIndex < Section.SoftVertices.Num(); ++VertexIndex) {
					const int32 GlobalIndex = Section.BaseVertexIndex + VertexIndex;
					if (!ProfileData.SkinWeights.IsValidIndex(GlobalIndex)) continue;

					const FSoftSkinVertex& Vertex = Section.SoftVertices[VertexIndex];
					FRawSkinWeight& SkinWeight = ProfileData.SkinWeights[GlobalIndex];

					FMemory::Memcpy(SkinWeight.InfluenceBones, Vertex.InfluenceBones, sizeof(SkinWeight.InfluenceBones));
					FMemory::Memcpy(SkinWeight.InfluenceWeights, Vertex.InfluenceWeights, sizeof(SkinWeight.InfluenceWeights));
				}
			}

			int32 AppliedVertices = 0;

			ProcessJsonArrayField(Lod, TEXT("Overrides"), [&](const TSharedPtr<FJsonObject>& Override) {
				const int32 GlobalIndex = Override->GetIntegerField(TEXT("Vertex"));
				if (!ProfileData.SkinWeights.IsValidIndex(GlobalIndex)) return;

				const TArray<TSharedPtr<FJsonValue>>* Bones;
				const TArray<TSharedPtr<FJsonValue>>* Weights;

				if (!Override->TryGetArrayField(TEXT("Bones"), Bones)) return;
				if (!Override->TryGetArrayField(TEXT("Weights"), Weights)) return;

				int32 SectionIndex = INDEX_NONE;
				int32 SectionVertexIndex = INDEX_NONE;

				LodModel.GetSectionFromVertexIndex(GlobalIndex, SectionIndex, SectionVertexIndex);
				if (!LodModel.Sections.IsValidIndex(SectionIndex)) return;

				const FSkelMeshSection& Section = LodModel.Sections[SectionIndex];

				FBoneIndexType InfluenceBones[MAX_TOTAL_INFLUENCES] = { };
				float InfluenceWeights[MAX_TOTAL_INFLUENCES] = { };

				int32 Influence = 0;
				float TotalWeight = 0.0f;

				for (int32 Index = 0; Index < Bones->Num() && Index < Weights->Num() && Influence < MAX_TOTAL_INFLUENCES; ++Index) {
					const int32 NameIndex = static_cast<int32>((*Bones)[Index]->AsNumber());
					if (!BoneIndices.IsValidIndex(NameIndex)) continue;

					const int32 BoneIndex = BoneIndices[NameIndex];
					if (BoneIndex == INDEX_NONE) continue;

					const int32 BoneMapIndex = Section.BoneMap.IndexOfByKey(static_cast<FBoneIndexType>(BoneIndex));
					if (BoneMapIndex == INDEX_NONE) continue;

					const float Weight = static_cast<float>((*Weights)[Index]->AsNumber());
					if (Weight <= 0.0f) continue;

					InfluenceBones[Influence] = static_cast<FBoneIndexType>(BoneMapIndex);
					InfluenceWeights[Influence] = Weight;

					TotalWeight += Weight;
					Influence++;
				}

				if (Influence == 0 || TotalWeight <= 0.0f) return;

				FRawSkinWeight& SkinWeight = ProfileData.SkinWeights[GlobalIndex];

				FMemory::Memzero(SkinWeight.InfluenceBones, sizeof(SkinWeight.InfluenceBones));
				FMemory::Memzero(SkinWeight.InfluenceWeights, sizeof(SkinWeight.InfluenceWeights));

				uint16 Remaining = TNumericLimits<uint16>::Max();

				for (int32 Index = 0; Index < Influence; ++Index) {
					SkinWeight.InfluenceBones[Index] = InfluenceBones[Index];

					const uint16 Quantized = Index == Influence - 1
						? Remaining
						: static_cast<uint16>(FMath::RoundToInt(InfluenceWeights[Index] / TotalWeight * TNumericLimits<uint16>::Max()));

					SkinWeight.InfluenceWeights[Index] = FMath::Min(Quantized, Remaining);
					Remaining -= SkinWeight.InfluenceWeights[Index];
				}

				AppliedVertices++;
			});

			if (AppliedVertices == 0) {
				LodModel.SkinWeightProfiles.Remove(ProfileName);

				continue;
			}

			bAppliedAnyLod = true;
		}

		if (bAppliedAnyLod) {
			AppliedProfiles++;
		}
	}

	return AppliedProfiles;
#else
	return 0;
#endif
}

void TSkeletalMeshData::ReportSkinWeightProfiles(USkeletalMesh* Mesh) {
#if UE4_27_AND_UE5 && WITH_EDITORONLY_DATA
	const FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
	if (ImportedModel == nullptr) return;

	TSet<FName> NamesWithWeights;

	for (const FSkeletalMeshLODModel& LodModel : ImportedModel->LODModels) {
		for (const TPair<FName, FImportedSkinWeightProfileData>& Profile : LodModel.SkinWeightProfiles) {
			NamesWithWeights.Add(Profile.Key);
		}
	}

	const TArray<FSkinWeightProfileInfo>& Profiles = Mesh->GetSkinWeightProfiles();

	for (const FSkinWeightProfileInfo& Profile : Profiles) {
		if (NamesWithWeights.Contains(Profile.Name)) continue;

		UE_LOG(LogReflection, Warning, TEXT("Skin weight profile '%s' was reflected onto '%s', which has no weights imported under that name. Re-import the mesh from a source that carries the profile."), *Profile.Name.ToString(), *Mesh->GetName());
	}

	for (const FName& Name : NamesWithWeights) {
		const bool bHasEntry = Profiles.ContainsByPredicate([&Name](const FSkinWeightProfileInfo& Profile) {
			return Profile.Name == Name;
		});

		if (bHasEntry) continue;

		UE_LOG(LogReflection, Warning, TEXT("'%s' has skin weights imported under '%s', which the export has no profile for. Those weights are now unreachable."), *Mesh->GetName(), *Name.ToString());
	}
#endif
}

TArray<FSkeletalMaterial>& TSkeletalMeshData::GetMaterials(USkeletalMesh* Mesh) {
#if UE4_27 || ENGINE_UE5
	return Mesh->GetMaterials();
#else
	return Mesh->Materials;
#endif
}
