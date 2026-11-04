#pragma once

#include "CoreMinimal.h"
#include "MaterialModdingSettings.generated.h"

USTRUCT()
struct FRMaterialModdingSettings {
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, DisplayName = "Stubs", Config, Category = MaterialSettings)
	bool Stubs = false;
};
