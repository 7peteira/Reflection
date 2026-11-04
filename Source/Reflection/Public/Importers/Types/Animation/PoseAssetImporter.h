#pragma once

#include "Importers/Constructor/Importer.h"

class IPoseAssetImporter final : public IImporter {
public:
	UPoseAsset* PoseAsset;

	virtual UObject* CreateAsset(UObject* CreatedAsset) override;
	virtual bool Import() override;

protected:
	void ReverseCookLocalSpacePose(USkeleton* Skeleton);
	static UAnimSequence* CreateAnimSequenceFromPose(USkeleton* Skeleton, const FString& SequenceName, const TSharedPtr<FJsonObject>& PoseContainer, UPackage* Outer);

	TMap<FName, FTransform> GetSourceMeshBindPose();
	TMap<FName, FTransform> GetCookedReferencePose() const;
};

REGISTER_IMPORTER(IPoseAssetImporter, TArray<FString>{ 
	TEXT("PoseAsset")
}, TEXT("Animation Assets"));
