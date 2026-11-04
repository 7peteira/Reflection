#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Mesh/SkeletalMeshImporter.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Engine/AssetCompatibility.h"
#include "Engine/AssetUserData.h"

#if REFLECTION_RIG_LOGIC
#include "Animation/PoseAsset.h"
#endif

#include "Animation/PoseAsset.h"
#include "Engine/EngineUtilities.h"
#include "Utilities/JsonHelpers.h"

#include "Modules/Cloud/Cloud.h"
#include "Modules/Cloud/Tools/MeshGeometry.h"
#include "Modules/Cloud/Tools/SkeletalMeshData.h"
#include "Modules/Cloud/Tools/ClothingData.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"

#if UE5_1_BEYOND
#include "Engine/SkinnedAssetCommon.h"
#endif

namespace {
#if UE4_27_AND_UE5
	void SetMeshRefSkeleton(USkeletalMesh* Mesh, const FReferenceSkeleton& Value) { Mesh->SetRefSkeleton(Value); }
	void SetMeshMaterials(USkeletalMesh* Mesh, const TArray<FSkeletalMaterial>& Value) { Mesh->SetMaterials(Value); }
	void SetMeshSkeleton(USkeletalMesh* Mesh, USkeleton* Value) { Mesh->SetSkeleton(Value); }
	void SetMeshMinLod(USkeletalMesh* Mesh, const FPerPlatformInt& Value) { Mesh->SetMinLod(Value); }
	decltype(auto) MeshMorphTargets(const USkeletalMesh* Mesh) { return Mesh->GetMorphTargets(); }
#else
	void SetMeshRefSkeleton(USkeletalMesh* Mesh, const FReferenceSkeleton& Value) { Mesh->RefSkeleton = Value; }
	void SetMeshMaterials(USkeletalMesh* Mesh, const TArray<FSkeletalMaterial>& Value) { Mesh->Materials = Value; }
	void SetMeshSkeleton(USkeletalMesh* Mesh, USkeleton* Value) { Mesh->Skeleton = Value; }
	void SetMeshMinLod(USkeletalMesh* Mesh, const FPerPlatformInt& Value) { Mesh->MinLod = Value; }
	const TArray<UMorphTarget*>& MeshMorphTargets(const USkeletalMesh* Mesh) { return Mesh->MorphTargets; }
#endif

#if UE4_24_BELOW
	FSkeletalMeshLODModel* MeshLodModel(const USkeletalMesh* Mesh, const int32 Lod) {
		FSkeletalMeshModel* Model = Mesh->GetImportedModel();

		return Model != nullptr && Model->LODModels.IsValidIndex(Lod) ? &Model->LODModels[Lod] : nullptr;
	}

	bool IsMeshLodImportedDataEmpty(const USkeletalMesh* Mesh, const int32 Lod) {
		const FSkeletalMeshLODModel* LodModel = MeshLodModel(Mesh, Lod);

		return LodModel == nullptr || LodModel->RawSkeletalMeshBulkData.IsEmpty();
	}

	void LoadMeshLodImportedData(const USkeletalMesh* Mesh, const int32 Lod, FSkeletalMeshImportData& Out) {
		if (FSkeletalMeshLODModel* LodModel = MeshLodModel(Mesh, Lod)) LodModel->RawSkeletalMeshBulkData.LoadRawMesh(Out);
	}

	void SaveMeshLodImportedData(const USkeletalMesh* Mesh, const int32 Lod, FSkeletalMeshImportData& In) {
		if (FSkeletalMeshLODModel* LodModel = MeshLodModel(Mesh, Lod)) LodModel->RawSkeletalMeshBulkData.SaveRawMesh(In);
	}
#else
	bool IsMeshLodImportedDataEmpty(const USkeletalMesh* Mesh, const int32 Lod) { return Mesh->IsLODImportedDataEmpty(Lod); }
	void LoadMeshLodImportedData(const USkeletalMesh* Mesh, const int32 Lod, FSkeletalMeshImportData& Out) { Mesh->LoadLODImportedData(Lod, Out); }
	void SaveMeshLodImportedData(USkeletalMesh* Mesh, const int32 Lod, FSkeletalMeshImportData& In) { Mesh->SaveLODImportedData(Lod, In); }
#endif
}

