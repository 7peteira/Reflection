#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Textures/SlateIcon.h"
#include "Engine/Compatibility.h"

#if ENGINE_UE5

struct REFLECTION_API FReflectionValidator {
	FName Id;

	FText Label;
	FText Description;
	FSlateIcon Icon;

	FExecuteAction OnOpen;

	FNewMenuDelegate BuildMenu;
};

class REFLECTION_API FReflectionValidatorRegistry {
public:
	static void Register(const FReflectionValidator& Validator);

	static const TArray<FReflectionValidator>& GetValidators();
	static const FReflectionValidator* Find(FName Id);

	static void Open(FName Id);

	static void OpenDefault();
};

#endif
