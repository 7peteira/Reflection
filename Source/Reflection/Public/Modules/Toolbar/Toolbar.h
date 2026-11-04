#pragma once

#include "CoreMinimal.h"
#include "Engine/Compatibility.h"
#include "Toolbar.generated.h"

class FMenuBuilder;

#if UE4_25_BELOW
class FToolBarBuilder;

#if !UE4_23_BELOW
struct FToolMenuSection;
#endif
#endif

UCLASS()
class REFLECTION_API UReflectionToolbar : public UObject {
	GENERATED_BODY()
public:
	void Register();

#if !UE4_23_BELOW
	void AddReflectionButtons(FToolMenuSection& Section);
	void AddCloudButtons(FToolMenuSection& Section);
#endif

#if ENGINE_UE5
	void RegisterMainMenu();

	static void PopulateValidationMenu(FMenuBuilder& MenuBuilder);

	static void PopulateAssetToolsMenu(FMenuBuilder& MenuBuilder);
#endif

#if !UE4_25_BELOW
	void RegisterAssetContextMenu();
#endif

#if ENGINE_UE4
	void UE4Register(FToolBarBuilder& Builder);
	void UE4CloudRegister(FToolBarBuilder& Builder);
#endif

	void ImportAction();

	void Import();

	static TSharedRef<SWidget> CreateMenuDropdown();
	static TSharedRef<SWidget> CreateCloudMenuDropdown();

	static bool IsToolBarVisible();
};
