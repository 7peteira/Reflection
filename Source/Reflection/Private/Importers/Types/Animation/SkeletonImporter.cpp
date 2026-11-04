#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Animation/SkeletonImporter.h"

#if ENGINE_UE4
#include "Utilities/JsonHelpers.h"
#endif

UObject* ISkeletonImporter::CreateAsset(UObject* CreatedAsset) {
	return IImporter::CreateAsset(NewObject<USkeleton>(GetPackage(), USkeleton::StaticClass(), *GetAssetName(), RF_Public | RF_Standalone));
}

bool ISkeletonImporter::Import() {
	USkeleton* Skeleton = GetSelectedAsset<USkeleton>(true);

	if (Skeleton) {
		if (Skeleton->GetName() != GetAssetName()) {
			Skeleton = nullptr;
		}
	}

	if (!Skeleton) {
		Skeleton = Create<USkeleton>();

		ApplySkeletalChanges(Skeleton);
	} else {
		Skeleton->Sockets.Empty();
		Skeleton->BlendProfiles.Empty();

		TArray<FName> VirtualBoneNames;

		for (const FVirtualBone VirtualBone : Skeleton->GetVirtualBones()) {
			VirtualBoneNames.Add(VirtualBone.VirtualBoneName);
		}

		Skeleton->RemoveVirtualBones(VirtualBoneNames);
		Skeleton->AnimRetargetSources.Empty();

		ApplySkeletalChanges(Skeleton);
	}

	DeserializeExports(Skeleton);
	GetObjectSerializer()->DeserializeObjectProperties(GetAssetData(), Skeleton);

	ApplySkeletalAssetData(Skeleton);

	return OnAssetCreation(Skeleton);
}

void ISkeletonImporter::DeserializeCurveMetaData(FCurveMetaData* OutMeta, const TSharedPtr<FJsonObject>& Json) const {
#if ENGINE_UE4
	if (const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr; Json->TryGetArrayField(TEXT("LinkedBones"), Bones)) {
		for (const auto& BoneVal : *Bones) {
			if (auto BoneObj = BoneVal->AsObject()) {
				FBoneReference Bone;
				GetPropertySerializer()->DeserializeStruct(TBaseStructure<FBoneReference>::Get(), BoneObj.ToSharedRef(), &Bone);
				OutMeta->LinkedBones.Add(MoveTemp(Bone));
			}
		}
	}

	OutMeta->MaxLOD = Json->GetNumberField(TEXT("MaxLOD"));

	if (const TSharedPtr<FJsonObject>* TypeObj; Json->TryGetObjectField(TEXT("Type"), TypeObj)) {
		FAnimCurveType& Type = OutMeta->Type;
		(*TypeObj)->TryGetBoolField(TEXT("bMaterial"), Type.bMaterial);
		(*TypeObj)->TryGetBoolField(TEXT("bMorphtarget"), Type.bMorphtarget);
	}
#endif
}

void ISkeletonImporter::ApplyModifications() {
	IImporter::ApplyModifications();

#if ENGINE_UE4
	const FUObjectExport* AnimCurveMetaData = GetContainer()->FindByType(FString("AnimCurveMetaData"));

	if (AnimCurveMetaData->IsJsonValid()) {
		const TSharedPtr<FJsonObject> CurveMetaDataProperties = AnimCurveMetaData->GetProperties();

		if (CurveMetaDataProperties->HasField(TEXT("CurveMetaData"))) {
			const TArray<TSharedPtr<FJsonValue>> CurveMetaData = CurveMetaDataProperties->GetArrayField(TEXT("CurveMetaData"));

			FJsonObject* NameMappings = EnsureObjectField(GetAssetData(), "NameMappings");
			FJsonObject* AnimationCurves = EnsureObjectField(NameMappings, "AnimationCurves");
			AnimationCurves->SetField("GuidMap", nullptr);
			AnimationCurves->SetField("UidMap", nullptr);

			FJsonObject* CurveMetaDataMap = EnsureObjectField(AnimationCurves, "CurveMetaDataMap");

			ProcessJsonArrayField(CurveMetaDataProperties, TEXT("CurveMetaData"), [&](const TSharedPtr<FJsonObject>& ObjectField) {
				const FString Key = ObjectField->GetStringField(TEXT("Key"));
				const TSharedPtr<FJsonObject> Value = ObjectField->GetObjectField(TEXT("Value"));

				CurveMetaDataMap->SetObjectField(Key, Value);
			});
		}
	}
#endif
}

