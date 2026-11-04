#pragma once

#include "CoreMinimal.h"
#include "TextureSettings.generated.h"

USTRUCT()
struct FRTextureSettings {
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Config, AdvancedDisplay, Category = TextureSettings)
	bool ReflectExistingTextures = false;
};
