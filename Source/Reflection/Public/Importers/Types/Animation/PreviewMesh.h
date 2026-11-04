#pragma once

#include "CoreMinimal.h"

class USkeleton;

struct REFLECTION_API FPreviewMesh {
	static bool FromFolder(UObject* Asset, const USkeleton* Skeleton);
};
