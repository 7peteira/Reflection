#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/Compatibility.h"

struct FReflectionSupport {
	static void Register();
	static void Unregister();

private:
	static bool Tick(float DeltaTime);
	static void Show();

	static bool IsQuiet();

#if ENGINE_UE5
	static FTSTicker::FDelegateHandle Handle;
#else
	static FDelegateHandle Handle;
#endif

	static int32 Turn;
};
