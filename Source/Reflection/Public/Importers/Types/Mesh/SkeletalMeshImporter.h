#pragma once

#include "Importers/Constructor/Importer.h"

class USkeletalMesh;
class USkeleton;
class UPoseAsset;

class ISkeletalMeshImporter : public IImporter {
public:
	virtual UObject* CreateAsset(UObject* CreatedAsset) override;
	virtual bool Import() override;

private:
	void Abandon(USkeletalMesh* SkeletalMesh);

	USkeleton* ResolveSkeleton();

	USkeleton* BuildSkeletonFromMesh(USkeletalMesh* SkeletalMesh) const;

	static void BuildMaterialSlots(USkeletalMesh* SkeletalMesh, const TArray<TSharedPtr<FJsonValue>>& Slots);

	static int32 BuildMorphTargets(USkeletalMesh* SkeletalMesh, const TSharedPtr<FJsonObject>& Payload);

	bool ExportNamesDna();

	TArray<uint8> RewriteDnaForAnimNode(USkeletalMesh* SkeletalMesh, const TArray<uint8>& Dna);

	bool ApplyDna(USkeletalMesh* SkeletalMesh, const FString& FetchPath);

	bool ApplyCookedBindPose(USkeletalMesh* SkeletalMesh, USkeleton* Skeleton, const FString& FetchPath);

	bool AlignBindPoseToDna(USkeletalMesh* SkeletalMesh);

	bool BuildBackportedPosePlan(const TSharedPtr<class IDNAReader>& Behavior, TArray<struct FDnaPosePlan>& OutPlan);
	UPoseAsset* BakeDnaPoseAsset(USkeletalMesh* SkeletalMesh);

	UPoseAsset* BakeDnaPoseAssetFromCloud(USkeletalMesh* SkeletalMesh, const FString& FetchPath);
};

REGISTER_IMPORTER(ISkeletalMeshImporter, {
	"SkeletalMesh"
}, "Mesh Assets");
