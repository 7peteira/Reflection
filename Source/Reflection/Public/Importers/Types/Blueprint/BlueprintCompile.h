#pragma once

#include "CoreMinimal.h"
#include "Kismet2/KismetEditorUtilities.h"

class UBlueprint;

REFLECTION_API bool CompileBlueprintGuarded(UBlueprint* Blueprint,
	EBlueprintCompileOptions Options = EBlueprintCompileOptions::None);

REFLECTION_API UBlueprint* CreateBlueprintGuarded(UClass* ParentClass, UObject* Outer, FName Name,
	EBlueprintType BlueprintType, UClass* BlueprintClassType, UClass* BlueprintGeneratedClassType);
