#pragma once

#include "CoreMinimal.h"
#include "LevelSettings.generated.h"

UENUM()
enum class ERLevelActors : uint8 {
	Everything UMETA(DisplayName = "Everything"),

	NoBlueprints UMETA(DisplayName = "Nothing a Blueprint Makes"),

	StaticMeshesOnly UMETA(DisplayName = "Static Mesh Actors Only"),

	HLODsOnly UMETA(DisplayName = "HLOD Actors Only"),

	Nothing UMETA(DisplayName = "Nothing")
};

USTRUCT()
struct FRLevelSettings {
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Config, Category = LevelSettings)
	ERLevelActors Actors = ERLevelActors::Everything;

	UPROPERTY(EditAnywhere, DisplayName = "Sub Levels", Config, Category = LevelSettings)
	bool SubLevels = true;

	UPROPERTY(EditAnywhere, DisplayName = "World Settings", Config, Category = LevelSettings)
	bool WorldSettings = true;

	UPROPERTY(EditAnywhere, DisplayName = "LOD Actors", Config, Category = LevelSettings)
	bool LODActors = true;
};
