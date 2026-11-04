#pragma once

#include "StructSerializer.h"
#include "FallbackStructSerializer.h"

class UPropertySerializer;

class FNiagaraVariableSerializer : public FStructSerializer {
	FFallbackStructSerializer Fallback;

public:
	explicit FNiagaraVariableSerializer(UPropertySerializer* PropertySerializer) : Fallback(PropertySerializer) {}

	virtual void Deserialize(UScriptStruct* Struct, void* StructData, const TSharedPtr<FJsonObject> JsonValue, UObject* OptionalOuter) override;
};
