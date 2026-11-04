
/* Copyright Reflection Contributors 2024-2026 */

using UnrealBuildTool;

public class Reflection : ModuleRules
{
    public Reflection(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;

        var bIsLinux = Target.Platform == UnrealTargetPlatform.Linux;

        // Set to false to use a local JSON file instead of the cloud server.
        var bCloudServer = true;

        PublicDefinitions.Add("REFLECTION_CLOUD_SERVER=" + (bCloudServer ? "1" : "0"));

#if UE_5_0_OR_LATER
        CppStandard = CppStandardVersion.Cpp20;
#else
        CppStandard = CppStandardVersion.Cpp17;
#endif

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "Json",
            "JsonUtilities",
            "UMG",
            "RenderCore",
            "HTTP",
            "Niagara",
            "UnrealEd",
            "MainFrame",
            "GameplayTags",
            "ApplicationCore",
            "AnimGraph",
            "UMGEditor",
            "MovieScene",

#if UE_4_26_OR_LATER
            "DeveloperSettings",
#endif

#if UE_4_24_OR_LATER
            "ClothingSystemRuntimeCommon",
#else
            "ClothingSystemRuntime",
#endif

#if UE_5_0_OR_LATER
            "ContentBrowserData"
#endif
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Projects",
            "InputCore",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "MaterialEditor",
            "Landscape",
            "AssetTools",
            "EditorStyle",
            "Settings",
            "RHI",
            "RenderCore",
            "AnimGraphRuntime",
            "AnimGraph",
            "GraphEditor",
            "BlueprintGraph",

#if UE_4_24_OR_LATER
            "ToolMenus",
#endif

#if UE_4_25_OR_LATER
            "AudioModulation",
#endif

#if UE_4_26_OR_LATER
            "PluginUtils",
#endif

#if UE_5_0_OR_LATER
            "AnimationDataController",
            "ToolWidgets"
#endif
        });

        if (!bIsLinux)
        {
            PrivateDependencyModuleNames.AddRange(new[]
            {
                "ReflectionDetex",
                "ReflectionNVTT"
            });
        }
    }
}