UObject* ISkeletalMeshImporter::CreateAsset(UObject* CreatedAsset) {
	if (UObject* Existing = StaticFindObjectFast(USkeletalMesh::StaticClass(), GetPackage(), StringToName(GetAssetName()))) {
		Existing->Rename(
			nullptr,
			GetTransientPackage(),
			REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional
		);
	}

	return IImporter::CreateAsset(NewObject<USkeletalMesh>(GetPackage(), USkeletalMesh::StaticClass(), *GetAssetName(), RF_Public | RF_Standalone));
}

void ISkeletalMeshImporter::Abandon(USkeletalMesh* SkeletalMesh) {
	if (SkeletalMesh == nullptr) return;

	SetMeshRefSkeleton(SkeletalMesh, FReferenceSkeleton());
	SkeletalMesh->CalculateInvRefMatrices();

	SkeletalMesh->GetLODInfoArray().Empty();

	SkeletalMesh->ClearFlags(RF_Public | RF_Standalone);

	SetAsset(nullptr);
}

USkeleton* ISkeletalMeshImporter::BuildSkeletonFromMesh(USkeletalMesh* SkeletalMesh) const {
	if (SkeletalMesh == nullptr || MeshRefSkeleton(SkeletalMesh).GetNum() == 0) return nullptr;

	const FString SkeletonName = GetAssetName() + TEXT("_Skeleton");
	const FString Folder = FPackageName::GetLongPackagePath(GetPackage()->GetName());

	UPackage* SkeletonPackage = FAssetUtilities::CreateAssetPackage(Folder / SkeletonName);
	if (SkeletonPackage == nullptr) return nullptr;

	SkeletonPackage->FullyLoad();

	USkeleton* Skeleton = NewObject<USkeleton>(SkeletonPackage, StringToName(SkeletonName), RF_Public | RF_Standalone);

	if (!Skeleton->MergeAllBonesToBoneTree(SkeletalMesh)) return nullptr;

	HandleAssetCreation(Skeleton, SkeletonPackage);

	if (GetSettings()->AssetSettings.SaveAssets) {
		SavePackage(SkeletonPackage);
	}

	UE_LOG(LogReflection, Display, TEXT("\"%s\" named no skeleton, so \"%s\" was made from its %d bone(s)"), *GetAssetName(), *SkeletonName, MeshRefSkeleton(SkeletalMesh).GetNum());

	return Skeleton;
}

USkeleton* ISkeletalMeshImporter::ResolveSkeleton() {
	const FUObjectJsonValueExport SkeletonReference = GetAssetDataAsValue().GetObject(TEXT("Skeleton"));

	if (!SkeletonReference.JsonObject.IsValid() || !SkeletonReference.Has(TEXT("ObjectName"))) {
		return nullptr;
	}

	TObjectPtr<USkeleton> Skeleton;
	LoadExport<USkeleton>(&SkeletonReference.JsonObject, Skeleton);

	return Skeleton.Get();
}

void ISkeletalMeshImporter::BuildMaterialSlots(USkeletalMesh* SkeletalMesh, const TArray<TSharedPtr<FJsonValue>>& Slots) {
	TArray<FSkeletalMaterial> Materials;
	Materials.Reserve(Slots.Num());

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex) {
		const TSharedPtr<FJsonObject> Slot = Slots[SlotIndex].IsValid() ? Slots[SlotIndex]->AsObject() : nullptr;

		FString SlotName;

		if (Slot.IsValid()) {
			Slot->TryGetStringField(TEXT("MaterialSlotName"), SlotName);
		}

		if (SlotName.IsEmpty() || SlotName == TEXT("None")) {
			SlotName = FString::Printf(TEXT("Material_%d"), SlotIndex);
		}

		FSkeletalMaterial& Material = Materials.AddDefaulted_GetRef();

		Material.MaterialSlotName = FName(*SlotName);
		Material.ImportedMaterialSlotName = Material.MaterialSlotName;
	}

	SetMeshMaterials(SkeletalMesh, Materials);
}

