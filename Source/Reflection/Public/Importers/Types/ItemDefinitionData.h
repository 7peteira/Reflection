#pragma once

#include "CoreMinimal.h"

class UObjectSerializer;

struct REFLECTION_API FItemDefinitionData {
	static int32 Apply(UObject* Asset, const TSharedPtr<FJsonObject>& Data, const UObjectSerializer* Serializer);
};
