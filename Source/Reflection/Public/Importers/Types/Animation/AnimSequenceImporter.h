#pragma once

#include "Importers/Constructor/Importer.h"

class USkeleton;
class UAnimSequence;

class IAnimSequenceImporter : public IImporter {
public:
	virtual UObject* CreateAsset(UObject* CreatedAsset) override;
	virtual bool Import() override;

private:
	USkeleton* ResolveSkeleton();

	int32 BuildTracks(UAnimSequence* AnimSequence, USkeleton* Skeleton, const TSharedPtr<FJsonObject>& Payload) const;
};

REGISTER_IMPORTER_OVERRIDE(IAnimSequenceImporter, (TArray<FString>{
	TEXT("AnimSequence")
}), TEXT("Animation Assets"));