int32 ISkeletalMeshImporter::BuildMorphTargets(USkeletalMesh* SkeletalMesh, const TSharedPtr<FJsonObject>& Payload) {
	const TArray<TSharedPtr<FJsonValue>>* Morphs = nullptr;

	if (!Payload.IsValid() || !Payload->TryGetArrayField(TEXT("morphs"), Morphs)) {
		return 0;
	}

	const FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
	if (ImportedModel == nullptr) return 0;

	TSet<FString> Written;

#if UE4_23_BELOW
	bool bRegistered = false;
#endif

	for (int32 LodIndex = 0; LodIndex < ImportedModel->LODModels.Num(); ++LodIndex) {
#if UE5_4_BEYOND
		if (!SkeletalMesh->HasMeshDescription(LodIndex)) continue;
#else
		if (IsMeshLodImportedDataEmpty(SkeletalMesh, LodIndex)) continue;
#endif

		FSkeletalMeshImportData ImportData;
		LoadMeshLodImportedData(SkeletalMesh, LodIndex, ImportData);

		if (ImportData.Points.Num() == 0) continue;

#if UE4_23_BELOW
		const FSkeletalMeshLODModel& LodModel = ImportedModel->LODModels[LodIndex];

		TMultiMap<int32, int32> PointVertices;

		for (int32 Vertex = 0; Vertex < LodModel.MeshToImportVertexMap.Num(); ++Vertex) {
			const int32 Point = LodModel.MeshToImportVertexMap[Vertex];

			if (Point >= 0) PointVertices.Add(Point, Vertex);
		}

		if (PointVertices.Num() == 0) continue;
#else
		ImportData.MorphTargetNames.Empty();
		ImportData.MorphTargets.Empty();
		ImportData.MorphTargetModifiedPoints.Empty();
#endif

		for (const TSharedPtr<FJsonValue>& MorphValue : *Morphs) {
			const TSharedPtr<FJsonObject> Morph = MorphValue.IsValid() ? MorphValue->AsObject() : nullptr;
			if (!Morph.IsValid()) continue;

			FString Name;
			const TArray<TSharedPtr<FJsonValue>>* Lods;

			if (!Morph->TryGetStringField(TEXT("Name"), Name) || Name.IsEmpty()) continue;
			if (!Morph->TryGetArrayField(TEXT("Lods"), Lods)) continue;

			for (const TSharedPtr<FJsonValue>& LodValue : *Lods) {
				const TSharedPtr<FJsonObject> Lod = LodValue.IsValid() ? LodValue->AsObject() : nullptr;
				if (!Lod.IsValid()) continue;
				if (Lod->GetIntegerField(TEXT("Index")) != LodIndex) continue;

				const TArray<TSharedPtr<FJsonValue>>* SourceIndices;
				const TArray<TSharedPtr<FJsonValue>>* PositionDeltas;

				if (!Lod->TryGetArrayField(TEXT("SourceIndices"), SourceIndices)) continue;
				if (!Lod->TryGetArrayField(TEXT("PositionDeltas"), PositionDeltas)) continue;

#if UE4_23_BELOW
				TArray<FMorphTargetDelta> Deltas;
				TSet<uint32> ModifiedPoints;

				Deltas.Reserve(SourceIndices->Num());
				ModifiedPoints.Reserve(SourceIndices->Num());

				TArray<int32> Vertices;

				for (int32 Delta = 0; Delta < SourceIndices->Num(); ++Delta) {
					const int32 Point = static_cast<int32>((*SourceIndices)[Delta]->AsNumber());

					if (!ImportData.Points.IsValidIndex(Point)) continue;
					if (ModifiedPoints.Contains(static_cast<uint32>(Point))) continue;

					ModifiedPoints.Add(static_cast<uint32>(Point));

					const FVector Moved(
						ReadFloat(PositionDeltas, Delta * 3),
						ReadFloat(PositionDeltas, Delta * 3 + 1),
						ReadFloat(PositionDeltas, Delta * 3 + 2)
					);

					Vertices.Reset();
					PointVertices.MultiFind(Point, Vertices);

					for (const int32 Vertex : Vertices) {
						FMorphTargetDelta& One = Deltas.AddDefaulted_GetRef();

						One.PositionDelta = Moved;
						One.TangentZDelta = FVector::ZeroVector;
						One.SourceIdx = static_cast<uint32>(Vertex);
					}
				}

				if (Deltas.Num() == 0) continue;

				UMorphTarget* MorphTarget = FindObject<UMorphTarget>(SkeletalMesh, *Name);

				if (MorphTarget == nullptr) {
					MorphTarget = NewObject<UMorphTarget>(SkeletalMesh, FName(*Name));
				}

				MorphTarget->PopulateDeltas(Deltas, LodIndex, LodModel.Sections);

				if (!MorphTarget->HasValidData()) continue;

				bRegistered |= SkeletalMesh->RegisterMorphTarget(MorphTarget, false);
#else
				FSkeletalMeshImportData Shape;
				TSet<uint32> ModifiedPoints;

				Shape.Points.Reserve(SourceIndices->Num());
				ModifiedPoints.Reserve(SourceIndices->Num());

				for (int32 Delta = 0; Delta < SourceIndices->Num(); ++Delta) {
					const int32 Point = static_cast<int32>((*SourceIndices)[Delta]->AsNumber());

					if (!ImportData.Points.IsValidIndex(Point)) continue;
					if (ModifiedPoints.Contains(static_cast<uint32>(Point))) continue;

					ModifiedPoints.Add(static_cast<uint32>(Point));

					Shape.Points.Add(ImportData.Points[Point] + FVector3f(
						ReadFloat(PositionDeltas, Delta * 3),
						ReadFloat(PositionDeltas, Delta * 3 + 1),
						ReadFloat(PositionDeltas, Delta * 3 + 2)
					));
				}

				if (Shape.Points.Num() == 0) continue;

				ImportData.MorphTargetNames.Add(Name);
				ImportData.MorphTargets.Add(MoveTemp(Shape));
				ImportData.MorphTargetModifiedPoints.Add(MoveTemp(ModifiedPoints));
#endif

				Written.Add(Name);
			}
		}

#if !UE4_23_BELOW
		SaveMeshLodImportedData(SkeletalMesh, LodIndex, ImportData);
#endif
	}

#if UE4_23_BELOW
	if (bRegistered) SkeletalMesh->InitMorphTargets();
#endif

	return Written.Num();
}

