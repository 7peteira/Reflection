#pragma once

#include "CoreMinimal.h"
#include "Modules/Tools/ToolBase.h"

class TToolImportFromPath : public TToolBase {
public:
	void Execute();

	static REFLECTION_API bool Import(const FString& InPath, const TSet<FString>& AllowedTypes = TSet<FString>(), bool* bOutFiltered = nullptr);
};
