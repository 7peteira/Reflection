#pragma once

#include "CoreMinimal.h"

struct FLegacyCurveDrive {
	FName Control;
	float Weight = 0.0f;
};

struct REFLECTION_API FReflectionLegacyCurves {
	static bool Read(TMap<FName, TArray<FLegacyCurveDrive>>& OutByCurve);

	static bool Rewrite(TArray<TSharedPtr<FJsonValue>>& FloatCurves, const FString& Named, bool bKeepControls = false);
};
