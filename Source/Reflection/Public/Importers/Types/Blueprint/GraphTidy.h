#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphPin;
class UK2Node_VariableGet;

struct FGraphTidy {
	virtual ~FGraphTidy() = default;

	virtual const TCHAR* GetName() const = 0;

	virtual int32 Apply(UEdGraph* Graph) const = 0;
};

REFLECTION_API bool SetVariableGetVariation(UK2Node_VariableGet* Get, FName Variation);

REFLECTION_API void GiveReadersTheirOwn(UEdGraph* Graph, UK2Node_VariableGet* Converted, const TArray<UEdGraphPin*>& Readers);

REFLECTION_API const TArray<TSharedRef<FGraphTidy>>& GetGraphTidies();

REFLECTION_API void AddGraphTidy(const TSharedRef<FGraphTidy>& Tidy);

#define REGISTER_TIDY(TidyClass) \
namespace { \
	struct FAutoTidy_##TidyClass { \
		FAutoTidy_##TidyClass() { \
			AddGraphTidy(MakeShared<TidyClass>()); \
		} \
	}; \
	static FAutoTidy_##TidyClass AutoTidy_##TidyClass; \
}
