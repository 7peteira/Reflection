#pragma once

#include "CoreMinimal.h"

class TSelectedAssetsBase;

using FToolInstanceDelegate = TSelectedAssetsBase* (*)();

inline TMap<FName, FToolInstanceDelegate>& GetToolRegistry() {
	static TMap<FName, FToolInstanceDelegate> Registry;

	return Registry;
}

template <typename ToolType>
TSelectedAssetsBase* GetToolInstance() {
	static ToolType Tool;

	return &Tool;
}

#define REGISTER_TOOL(ToolClass) \
namespace { \
	struct FAutoRegisterTool_##ToolClass { \
		FAutoRegisterTool_##ToolClass() { \
			GetToolRegistry().Add(FName(TEXT(#ToolClass)), &GetToolInstance<ToolClass>); \
		} \
	}; \
	static FAutoRegisterTool_##ToolClass AutoRegisterTool_##ToolClass; \
}
