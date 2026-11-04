using UnrealBuildTool;
using System.IO;

public class ReflectionNVTT : ModuleRules
{
    public ReflectionNVTT(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDefinitions.Add("NVTT_API=REFLECTIONNVTT_API");

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "ThirdParty/nvtt"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "ThirdParty/nvtt"));

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "ApplicationCore"
        });
    }
}
