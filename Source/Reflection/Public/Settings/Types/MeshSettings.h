#pragma once

#include "CoreMinimal.h"
#include "MeshSettings.generated.h"

USTRUCT()
struct FRMeshSettings {
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, DisplayName = "Show Best LOD", Config, Category = MeshSettings)
	bool IgnoreMinQualityLevelLODDefault = true;
};
