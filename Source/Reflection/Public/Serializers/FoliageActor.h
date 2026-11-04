#pragma once

#include "CoreMinimal.h"

class AInstancedFoliageActor;
class UPropertySerializer;

class REFLECTION_API FFoliageActor {
public:
	static void Rebuild(AInstancedFoliageActor* Actor, const UPropertySerializer* Serializer);
};
