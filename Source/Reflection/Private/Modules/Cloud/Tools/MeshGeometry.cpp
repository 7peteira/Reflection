#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Cloud/Tools/MeshGeometry.h"

#include "Engine/AssetCompatibility.h"
#include "Engine/EngineUtilities.h"
#include "Utilities/JsonHelpers.h"

#include "GPUSkinPublicDefs.h"
#include "Modules/Cloud/Cloud.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODImporterData.h"

#if UE5_1_BEYOND
#include "Engine/SkinnedAssetCommon.h"
#endif
#include "SkeletalMeshTypes.h"

#if !UE4_27_AND_UE5
#include "MeshUtilities.h"
#endif

namespace {
#if UE4_27_AND_UE5
	void SetMeshHasVertexColors(USkeletalMesh* Mesh, const bool Value) { Mesh->SetHasVertexColors(Value); }
	const TArray<FSkeletalMaterial>& MeshMaterials(const USkeletalMesh* Mesh) { return Mesh->GetMaterials(); }
#else
	void SetMeshHasVertexColors(USkeletalMesh* Mesh, const bool Value) { Mesh->bHasVertexColors = Value; }
	const TArray<FSkeletalMaterial>& MeshMaterials(const USkeletalMesh* Mesh) { return Mesh->Materials; }
#endif

	static const FVector3f GMeshUpAxis = FVector3f(0.0f, 0.0f, 1.0f);
	static const FVector3f GMeshForwardAxis = FVector3f(1.0f, 0.0f, 0.0f);
}

