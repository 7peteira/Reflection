#pragma once

#include "CoreMinimal.h"
#include "Modules/Tools/SelectedAssetsBase.h"

class REFLECTION_API TMeshGeometry : public TSelectedAssetsBase {
public:
	virtual void Process(UObject* Object, const TArray<TSharedPtr<FJsonValue>>& Exports) override;

	virtual FName GetSupportedClass() const override { return FName("SkeletalMesh"); }

	virtual FText GetDisplayName() const override { return FText::FromString("Mesh Geometry"); }
	virtual FText GetTooltip() const override { return FText::FromString("Rebuilds the selected mesh's LOD models from the cooked geometry"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.SkeletalMeshComponent"); }

	static int32 RebuildLodModels(USkeletalMesh* SkeletalMesh, const TSharedPtr<FJsonObject>& Response);
};

REGISTER_TOOL(TMeshGeometry)
