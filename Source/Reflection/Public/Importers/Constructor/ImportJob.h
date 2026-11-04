#pragma once

#include "CoreMinimal.h"

class REFLECTION_API FImportJob {
public:
	static void Enqueue(const TArray<FString>& Files);

	static bool IsRunning();

	static void Cancel();
};
