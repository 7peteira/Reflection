#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"

extern FName GReflectionName;

struct FRMetadata {
	static TSharedPtr<IPlugin> Plugin;
	static FString Version;

	static void Initialize();
};
