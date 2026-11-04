#pragma once

#include "CoreMinimal.h"
#include "Engine/Compatibility.h"

#if ENGINE_UE5

class SDockTab;
class SValidationPanel;
class FSpawnTabArgs;

class REFLECTION_API FValidationTab {
public:
	static const FName TabId;

	static void Register();
	static void Unregister();

	static void Open();

	static void OpenAt(FString RootPath, bool bRunImmediately);

	static void OpenAtSelectedFolder();

private:
	static TSharedRef<SDockTab> Spawn(const FSpawnTabArgs& Args);

	static TWeakPtr<SValidationPanel> ActivePanel;
};

#endif
