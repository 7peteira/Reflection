
using UnrealBuildTool;

public class Reflection : ModuleRules
{
    public Reflection(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;

        PublicDefinitions.Add("REFLECTION_CLOUD_SERVER=1");

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
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
                "ClothingSystemRuntime"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Projects",
                "InputCore",
                "Slate",
                "SlateCore",
                "MaterialEditor",
                "Landscape",
                "AssetTools",
                "EditorStyle",
                "Settings",
                "RHI",
                "AnimGraphRuntime",
                "GraphEditor",
                "BlueprintGraph",
                "ReflectionDetex",
                "ReflectionNVTT"
            }
        );
    }
}
