#pragma once

#include "CoreMinimal.h"
#include "Modules/Tools/SelectedAssetsBase.h"

class UClothingAssetCommon;

class REFLECTION_API TClothingData : public TSelectedAssetsBase {
public:
	virtual void Process(UObject* Object, const TArray<TSharedPtr<FJsonValue>>& Exports) override;

	virtual FName GetSupportedClass() const override { return FName("SkeletalMesh"); }

	virtual FText GetDisplayName() const override { return FText::FromString("Clothing Assets"); }
	virtual FText GetTooltip() const override { return FText::FromString("Reflects cloth data onto the selected mesh, and binds it to its sections"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), "PhysicsAssetEditor.EnableCollision.Small"); }

protected:
	static void RebuildParameterMasks(UClothingAssetCommon* ClothingAsset);
};

REGISTER_TOOL(TClothingData)
