#pragma once

#include "CoreMinimal.h"
#include "Containers/JsonValueExport.h"

class UEdGraph;

struct FMacroMatch {
	int32 First = INDEX_NONE;
	int32 Last = INDEX_NONE;

	TMap<FName, FUObjectJsonValueExport> Inputs;

	TMap<FName, FUObjectJsonValueExport> Deferred;

	TArray<TPair<FName, TPair<int32, int32>>> Bodies;

	TMap<FName, int32> Leads;

	TMap<FString, FName> Handouts;

	TSet<int32> Internal;

	TMap<int32, FName> Takes;

	TSet<FString> ByReference;

	const struct FMacroPattern* Pattern = nullptr;

	bool IsValid() const { return First != INDEX_NONE && Last != INDEX_NONE; }
};

struct FMacroPattern {
	virtual ~FMacroPattern() = default;

	virtual const TCHAR* GetName() const = 0;

	virtual bool Match(const TArray<FUObjectJsonValueExport>& Statements, int32 At, FMacroMatch& Out) const = 0;
};

REFLECTION_API const TArray<TSharedRef<FMacroPattern>>& GetMacroPatterns();

REFLECTION_API void AddMacroPattern(const TSharedRef<FMacroPattern>& Pattern);

#define REGISTER_MACRO(PatternClass) \
namespace { \
	struct FAutoMacro_##PatternClass { \
		FAutoMacro_##PatternClass() { \
			AddMacroPattern(MakeShared<PatternClass>()); \
		} \
	}; \
	static FAutoMacro_##PatternClass AutoMacro_##PatternClass; \
}

namespace MacroReading {
	REFLECTION_API FString NamedProperty(const FUObjectJsonValueExport& Variable);

	REFLECTION_API FString WrittenTo(const FUObjectJsonValueExport& Statement);

	REFLECTION_API FString ReadFrom(const FUObjectJsonValueExport& Expression);

	REFLECTION_API FString CallsTo(const FUObjectJsonValueExport& Expression);

	REFLECTION_API FString TokenOf(const FUObjectJsonValueExport& Statement);

	REFLECTION_API UEdGraph* StandardMacro(const TCHAR* Named);

	REFLECTION_API bool Same(const FUObjectJsonValueExport& A, const FUObjectJsonValueExport& B);

	REFLECTION_API bool IsLet(const FString& Token);

	REFLECTION_API int32 AddressOf(const FUObjectJsonValueExport& Statement);

	REFLECTION_API int32 IndexOfAddress(const TArray<FUObjectJsonValueExport>& Statements, int32 Address);

	REFLECTION_API int32 NextInRun(const TArray<FUObjectJsonValueExport>& Statements, int32 Address, TSet<int32>& Passed);
}
