using UnrealBuildTool;
using System.IO;

public class ReflectionDetex : ModuleRules
{
    public ReflectionDetex(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDefinitions.Add("DETEX_API=REFLECTIONDETEX_API");
        Definitions.Add("REFLECTIONDETEX_EXPORTS=1");

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "ThirdParty/detex"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "ThirdParty/detex"));

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject"
        });
    }
}
