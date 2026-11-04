#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "Dom/JsonObject.h"

class UBlueprint;
struct FUObjectExportContainer;

struct REFLECTION_API FBlueprintVariables {
	static TArray<TSharedPtr<FJsonValue>> GetDeclared(const TSharedPtr<FJsonObject>& Owner, FUObjectExportContainer* Container);

	static TSet<FString> GetComponentVariables(FUObjectExportContainer* Container);

	static TSet<FString> GetWidgetVariables(FUObjectExportContainer* Container);

	static int32 Construct(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& ChildProperties, FUObjectExportContainer* Container = nullptr);

	static bool IsUserVariable(const TSharedPtr<FJsonObject>& Property);

	static bool GetPinType(const TSharedPtr<FJsonObject>& Property, FEdGraphPinType& OutPinType, FUObjectExportContainer* Container = nullptr);
};
