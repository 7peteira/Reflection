#pragma once

#include "CoreMinimal.h"
#include "Containers/JsonValueExport.h"

class UBlueprint;
class UEdGraph;
class UK2Node;
class UK2Node_FunctionEntry;

struct REFLECTION_API FBlueprintGraphs {
	enum class EWritten : uint8 {
		Function,

		Event,

		Ubergraph
	};

	struct FWritten {
		EWritten Kind = EWritten::Function;

		int32 EntryPoint = INDEX_NONE;

		TMap<FString, FString> Frame;
	};

	static FWritten Reads(const FUObjectJsonValueExport& Function);

	static bool IsParameter(const TSharedPtr<FJsonObject>& Property, bool& bOutGivenBack);

	static UEdGraph* MakeLayer(UBlueprint* Blueprint, const FString& Name, const TArray<TSharedPtr<FJsonValue>>& Declared);

	static constexpr const TCHAR* ClashSuffix = TEXT("_BP");

	static UEdGraph* Make(UBlueprint* Blueprint, const FString& Name, const FUObjectJsonValueExport& Function, const TArray<TSharedPtr<FJsonValue>>& Declared);

	static UEdGraph* MakeDispatcher(UBlueprint* Blueprint, const FString& Name, const TArray<TSharedPtr<FJsonValue>>& Declared);

	static UK2Node_FunctionEntry* EntryOf(const UEdGraph* Graph);

	static UEdGraph* Events(UBlueprint* Blueprint);

	static int32 RemoveUnwrittenEvents(UEdGraph* EventGraph, const TSet<FString>& Written);

	static UK2Node* MakeEvent(UBlueprint* Blueprint, UEdGraph* EventGraph, const FString& Name, const TArray<TSharedPtr<FJsonValue>>& Declared);
};
