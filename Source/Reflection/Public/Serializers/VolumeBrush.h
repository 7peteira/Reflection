#pragma once

#include "CoreMinimal.h"

class ABrush;
class UBrushComponent;
class UPropertySerializer;

class REFLECTION_API FVolumeBrush {
public:
	static void Rebuild(UBrushComponent* Component, const TSharedPtr<FJsonObject>& Properties = nullptr, const UPropertySerializer* Serializer = nullptr);

	static void Rebuild(ABrush* Brush);
};