bool ISkeletalMeshImporter::ApplyCookedBindPose(USkeletalMesh* SkeletalMesh, USkeleton* Skeleton, const FString& FetchPath) {
	const TSharedPtr<FJsonObject> Payload = Cloud::Export::GetReferenceSkeletonBlocking(FetchPath);

	const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;

	if (!Payload.IsValid() || !Payload->TryGetArrayField(TEXT("bones"), Bones) || Bones->Num() == 0) {
		return false;
	}

	FReferenceSkeleton BindPose;

	{
		FReferenceSkeletonModifier Modifier(BindPose, Skeleton);

		const auto ReadVector = [](const TArray<TSharedPtr<FJsonValue>>* Values, const int32 Offset) {
			return FVector(
				Values != nullptr && Values->IsValidIndex(Offset) ? (*Values)[Offset]->AsNumber() : 0.0,
				Values != nullptr && Values->IsValidIndex(Offset + 1) ? (*Values)[Offset + 1]->AsNumber() : 0.0,
				Values != nullptr && Values->IsValidIndex(Offset + 2) ? (*Values)[Offset + 2]->AsNumber() : 0.0
			);
		};

		for (const TSharedPtr<FJsonValue>& Value : *Bones) {
			const TSharedPtr<FJsonObject> Bone = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Bone.IsValid()) continue;

			FString Name;
			if (!Bone->TryGetStringField(TEXT("Name"), Name)) return false;

			int32 ParentIndex = INDEX_NONE;
			Bone->TryGetNumberField(TEXT("ParentIndex"), ParentIndex);

			const TArray<TSharedPtr<FJsonValue>>* Translation = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Rotation = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Scale = nullptr;

			Bone->TryGetArrayField(TEXT("Translation"), Translation);
			Bone->TryGetArrayField(TEXT("Rotation"), Rotation);
			Bone->TryGetArrayField(TEXT("Scale"), Scale);

			const FQuat BoneRotation(
				Rotation != nullptr && Rotation->IsValidIndex(0) ? (*Rotation)[0]->AsNumber() : 0.0,
				Rotation != nullptr && Rotation->IsValidIndex(1) ? (*Rotation)[1]->AsNumber() : 0.0,
				Rotation != nullptr && Rotation->IsValidIndex(2) ? (*Rotation)[2]->AsNumber() : 0.0,
				Rotation != nullptr && Rotation->IsValidIndex(3) ? (*Rotation)[3]->AsNumber() : 1.0
			);

			const FTransform Pose(BoneRotation.GetNormalized(), ReadVector(Translation, 0), ReadVector(Scale, 0));

			Modifier.Add(FMeshBoneInfo(FName(*Name), Name, ParentIndex), Pose);
		}
	}

	if (BindPose.GetNum() == 0) return false;

	SetMeshRefSkeleton(SkeletalMesh, BindPose);

	UE_LOG(LogReflection, Display, TEXT("\"%s\" bound at its own pose, %d bone(s)"), *GetAssetName(), BindPose.GetNum());

	return true;
}