int32 TMeshGeometry::RebuildLodModels(USkeletalMesh* SkeletalMesh, const TSharedPtr<FJsonObject>& Response) {
	if (SkeletalMesh == nullptr || !Response.IsValid()) return 0;

	FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
	if (ImportedModel == nullptr) return 0;

	const TArray<TSharedPtr<FJsonValue>>* Lods;
	if (!Response->TryGetArrayField(TEXT("lods"), Lods)) return 0;

	const FReferenceSkeleton& RefSkeleton = MeshRefSkeleton(SkeletalMesh);

	int32 RebuiltLods = 0;

	{
	FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);

	SkeletalMesh->Modify();

	for (const TSharedPtr<FJsonValue>& LodValue : *Lods) {
		const TSharedPtr<FJsonObject> Lod = LodValue.IsValid() ? LodValue->AsObject() : nullptr;
		if (!Lod.IsValid()) continue;

		const int32 LodIndex = Lod->GetIntegerField(TEXT("Index"));

		const TSharedPtr<FJsonObject>* Vertices;
		if (!Lod->TryGetObjectField(TEXT("Vertices"), Vertices)) continue;

		const TArray<TSharedPtr<FJsonValue>>* Indices;
		const TArray<TSharedPtr<FJsonValue>>* SectionsJson;

		if (!Lod->TryGetArrayField(TEXT("Indices"), Indices)) continue;
		if (!Lod->TryGetArrayField(TEXT("Sections"), SectionsJson)) continue;

		const int32 VertexCount = (*Vertices)->GetIntegerField(TEXT("Count"));
		const int32 BonesPerVertex = (*Vertices)->GetIntegerField(TEXT("BonesPerVertex"));
		const int32 PayloadTexCoords = FMath::Max((*Vertices)->GetIntegerField(TEXT("NumTexCoords")), 1);
		const int32 NumTexCoords = FMath::Min(PayloadTexCoords, static_cast<int32>(MAX_TEXCOORDS));

		if (VertexCount == 0 || BonesPerVertex == 0) continue;

		const TArray<TSharedPtr<FJsonValue>>* Positions = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Normals = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Tangents = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Binormals = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* UVs = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Colors = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Weights = nullptr;

		(*Vertices)->TryGetArrayField(TEXT("Positions"), Positions);
		(*Vertices)->TryGetArrayField(TEXT("Normals"), Normals);
		(*Vertices)->TryGetArrayField(TEXT("Binormals"), Binormals);
		(*Vertices)->TryGetArrayField(TEXT("Tangents"), Tangents);
		(*Vertices)->TryGetArrayField(TEXT("UVs"), UVs);
		(*Vertices)->TryGetArrayField(TEXT("Colors"), Colors);
		(*Vertices)->TryGetArrayField(TEXT("Bones"), Bones);
		(*Vertices)->TryGetArrayField(TEXT("Weights"), Weights);

		while (ImportedModel->LODModels.Num() <= LodIndex) {
			ImportedModel->LODModels.Add(new FSkeletalMeshLODModel());
		}

		while (SkeletalMesh->GetLODNum() <= LodIndex
#if UE5_4_BEYOND
			|| SkeletalMesh->GetNumSourceModels() <= LodIndex
#endif
			) {
			SkeletalMesh->AddLODInfo();
		}

		FSkeletalMeshImportData ImportData;

		ImportData.NumTexCoords = NumTexCoords;
		ImportData.bHasNormals = true;
		ImportData.bHasTangents = true;
		ImportData.bHasVertexColors = false;

		ImportData.Points.Empty(VertexCount);
		ImportData.PointToRawMap.Empty(VertexCount);

		TArray<FVector3f> VertexNormals;
		TArray<FVector3f> VertexBinormals;
		TArray<FVector3f> VertexTangents;

		VertexNormals.Empty(VertexCount);
		VertexBinormals.Empty(VertexCount);
		VertexTangents.Empty(VertexCount);

		TArray<int32> VertexSection;
		VertexSection.Init(0, VertexCount);

		for (int32 SectionIndex = 0; SectionIndex < SectionsJson->Num(); ++SectionIndex) {
			const TSharedPtr<FJsonObject> SectionJson = (*SectionsJson)[SectionIndex]->AsObject();
			if (!SectionJson.IsValid()) continue;

			const int32 BaseVertex = SectionJson->GetIntegerField(TEXT("BaseVertexIndex"));
			const int32 SectionVertices = SectionJson->GetIntegerField(TEXT("NumVertices"));

			for (int32 Local = 0; Local < SectionVertices; ++Local) {
				if (VertexSection.IsValidIndex(BaseVertex + Local)) {
					VertexSection[BaseVertex + Local] = SectionIndex;
				}
			}
		}

		TArray<FVector2f> VertexUVs;
		TArray<FColor> VertexColors;

		VertexUVs.Empty(VertexCount * NumTexCoords);
		VertexColors.Empty(VertexCount);

		for (int32 Vertex = 0; Vertex < VertexCount; ++Vertex) {
			ImportData.Points.Add(FVector3f(ReadFloat(Positions, Vertex * 3), ReadFloat(Positions, Vertex * 3 + 1), ReadFloat(Positions, Vertex * 3 + 2)));
			ImportData.PointToRawMap.Add(Vertex);

			VertexNormals.Add(FVector3f(ReadFloat(Normals, Vertex * 3), ReadFloat(Normals, Vertex * 3 + 1), ReadFloat(Normals, Vertex * 3 + 2)));

			if (Binormals != nullptr) {
				VertexBinormals.Add(FVector3f(ReadFloat(Binormals, Vertex * 3), ReadFloat(Binormals, Vertex * 3 + 1), ReadFloat(Binormals, Vertex * 3 + 2)));
			}
			VertexTangents.Add(FVector3f(ReadFloat(Tangents, Vertex * 3), ReadFloat(Tangents, Vertex * 3 + 1), ReadFloat(Tangents, Vertex * 3 + 2)));

			for (int32 UV = 0; UV < NumTexCoords; ++UV) {
				const int32 Offset = (Vertex * PayloadTexCoords + UV) * 2;

				VertexUVs.Add(FVector2f(ReadFloat(UVs, Offset), ReadFloat(UVs, Offset + 1)));
			}

			const uint32 Packed = ReadUInt(Colors, Vertex);

			const FColor Color((Packed >> 24) & 0xFF, (Packed >> 16) & 0xFF, (Packed >> 8) & 0xFF, Packed & 0xFF);

			VertexColors.Add(Color);

			if (Color != FColor::White) {
				ImportData.bHasVertexColors = true;
			}
		}

		ImportData.Materials.Empty(SectionsJson->Num());

		for (int32 SectionIndex = 0; SectionIndex < SectionsJson->Num(); ++SectionIndex) {
			const TSharedPtr<FJsonObject> SectionJson = (*SectionsJson)[SectionIndex]->AsObject();

			SkeletalMeshImportData::FMaterial& Material = ImportData.Materials.AddDefaulted_GetRef();

			const int32 MaterialIndex = SectionJson.IsValid() ? SectionJson->GetIntegerField(TEXT("MaterialIndex")) : 0;
			const TArray<FSkeletalMaterial>& Slots = MeshMaterials(SkeletalMesh);

			Material.MaterialImportName = Slots.IsValidIndex(MaterialIndex)
				? Slots[MaterialIndex].MaterialSlotName.ToString()
				: FString::Printf(TEXT("Material_%d"), MaterialIndex);

			UE_LOG(LogReflection, Display, TEXT("LOD %d section %d: cooked material %d of %d, matched by slot name \"%s\""),
				LodIndex, SectionIndex, MaterialIndex, Slots.Num(), *Material.MaterialImportName);
		}

		ImportData.MaxMaterialIndex = FMath::Max(ImportData.Materials.Num() - 1, 0);

		ImportData.Faces.Empty(Indices->Num() / 3);
		ImportData.Wedges.Empty(Indices->Num());

		for (int32 SectionIndex = 0; SectionIndex < SectionsJson->Num(); ++SectionIndex) {
			const TSharedPtr<FJsonObject> SectionJson = (*SectionsJson)[SectionIndex]->AsObject();
			if (!SectionJson.IsValid()) continue;

			const int32 BaseIndex = SectionJson->GetIntegerField(TEXT("BaseIndex"));
			const int32 NumTriangles = SectionJson->GetIntegerField(TEXT("NumTriangles"));

			for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle) {
				SkeletalMeshImportData::FTriangle& Face = ImportData.Faces.AddDefaulted_GetRef();

				Face.MatIndex = static_cast<uint16>(SectionIndex);
				Face.AuxMatIndex = 0;
				Face.SmoothingGroups = 1;

				for (int32 Corner = 0; Corner < 3; ++Corner) {
					const int32 Vertex = ReadInt(Indices, BaseIndex + Triangle * 3 + Corner);

					SkeletalMeshImportData::FVertex& Wedge = ImportData.Wedges.AddDefaulted_GetRef();

					Wedge.VertexIndex = Vertex;
					Wedge.MatIndex = static_cast<uint8>(SectionIndex);
					Wedge.Color = VertexColors.IsValidIndex(Vertex) ? VertexColors[Vertex] : FColor::White;

					for (int32 UV = 0; UV < NumTexCoords; ++UV) {
						const int32 Offset = Vertex * NumTexCoords + UV;

						Wedge.UVs[UV] = VertexUVs.IsValidIndex(Offset) ? VertexUVs[Offset] : FVector2f::ZeroVector;
					}

					Face.WedgeIndex[Corner] = ImportData.Wedges.Num() - 1;

					Face.TangentZ[Corner] = VertexNormals.IsValidIndex(Vertex) ? VertexNormals[Vertex] : GMeshUpAxis;
					Face.TangentX[Corner] = VertexTangents.IsValidIndex(Vertex) ? VertexTangents[Vertex] : GMeshForwardAxis;
					Face.TangentY[Corner] = VertexBinormals.IsValidIndex(Vertex)
						? VertexBinormals[Vertex]
						: FVector3f::CrossProduct(Face.TangentZ[Corner], Face.TangentX[Corner]);
				}
			}
		}

		ImportData.Influences.Empty(VertexCount * BonesPerVertex);

		TArray<TArray<int32>> SectionBoneMaps;
		SectionBoneMaps.SetNum(FMath::Max(SectionsJson->Num(), 1));

		for (int32 SectionIndex = 0; SectionIndex < SectionsJson->Num(); ++SectionIndex) {
			const TSharedPtr<FJsonObject> SectionJson = (*SectionsJson)[SectionIndex]->AsObject();
			if (!SectionJson.IsValid()) continue;

			const TArray<TSharedPtr<FJsonValue>>* BoneMap = nullptr;
			if (!SectionJson->TryGetArrayField(TEXT("BoneMap"), BoneMap)) continue;

			for (const TSharedPtr<FJsonValue>& BoneName : *BoneMap) {
				SectionBoneMaps[SectionIndex].Add(RefSkeleton.FindBoneIndex(FName(*BoneName->AsString())));
			}
		}

		for (int32 Vertex = 0; Vertex < VertexCount; ++Vertex) {
			const TArray<int32>& BoneMap = SectionBoneMaps[VertexSection.IsValidIndex(Vertex) ? VertexSection[Vertex] : 0];

			for (int32 Influence = 0; Influence < BonesPerVertex; ++Influence) {
				const int32 Offset = Vertex * BonesPerVertex + Influence;

				const float Weight = ReadFloat(Weights, Offset);
				if (Weight <= 0.0f) continue;

				const int32 BoneMapIndex = ReadInt(Bones, Offset);
				if (!BoneMap.IsValidIndex(BoneMapIndex) || BoneMap[BoneMapIndex] == INDEX_NONE) continue;

				SkeletalMeshImportData::FRawBoneInfluence& RawInfluence = ImportData.Influences.AddDefaulted_GetRef();

				RawInfluence.VertexIndex = Vertex;
				RawInfluence.BoneIndex = BoneMap[BoneMapIndex];
				RawInfluence.Weight = Weight;
			}
		}

#if !UE4_27_AND_UE5
#if UE4_24_BELOW
		ImportedModel->LODModels[LodIndex].RawSkeletalMeshBulkData.SaveRawMesh(ImportData);
#else
		SkeletalMesh->SaveLODImportedData(LodIndex, ImportData);
#endif

		TArray<FVector> LodPoints;
		TArray<SkeletalMeshImportData::FMeshWedge> LodWedges;
		TArray<SkeletalMeshImportData::FMeshFace> LodFaces;
		TArray<SkeletalMeshImportData::FVertInfluence> LodInfluences;
		TArray<int32> LodPointToRawMap;

		ImportData.CopyLODImportData(LodPoints, LodWedges, LodFaces, LodInfluences, LodPointToRawMap);

		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

		IMeshUtilities::MeshBuildOptions BuildOptions;

		BuildOptions.bComputeNormals = false;
		BuildOptions.bComputeTangents = false;
		BuildOptions.bRemoveDegenerateTriangles = false;

		if (!MeshUtilities.BuildSkeletalMesh(
			ImportedModel->LODModels[LodIndex],
#if !UE4_25_BELOW
			SkeletalMesh->GetName(),
#endif
			RefSkeleton, LodInfluences, LodWedges, LodFaces, LodPoints, LodPointToRawMap, BuildOptions)) {
			continue;
		}

		ImportedModel->LODModels[LodIndex].NumTexCoords = FMath::Max(1, NumTexCoords);
#else
		SkeletalMesh->SaveLODImportedData(LodIndex, ImportData);

		SkeletalMesh->SetLODImportedDataVersions(
			LodIndex,
			ESkeletalMeshGeoImportVersions::LatestVersion,
			ESkeletalMeshSkinningImportVersions::LatestVersion
		);

		if (FSkeletalMeshLODInfo* LodInfo = SkeletalMesh->GetLODInfo(LodIndex)) {
			LodInfo->bHasBeenSimplified = false;
			LodInfo->ReductionSettings.NumOfTrianglesPercentage = 1.0f;
			LodInfo->ReductionSettings.NumOfVertPercentage = 1.0f;

			LodInfo->BuildSettings.bRecomputeNormals = false;
			LodInfo->BuildSettings.bRecomputeTangents = false;

			LodInfo->BuildSettings.bRemoveDegenerates = false;
		}

#endif

		if (ImportData.bHasVertexColors) {
			SetMeshHasVertexColors(SkeletalMesh, true);
		}

		RebuiltLods++;
	}

	while (SkeletalMesh->GetLODNum() > Lods->Num()) {
		SkeletalMesh->RemoveLODInfo(SkeletalMesh->GetLODNum() - 1);
	}

	if (RebuiltLods > 0) {
		InvalidateSkeletalMeshDerivedData(SkeletalMesh);
	}

	SkeletalMesh->MarkPackageDirty();
	}

	return RebuiltLods;
}

void TMeshGeometry::Process(UObject* Object, const TArray<TSharedPtr<FJsonValue>>& Exports) {
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object);
	if (SkeletalMesh == nullptr) return;

	const TSharedPtr<FJsonObject> Response = Cloud::Export::GetLodModelBlocking(SkeletalMesh->GetPathName());
	if (!Response.IsValid()) return;

	const int32 RebuiltLods = RebuildLodModels(SkeletalMesh, Response);

	BrowseToWhenFinished(SkeletalMesh);

	AppendNotification(
		FText::FromString("Reflected Mesh Geometry: " + SkeletalMesh->GetName()),
		FText::FromString(FString::Printf(TEXT("%d LOD(s) rebuilt"), RebuiltLods)),
		3.5f,
		FAppStyle::Get().GetBrush("PhysicsAssetEditor.EnableCollision"),
		RebuiltLods > 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail,
		false,
		310.0f
	);
}
