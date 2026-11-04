#include "Engine/SharedPtrCompatibility.h"
#include "Reflection.h"

#if ENGINE_UE4
#if !UE4_23_BELOW
#include "ToolMenus.h"
#endif
#include "LevelEditor.h"
#endif

#include "Http.h"
#include "Modules/Versioning.h"
#include "Modules/Support.h"

#include "Modules/UI/StyleModule.h"
#include "Modules/UI/SupportedAssets/SupportedAssetsTab.h"
#include "Modules/UI/Validation/ValidationTab.h"
#include "Modules/Toolbar/Toolbar.h"
#include "Engine/EngineUtilities.h"

#include "Logging/LogVerbosity.h"
#include "JsonGlobals.h"
#include "Settings/Runtime.h"

#ifdef _MSC_VER
#undef GetObject
#endif

void FReflectionModule::StartupModule() {
	LogHttp.SetVerbosity(ELogVerbosity::Error);

	LogJson.SetVerbosity(ELogVerbosity::NoLogging);

	FRMetadata::Initialize();

    FReflectionStyle::Initialize();
    FReflectionStyle::ReloadTextures();

	FSupportedAssetsTab::Register();

#if ENGINE_UE5
	FValidationTab::Register();
#endif

	Toolbar = NewObject<UReflectionToolbar>();
	Toolbar->AddToRoot();

#if !UE4_23_BELOW
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateUObject(Toolbar, &UReflectionToolbar::Register));
#endif

#if ENGINE_UE4
	{
    	const TSharedPtr<FUICommandList> PluginCommands = MakeShareable(new FUICommandList);

    	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
    	const TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
    	ToolbarExtender->AddToolBarExtension(
			"Settings",
			EExtensionHook::After,
			PluginCommands,
			FToolBarExtensionDelegate::CreateUObject(Toolbar, &UReflectionToolbar::UE4Register)
		);

    	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}
#endif

	GReflectionVersioning.Update();
	GReflectionRuntime.Update();

	FReflectionSupport::Register();
}

void FReflectionModule::ShutdownModule() {
#if !UE4_23_BELOW
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
#endif

	FSupportedAssetsTab::Unregister();

#if ENGINE_UE5
	if (Toolbar) {
		UToolMenus::UnregisterOwner(Toolbar);
	}

	FValidationTab::Unregister();
#endif

	FReflectionSupport::Unregister();

	FReflectionStyle::Shutdown();

	if (Toolbar) {
#if UE4_23_BELOW
		if (!GIsRequestingExit) {
#else
		if (!IsEngineExitRequested()) {
#endif
			Toolbar->RemoveFromRoot();
		}
		Toolbar = nullptr;
	}
}

IMPLEMENT_MODULE(FReflectionModule, Reflection)
