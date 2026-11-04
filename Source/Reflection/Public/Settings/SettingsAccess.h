#pragma once

#include "ISettingsModule.h"
#include "Modules/Metadata.h"
#include "Settings/ReflectionSettings.h"
#include "Settings/ModdingSettings.h"
#include "Engine/Compatibility.h"

#if (ENGINE_MAJOR_VERSION != 4 || ENGINE_MINOR_VERSION < 27)
#include "Engine/DeveloperSettings.h"
#endif

inline UReflectionSettings* GetSettings() {
	return GetMutableDefault<UReflectionSettings>();
}

inline UReflectionModdingSettings* GetModdingSettings() {
	return GetMutableDefault<UReflectionModdingSettings>();
}

inline const FRModdingSettings& GetModdingAssetSettings() {
	static const FRModdingSettings Off = [] {
		FRModdingSettings Settings;

		Settings.Material.Stubs = false;
		Settings.MetaHuman.Bake = ERDnaBake::None;
		Settings.MetaHuman.Curves = ERDnaCurves::Controls;
		Settings.CurveMapping = ERCurveMapping::DataAsset;

		return Settings;
	}();

	const UReflectionModdingSettings* Settings = GetModdingSettings();

	return Settings->Enabled ? Settings->Settings : Off;
}

inline bool ImportsCurveMappingAsRig() {
#if REFLECTION_RIGVM
#if REFLECTION_CURVE_EXPRESSION
	return GetModdingAssetSettings().CurveMapping == ERCurveMapping::ControlRig;
#else
	return true;
#endif
#else
	return false;
#endif
}

inline void SavePluginSettings(UDeveloperSettings* EditorSettings) {
	EditorSettings->SaveConfig();

#if ENGINE_UE5
	EditorSettings->TryUpdateDefaultConfigFile();
	EditorSettings->ReloadConfig(nullptr, nullptr, UE::LCPF_PropagateToInstances);
#else
	EditorSettings->UpdateDefaultConfigFile();
	EditorSettings->ReloadConfig(nullptr, nullptr, UE4::LCPF_PropagateToInstances);
#endif

	EditorSettings->LoadConfig();
}

inline void OpenPluginSettings() {
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", GReflectionSettingsCategoryName, GReflectionName);
}
