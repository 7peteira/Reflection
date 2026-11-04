#pragma once

#include "Engine/Compatibility.h"

#if REFLECTION_RIGVM

#include "CoreMinimal.h"

class UControlRigBlueprint;

struct FCurveExpressionRigStats {
	int32 Targets = 0;
	int32 Written = 0;

	int32 Read = 0;

	int32 Nodes = 0;

	TArray<FString> Refused;
};

class REFLECTION_API FCurveExpressionRig {
public:
	static bool Build(UControlRigBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& Expressions,
		const FString& Named, FCurveExpressionRigStats& OutStats);
};

#endif
