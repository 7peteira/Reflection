#pragma once

#include "CoreMinimal.h"
#include "Modules/Tools/ToolBase.h"

class TToolImportFolder : public TToolBase {
public:
	bool Execute(const FString& InitialFolder = FString(), bool bUseClipboard = false, bool bCanGoBack = false);
};
