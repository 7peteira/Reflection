#pragma once

#include "CoreMinimal.h"

struct FUObjectExportContainer;

class REFLECTION_API FNiagaraCookedCache {
public:
	static int32 Restore(FUObjectExportContainer* Container);
};
