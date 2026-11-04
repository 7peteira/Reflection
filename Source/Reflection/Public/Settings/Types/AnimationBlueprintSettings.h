#pragma once

#include "CoreMinimal.h"
#include "AnimationBlueprintSettings.generated.h"

USTRUCT()
struct FRAnimationBlueprintSettings {
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, DisplayName = "ID in Comments", Config, AdvancedDisplay, Category = AnimationBlueprintSettings)
	bool NodeIDComments = false;
};
