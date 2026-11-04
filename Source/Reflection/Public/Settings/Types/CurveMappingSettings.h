#pragma once

#include "CoreMinimal.h"
#include "CurveMappingSettings.generated.h"

UENUM()
enum class ERCurveMapping : uint8 {
	DataAsset UMETA(DisplayName = "Data Asset"),

	ControlRig UMETA(DisplayName = "Control Rig")
};
