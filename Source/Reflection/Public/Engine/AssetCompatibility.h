#pragma once

#include "Engine/Compatibility.h"

#include "Animation/AnimSequence.h"
#if UE4_25_BELOW
#include "AssetData.h"
#else
#include "AssetRegistry/AssetData.h"
#endif
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ScopedTransaction.h"

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 2
#include "Animation/AnimData/IAnimationDataController.h"
#endif

inline const FReferenceSkeleton& MeshRefSkeleton(const USkeletalMesh* Mesh) {
#if UE4_27_AND_UE5
	return Mesh->GetRefSkeleton();
#else
	return Mesh->RefSkeleton;
#endif
}

#if UE4_23_BELOW
class FScopedSkeletalMeshPostEditChange {
public:
	explicit FScopedSkeletalMeshPostEditChange(USkeletalMesh* InMesh) : Mesh(InMesh) {}

	~FScopedSkeletalMeshPostEditChange() {
		if (Mesh != nullptr) Mesh->PostEditChange();
	}

private:
	USkeletalMesh* Mesh;
};
#endif

inline void InvalidateSkeletalMeshDerivedData(USkeletalMesh* Mesh) {
	if (Mesh == nullptr) return;

#if UE4_23_BELOW
	if (FSkeletalMeshModel* Model = Mesh->GetImportedModel()) {
		Model->SkeletalMeshModelGUID = FGuid::NewGuid();
		Model->bGuidIsHash = false;
	}
#else
	Mesh->InvalidateDeriveDataCacheGUID();
#endif
}

inline UAssetImportData* GetAssetImportData(USkeletalMesh* InMesh) {
#if UE4_27_AND_UE5
	return InMesh->GetAssetImportData();
#else
	return InMesh->AssetImportData;
#endif
}

inline void SetAssetImportData(USkeletalMesh* InMesh, UAssetImportData* AssetImportData) {
#if UE4_27_AND_UE5
	InMesh->SetAssetImportData(AssetImportData);
#else
	InMesh->AssetImportData = AssetImportData;
#endif
}

inline FName GetAssetDataClass(const FAssetData& AssetData) {
#if UE5_1_BEYOND
	return AssetData.AssetClassPath.GetAssetName();
#else
	return AssetData.AssetClass;
#endif
}

inline FString GetAssetObjectPath(const FAssetData& AssetData) {
#if UE5_1_BEYOND
	return AssetData.GetObjectPathString();
#else
	return AssetData.ObjectPath.ToString();
#endif
}

inline void BuildAnimNotifyTracks(UAnimSequenceBase* Sequence) {
#if WITH_EDITORONLY_DATA
	if (!Sequence) {
		return;
	}

	int32 RowCount = Sequence->Notifies.Num() > 0 ? 1 : 0;

	for (FAnimNotifyEvent& Notify : Sequence->Notifies) {
		if (Notify.TrackIndex < 0 || Notify.TrackIndex > 20) {
			Notify.TrackIndex = 0;
		}

		RowCount = FMath::Max(RowCount, Notify.TrackIndex + 1);
	}

	if (const UAnimSequence* Marked = Cast<UAnimSequence>(Sequence)) {
		for (const FAnimSyncMarker& Marker : Marked->AuthoredSyncMarkers) {
			if (Marker.TrackIndex >= 0 && Marker.TrackIndex <= 20) {
				RowCount = FMath::Max(RowCount, Marker.TrackIndex + 1);
			}
		}
	}

	while (Sequence->AnimNotifyTracks.Num() < RowCount) {
		Sequence->AnimNotifyTracks.Add(FAnimNotifyTrack(
			*FString::FromInt(Sequence->AnimNotifyTracks.Num() + 1),
			FLinearColor::White
		));
	}

	if (Sequence->AnimNotifyTracks.Num() > RowCount) {
		Sequence->AnimNotifyTracks.SetNum(RowCount);
	}
#endif
}

inline void SetAnimSequenceLength(UAnimSequenceBase* Sequence, const float NewLength) {
	if (!Sequence || NewLength <= 0.f) {
		return;
	}

	const float OldLength = Sequence->GetPlayLength();
	if (FMath::IsNearlyEqual(OldLength, NewLength)) {
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("Change Sequence Length %.3f to %.3f"), OldLength, NewLength)));

	Sequence->Modify();
#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 2
	Sequence->GetController().SetNumberOfFrames(Sequence->GetController().ConvertSecondsToFrameNumber(NewLength), true);
#else
	if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(Sequence)) {
		Sequence->SequenceLength = NewLength;
#if ENGINE_UE4
		AnimSequence->PostProcessSequence();
#endif
	}
#endif

	Sequence->PostEditChange();
	Sequence->MarkPackageDirty();
}
