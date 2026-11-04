#pragma once

#include "CoreMinimal.h"

class UBlueprint;
struct FUObjectExportContainer;

struct REFLECTION_API FBlueprintCookedMetaData {
	static int32 Apply(UBlueprint* Blueprint, FUObjectExportContainer* Container);

	static TMap<FString, FString> WorldContexts(FUObjectExportContainer* Container);
};
