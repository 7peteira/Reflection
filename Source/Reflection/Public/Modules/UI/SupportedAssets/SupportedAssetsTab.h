#pragma once

#include "CoreMinimal.h"

class SDockTab;
class FSpawnTabArgs;
class SSupportedAssetsPanel;

class REFLECTION_API FSupportedAssetsTab {
public:
	static void Register();
	static void Unregister();

	static void Open();

	static const FName TabId;

private:
	static TSharedRef<SDockTab> Spawn(const FSpawnTabArgs& Args);

	static TWeakPtr<SSupportedAssetsPanel> ActivePanel;
};