bool ISkeletalMeshImporter::Import() {
	USkeleton* Skeleton = ResolveSkeleton();

	FString FetchPath = GetAssetExport()->HasField(TEXT("Package"))
		? GetAssetExport()->GetStringField(TEXT("Package"))
		: FString();

	if (FetchPath.IsEmpty()) {
		FetchPath = GetPackage()->GetPathName();

		FRRedirects::Reverse(FetchPath);
	}

	const TSharedPtr<FJsonObject> Geometry = Cloud::Export::GetLodModelBlocking(FetchPath);

	if (!Geometry.IsValid()) {
		FImportIssues::Report(
			EImportIssue::Failed,
			TEXT("No geometry from the Cloud"),
			TEXT("The Cloud has to be running, and the mesh needs cooked render data to read.")
		);

		return false;
	}

	USkeletalMesh* SkeletalMesh = Create<USkeletalMesh>();
	if (SkeletalMesh == nullptr) return false;

	if (Skeleton != nullptr) {
		SetMeshSkeleton(SkeletalMesh, Skeleton);
	}

	const bool bCookedBindPose = ApplyCookedBindPose(SkeletalMesh, Skeleton, FetchPath);

	if (!bCookedBindPose) {
		if (Skeleton == nullptr) {
			FImportIssues::Report(
				EImportIssue::Failed,
				TEXT("The mesh has no bones to skin to"),
				TEXT("The export names no skeleton, and the Cloud served no reference pose for the mesh either, so there is nothing to bind it against.")
			);

			Abandon(SkeletalMesh);

			return false;
		}

		SetMeshRefSkeleton(SkeletalMesh, Skeleton->GetReferenceSkeleton());
	}

	if (Skeleton == nullptr) {
		Skeleton = BuildSkeletonFromMesh(SkeletalMesh);

		if (Skeleton == nullptr) {
			FImportIssues::Report(
				EImportIssue::Failed,
				TEXT("A skeleton couldn't be made for this mesh"),
				TEXT("The export names none, so one is built out of the mesh's own bones, and the engine would not take them into a bone tree.")
			);

			Abandon(SkeletalMesh);

			return false;
		}

		SetMeshSkeleton(SkeletalMesh, Skeleton);
	}

	SkeletalMesh->CalculateInvRefMatrices();

	const TArray<TSharedPtr<FJsonValue>>* Slots;

	if (GetAssetExport()->TryGetArrayField(TEXT("SkeletalMaterials"), Slots)) {
		BuildMaterialSlots(SkeletalMesh, *Slots);
	}

	if (!GetAssetData()->HasField(TEXT("LODInfo"))) {
		const TArray<TSharedPtr<FJsonValue>>* Models;

		if (GetAssetData()->TryGetArrayField(TEXT("SourceModels"), Models)) {
			GetAssetData()->SetArrayField(TEXT("LODInfo"), *Models);
		}
	}

	GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(GetAssetData(), {
		"LODModels",
		"NaniteResources",
		"SamplingInfo",

		"SourceModels",

		"Skeleton",

		"Sockets",
		"MorphTargets",
		"SkinWeightProfiles",
	}), SkeletalMesh);

	const int32 BuiltLods = TMeshGeometry::RebuildLodModels(SkeletalMesh, Geometry);

	if (BuiltLods == 0) {
		FImportIssues::Report(
			EImportIssue::Failed,
			TEXT("The mesh has no LOD to build"),
			TEXT("Every LOD the payload carries was missing the vertices or the sections to describe it.")
		);

		Abandon(SkeletalMesh);

		return false;
	}

	const FUObjectJsonValueExport ImportedBounds = GetAssetAsValue().GetObject(TEXT("ImportedBounds"));

	if (ImportedBounds.JsonObject.IsValid() && ImportedBounds.Has(TEXT("SphereRadius"))) {
		auto ReadVector = [](const FUObjectJsonValueExport& Vector) {
			return FVector(
				Vector.Has(TEXT("X")) ? Vector.GetNumber(TEXT("X")) : 0.0,
				Vector.Has(TEXT("Y")) ? Vector.GetNumber(TEXT("Y")) : 0.0,
				Vector.Has(TEXT("Z")) ? Vector.GetNumber(TEXT("Z")) : 0.0
			);
		};

		FBoxSphereBounds Bounds;

		Bounds.Origin = ReadVector(ImportedBounds.GetObject(TEXT("Origin")));
		Bounds.BoxExtent = ReadVector(ImportedBounds.GetObject(TEXT("BoxExtent")));
		Bounds.SphereRadius = ImportedBounds.GetNumber(TEXT("SphereRadius"));

		SkeletalMesh->SetImportedBounds(Bounds);
	}

	const TArray<TSharedPtr<FJsonValue>>* ExportedMorphTargets;

	const bool bHasMorphTargets = GetAssetData()->TryGetArrayField(TEXT("MorphTargets"), ExportedMorphTargets)
		&& ExportedMorphTargets->Num() > 0;

	const int32 WrittenMorphs = bHasMorphTargets
		? BuildMorphTargets(SkeletalMesh, Cloud::Export::GetMorphTargetsBlocking(FetchPath))
		: 0;

	if (ExportNamesDna()) {
		const bool bAppliedDna = ApplyDna(SkeletalMesh, FetchPath);

		if (bAppliedDna) {
			if (!bCookedBindPose) {
				AlignBindPoseToDna(SkeletalMesh);
			}
		}

		bool bBakedPoses = false;

		if (GetModdingAssetSettings().MetaHuman.Bake == ERDnaBake::MorphTargets) {
			const ERDnaCurves Curves = GetModdingAssetSettings().MetaHuman.Curves;

			TArray<TSharedPtr<FJsonValue>> Faces;

			TSet<FString> Named;

			const auto Gather = [&Faces, &Named](const TSharedPtr<FJsonObject>& Answer) {
				const TArray<TSharedPtr<FJsonValue>>* Morphs = nullptr;

				if (!Answer.IsValid() || !Answer->TryGetArrayField(TEXT("morphs"), Morphs)) return false;

				for (const TSharedPtr<FJsonValue>& One : *Morphs) {
					const TSharedPtr<FJsonObject> Morph = One.IsValid() ? One->AsObject() : nullptr;

					FString Name;

					if (!Morph.IsValid() || !Morph->TryGetStringField(TEXT("Name"), Name) || Name.IsEmpty()) continue;

					if (Named.Contains(Name)) continue;

					Named.Add(Name);
					Faces.Add(One);
				}

				return true;
			};

			const bool bHadLegacy = !WantsLegacyCurves(Curves) || Gather(Cloud::Export::GetDnaMorphsBlocking(FetchPath, true));
			const bool bHadControls = !KeepsRigControls(Curves) || Gather(Cloud::Export::GetDnaMorphsBlocking(FetchPath, false));

			if (!bHadLegacy || !bHadControls) {
				FImportIssues::Report(
					EImportIssue::Data,
					TEXT("Only one set of the face's names came back"),
					FString::Printf(
						TEXT("Both were asked for and the %s didn't arrive, so the face is on the mesh under one set of names and anything driving it by the other finds nothing."),
						bHadLegacy ? TEXT("rig's own controls") : TEXT("older head's curves"))
				);
			}

			const TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetArrayField(TEXT("morphs"), Faces);

			const int32 Built = BuildMorphTargets(SkeletalMesh, Payload);

			bBakedPoses = Built > 0;

			UE_LOG(LogReflection, Display, TEXT("\"%s\" built %d morph target(s) out of its DNA"), *GetAssetName(), Built);
		}

		if (GetModdingAssetSettings().MetaHuman.Bake == ERDnaBake::PoseAsset) {
			const UPoseAsset* PoseAsset = bAppliedDna
				? BakeDnaPoseAsset(SkeletalMesh)
				: BakeDnaPoseAssetFromCloud(SkeletalMesh, FetchPath);

			bBakedPoses = PoseAsset != nullptr;

			if (bBakedPoses) {
				UE_LOG(LogReflection, Display, TEXT("\"%s\" baked %d pose(s) out of its DNA into \"%s\""),
					*GetAssetName(), PoseAsset->GetNumPoses(), *PoseAsset->GetName());
			}
		}

		if (!bAppliedDna && !bBakedPoses) {
#if REFLECTION_RIG_LOGIC
			FImportIssues::Report(
				EImportIssue::Data,
				TEXT("The mesh's DNA didn't come back"),
				TEXT("The export hangs a DNA off this mesh, so it is a rigged head. Without the stream the mesh imports, but nothing drives its face.")
			);
#else
			FImportIssues::Report(
				EImportIssue::MissingClass,
				TEXT("This engine has no RigLogic"),
				TEXT("The export hangs a DNA off this mesh, so it is a rigged head. RigLogic is what reads one and it ships with Unreal Engine 5, so the face has to arrive already worked out. Baking it to a pose asset is what does that, and it is off or did not run.")
			);
#endif
		}
	}

