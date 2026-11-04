#pragma once

#include "CoreMinimal.h"
#include "DNASettings.generated.h"

UENUM()
enum class ERDnaBake : uint8 {
	None UMETA(DisplayName = "None"),

	PoseAsset UMETA(DisplayName = "Pose Asset"),

	MorphTargets UMETA(DisplayName = "Morph Targets")
};

UENUM()
enum class ERDnaCurves : uint8 {
	Controls UMETA(DisplayName = "Rig Controls"),

	Legacy UMETA(DisplayName = "Legacy Head"),

	Both UMETA(DisplayName = "Rig Controls and Legacy Head")
};

inline bool WantsLegacyCurves(const ERDnaCurves Curves) {
	return Curves != ERDnaCurves::Controls;
}

inline bool KeepsRigControls(const ERDnaCurves Curves) {
	return Curves != ERDnaCurves::Legacy;
}

USTRUCT()
struct FRDnaSettings {
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, DisplayName = "Bake", Config, Category = MetaHuman)
	ERDnaBake Bake = ERDnaBake::None;

	UPROPERTY(EditAnywhere, DisplayName = "Curves", Config, Category = MetaHuman)
	ERDnaCurves Curves = ERDnaCurves::Controls;
};