void ISkeletonImporter::ApplySkeletalChanges(USkeleton* Skeleton) const {
	const TSharedPtr<FJsonObject> ReferenceSkeletonObject = GetAssetData()->GetObjectField(TEXT("ReferenceSkeleton"));

	FReferenceSkeleton& ReferenceSkeleton = const_cast<FReferenceSkeleton&>(Skeleton->GetReferenceSkeleton());
	FReferenceSkeletonModifier ReferenceSkeletonModifier(ReferenceSkeleton, Skeleton);

	TArray<TSharedPtr<FJsonValue>> FinalRefBoneInfo = ReferenceSkeletonObject->GetArrayField(TEXT("FinalRefBoneInfo"));
	TArray<TSharedPtr<FJsonValue>> FinalRefBonePose = ReferenceSkeletonObject->GetArrayField(TEXT("FinalRefBonePose"));

	TArray<FName> ExportBoneNames;

	for (const auto& FinalReferenceBoneInfoValue : FinalRefBoneInfo) {
		ExportBoneNames.Add(FName(*FinalReferenceBoneInfoValue->AsObject()->GetStringField(TEXT("Name"))));
	}

	for (int BoneIndex = 0; BoneIndex < FinalRefBoneInfo.Num(); BoneIndex++) {
		const TSharedPtr<FJsonObject> FinalReferenceBoneInfo = FinalRefBoneInfo[BoneIndex]->AsObject();

		const FName Name = ExportBoneNames[BoneIndex];
		const int ExportParentIndex = FinalReferenceBoneInfo->GetIntegerField(TEXT("ParentIndex"));

		if (ReferenceSkeleton.FindRawBoneIndex(Name) != INDEX_NONE) {
			continue;
		}

		if (!FinalRefBonePose.IsValidIndex(BoneIndex) || !FinalRefBonePose[BoneIndex].IsValid()) {
			continue;
		}

		int ParentIndex = INDEX_NONE;

		if (ExportBoneNames.IsValidIndex(ExportParentIndex)) {
			ParentIndex = ReferenceSkeleton.FindRawBoneIndex(ExportBoneNames[ExportParentIndex]);

			if (ParentIndex == INDEX_NONE) {
				continue;
			}
		}

		TSharedPtr<FJsonObject> BonePoseTransform = FinalRefBonePose[BoneIndex]->AsObject();
		FTransform Transform; {
			GetPropertySerializer()->DeserializeStruct(TBaseStructure<FTransform>::Get(), BonePoseTransform.ToSharedRef(), &Transform);
		}

		FMeshBoneInfo MeshBoneInfo = FMeshBoneInfo(Name, "", ParentIndex);

		ReferenceSkeletonModifier.Add(MeshBoneInfo, Transform);
	}

	ReferenceSkeleton.RebuildRefSkeleton(Skeleton, true);

	Skeleton->ClearCacheData();
	Skeleton->MarkPackageDirty();
}

void ISkeletonImporter::ApplySkeletalAssetData(USkeleton* Skeleton) const {
#if ENGINE_UE4
	if (GetAssetData()->HasField("NameMappings")
		&& GetAssetData()->GetObjectField("NameMappings")->Values.Num() > 0
		&& GetAssetData()->GetObjectField("NameMappings")->HasField(TEXT("AnimationCurves"))
		&& GetAssetData()->GetObjectField("NameMappings")->GetObjectField(TEXT("AnimationCurves"))->HasField(TEXT("CurveMetaDataMap"))) {

		TSharedPtr<FJsonObject> NameMappings = GetAssetData()->GetObjectField("NameMappings");
		TSharedPtr<FJsonObject> AnimationCurves = NameMappings->GetObjectField("AnimationCurves");
		TSharedPtr<FJsonObject> CurveMetaDataMap = AnimationCurves->GetObjectField("CurveMetaDataMap");

		ProcessObjects(CurveMetaDataMap, [&](const FString& Name, const TSharedPtr<FJsonObject>& Object) {
			FSmartName NewTrackName;

			Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, FName(*Name), NewTrackName);
			ensureAlways(Skeleton->GetSmartNameByUID(USkeleton::AnimCurveMappingName, NewTrackName.UID, NewTrackName));
			FCurveMetaData* CurveMetaData = Skeleton->GetCurveMetaData(FName(*Name));

			DeserializeCurveMetaData(CurveMetaData, Object);
		});
	}
#endif

	const TSharedPtr<FJsonObject> AnimRetargetSources = GetAssetData()->GetObjectField(TEXT("AnimRetargetSources"));

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : AnimRetargetSources->Values) {
		const FName KeyName = FName(*Pair.Key);
		const TSharedPtr<FJsonObject> RetargetObject = Pair.Value->AsObject();

		const FName PoseName(*RetargetObject->GetStringField(TEXT("PoseName")));

		TArray<FTransform> ReferencePose;

		TArray<TSharedPtr<FJsonValue>> ReferencePoseArray = RetargetObject->GetArrayField(TEXT("ReferencePose"));

		for (TSharedPtr<FJsonValue> ReferencePoseTransformValue : ReferencePoseArray) {
			TSharedPtr<FJsonObject> ReferencePoseObject = ReferencePoseTransformValue->AsObject();

			FTransform Transform; {
				GetPropertySerializer()->DeserializeStruct(TBaseStructure<FTransform>::Get(), ReferencePoseObject.ToSharedRef(), &Transform);
			}

			ReferencePose.Add(Transform);
		}

		FReferencePose RetargetSource;
		RetargetSource.ReferencePose = ReferencePose;
		RetargetSource.PoseName = PoseName;

		Skeleton->AnimRetargetSources.Add(KeyName, RetargetSource);
	}

	RebuildSkeleton(Skeleton);
}

void ISkeletonImporter::RebuildSkeleton(const USkeleton* Skeleton) {
	FReferenceSkeleton& ReferenceSkeleton = const_cast<FReferenceSkeleton&>(Skeleton->GetReferenceSkeleton());
	FReferenceSkeletonModifier ReferenceSkeletonModifier(ReferenceSkeleton, Skeleton);

	ReferenceSkeleton.RebuildRefSkeleton(Skeleton, true);
}
