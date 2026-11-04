#pragma once

#include "Importers/Constructor/Importer.h"

#if ENGINE_UE4
class UBlendSpaceBase;

using UReflectionBlendSpace = UBlendSpaceBase;
#else
class UBlendSpace;

using UReflectionBlendSpace = UBlendSpace;
#endif

class IBlendSpaceImporter : public IImporter {
public:
	virtual UObject* CreateAsset(UObject* CreatedAsset) override;
	virtual bool Import() override;

private:
	void PreloadSampleAnimations();

	void ResolveEmptySamples(UReflectionBlendSpace* BlendSpace);

	void AlignGridToEngine(UReflectionBlendSpace* BlendSpace);
};

REGISTER_IMPORTER(IBlendSpaceImporter, (TArray<FString>{
	"BlendSpace",
	"BlendSpace1D",

	"AimOffsetBlendSpace",
	"AimOffsetBlendSpace1D"
}), TEXT("Animation Assets"));