#if UE4_27_AND_UE5
	SkeletalMesh->Build();
#endif

	bool bBuilt = false;

	if (const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering()) {
		for (int32 Lod = 0; Lod < RenderData->LODRenderData.Num(); ++Lod) {
			const FSkeletalMeshLODRenderData& LodData = RenderData->LODRenderData[Lod];

			if (LodData.GetNumVertices() > 0 && LodData.RequiredBones.Num() > 0) {
				bBuilt = true;

				break;
			}
		}
	}

	if (!bBuilt) {
		FImportIssues::Report(
			EImportIssue::Failed,
			TEXT("The build made nothing out of the geometry"),
			TEXT("The vertices arrived, and the mesh the engine built out of them is empty. A LOD whose influences name no bone the skeleton has is the usual reason.")
		);

		UE_LOG(LogReflection, Error, TEXT("\"%s\" built %d LOD(s) from the Cloud and the engine made an empty mesh out of them"), *GetAssetName(), BuiltLods);

		Abandon(SkeletalMesh);

		return false;
	}

	if (bHasMorphTargets && WrittenMorphs < ExportedMorphTargets->Num()) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("Some of the mesh's morph targets came back empty"),
			FString::Printf(
				TEXT("%d of %d were rebuilt. A morph whose deltas the cook stripped has nothing left to move the mesh with."),
				WrittenMorphs,
				ExportedMorphTargets->Num()
			)
		);
	}

	TSkeletalMeshData MeshData;
	MeshData.Process(SkeletalMesh, GetContainer()->JsonObjects);

	if (GetAssetData()->HasField(TEXT("MeshClothingAssets"))) {
		TClothingData ClothingData;
		ClothingData.Process(SkeletalMesh, GetContainer()->JsonObjects);
	}

	if (GetSettings()->AssetSettings.Mesh.IgnoreMinQualityLevelLODDefault) {
		SetMeshMinLod(SkeletalMesh, FPerPlatformInt(0));

#if UE5_1_BEYOND
		FPerQualityLevelInt MinQualityLevelLod = SkeletalMesh->GetQualityLevelMinLod();

		MinQualityLevelLod.Default = 0;

		SkeletalMesh->SetQualityLevelMinLod(MinQualityLevelLod);
#endif
	}

	SkeletalMesh->CalculateInvRefMatrices();
	SkeletalMesh->PostEditChange();

	UE_LOG(LogReflection, Display, TEXT("\"%s\" built %d LOD(s) and %d morph target(s) against skeleton \"%s\""), *GetAssetName(), BuiltLods, MeshMorphTargets(SkeletalMesh).Num(), *Skeleton->GetName());

	return OnAssetCreation(SkeletalMesh);
}
