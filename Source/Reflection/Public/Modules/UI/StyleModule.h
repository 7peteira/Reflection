#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class FReflectionStyle {
public:
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

	static FName GetEmbeddedToolbarStyleName();

	static FName GetActionButtonStyleName();

	static void EnsureEmbeddedToolbarStyleRegistered();

public:
	static void Initialize();
	static void ReloadTextures();

private:
	static TSharedRef<FSlateStyleSet> Create();
	static TSharedPtr<FSlateStyleSet> StyleInstance;

public:
	static void Shutdown();
};
