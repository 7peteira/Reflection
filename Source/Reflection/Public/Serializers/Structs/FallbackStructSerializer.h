#pragma once

#include "StructSerializer.h"

class UPropertySerializer;

class FFallbackStructSerializer : public FStructSerializer {
	UPropertySerializer* PropertySerializer;

public:
	explicit FFallbackStructSerializer(UPropertySerializer* PropertySerializer) : PropertySerializer(PropertySerializer) { }
	virtual void Deserialize(UScriptStruct* Struct, void* StructValue, const TSharedPtr<FJsonObject> JsonValue, UObject* OptionalOuter) override;
};
