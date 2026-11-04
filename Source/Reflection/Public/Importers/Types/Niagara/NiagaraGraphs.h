#pragma once

#include "CoreMinimal.h"

struct FUObjectExportContainer;
class UPropertySerializer;

struct FNiagaraGraphStats {
	int32 Placed = 0;

	int32 Pins = 0;
	int32 Links = 0;

	int32 Dangling = 0;

	int32 Unpaired = 0;

	int32 Uncalled = 0;
};

class REFLECTION_API FNiagaraGraphs {
public:
	static void Rebuild(FUObjectExportContainer* Container, UPackage* Package,
		UPropertySerializer* Serializer, FNiagaraGraphStats& OutStats);
};
