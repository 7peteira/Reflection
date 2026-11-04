#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "Types/AnimationBlueprintSettings.h"
#include "Types/LevelSettings.h"
#include "Types/MeshSettings.h"
#include "Types/TextureSettings.h"
#include "Redirector.h"

#include "ReflectionSettings.generated.h"

extern FName GReflectionSettingsCategoryName;
extern FName GReflectionInternalName;

UENUM()
enum class ERPackagedAssets : uint8 {
	Together UMETA(DisplayName = "Kept Together"),

	Separate UMETA(DisplayName = "Split Apart")
};

USTRUCT()
struct FRSettings
{
	GENERATED_BODY()
public:
	FRSettings() {
		AnimationBlueprint = FRAnimationBlueprintSettings();
		Texture = FRTextureSettings();
		Mesh = FRMeshSettings();
	}

	UPROPERTY(EditAnywhere, Config, Category = Settings)
	FRAnimationBlueprintSettings AnimationBlueprint;

	UPROPERTY(EditAnywhere, Config, Category = Settings)
	FRTextureSettings Texture;

	UPROPERTY(EditAnywhere, Config, Category = Settings)
	FRMeshSettings Mesh;

	UPROPERTY(EditAnywhere, DisplayName = "Packaged Assets", Config, Category = Settings)
	ERPackagedAssets PackagedAssets = ERPackagedAssets::Together;

	UPROPERTY(EditAnywhere, Config, Category = Settings)
	bool SaveAssets = false;

	UPROPERTY(EditAnywhere, DisplayName = "Import References", Config, Category = Settings)
	bool ImportReferences = true;

	UPROPERTY(EditAnywhere, DisplayName = "Import Blueprint Code", Config, Category = Settings)
	bool ImportBlueprintCode = true;
};

UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig)
class REFLECTION_API UReflectionSettings : public UDeveloperSettings {
	GENERATED_BODY()
public:
	UReflectionSettings();

	virtual FText GetSectionText() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	UPROPERTY(EditAnywhere, Config, Category = Redirectors, meta = (TitleProperty = "Name"))
	TArray<FRRedirector> Redirectors;

	UPROPERTY(EditAnywhere, Config, Category = Settings)
	FRSettings AssetSettings;

	UPROPERTY(EditAnywhere, DisplayName = "Level", Config, Category = Settings, meta = (EditCondition = "EnableExperiments"))
	FRLevelSettings Level;

	UPROPERTY(EditAnywhere, DisplayName = "Use Local Json instead of Cloud", Category = Settings, Config)
	bool UseLocalJson = false;

	UPROPERTY(EditAnywhere, Config, DisplayName = "Experiments", Category = Settings, AdvancedDisplay)
	bool EnableExperiments = false;

	UPROPERTY(EditAnywhere, Config, DisplayName = "Support Prompt", Category = Settings, AdvancedDisplay)
	bool ShowSupportPrompt = true;
};
