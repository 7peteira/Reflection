using System;
using System.IO;
using UnrealBuildTool;

public class Reflection : ModuleRules {
	public Reflection(ReadOnlyTargetRules Target) : base(Target)  {
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		var bIsLinux = Target.Platform == UnrealTargetPlatform.Linux;

		var bCloudServer = true;

		PublicDefinitions.Add("REFLECTION_CLOUD_SERVER=" + (bCloudServer ? "1" : "0"));

		var bControlRig = false;

#if UE_5_2_OR_LATER
		bControlRig = Directory.Exists(Path.Combine(EngineDirectory, "Plugins", "Animation", "ControlRig"))
			&& Directory.Exists(Path.Combine(EngineDirectory, "Plugins", "Runtime", "RigVM"));
#endif

		PublicDefinitions.Add("REFLECTION_CONTROL_RIG=" + (bControlRig ? "1" : "0"));

		var bRigVM = bControlRig;

		if (!bRigVM) {
			bRigVM = Directory.Exists(Path.Combine(EngineDirectory, "Plugins", "Experimental", "ControlRig"))
				&& Directory.Exists(Path.Combine(EngineDirectory, "Source", "Developer", "RigVMDeveloper"));
		}

		PublicDefinitions.Add("REFLECTION_RIGVM=" + (bRigVM ? "1" : "0"));

		var bRigLogic = false;

#if UE_5_2_OR_LATER
		bRigLogic = Directory.Exists(Path.Combine(EngineDirectory, "Plugins", "Animation", "RigLogic"));
#endif

		PublicDefinitions.Add("REFLECTION_RIG_LOGIC=" + (bRigLogic ? "1" : "0"));

		var bDnaUserData = bRigLogic && File.Exists(Path.Combine(EngineDirectory, "Plugins", "Animation", "RigLogic",
			"Source", "RigLogicModule", "Public", "DNAAssetUserData.h"));

		PublicDefinitions.Add("REFLECTION_DNA_USER_DATA=" + (bDnaUserData ? "1" : "0"));

		var ueSpaceReaderHeader = Path.Combine(EngineDirectory, "Plugins", "Animation", "RigLogic",
			"Source", "RigLogicModule", "Public", "RigLogicDNAReader.h");

		var bUeSpaceReader = bRigLogic && File.Exists(ueSpaceReaderHeader)
			&& !File.ReadAllText(ueSpaceReaderHeader).Contains("is obsolete");

		PublicDefinitions.Add("REFLECTION_RIG_LOGIC_UE_SPACE_READER=" + (bUeSpaceReader ? "1" : "0"));

		var bCurveExpression = false;

#if UE_5_0_OR_LATER
		foreach (var CurveExpressionRoot in new[] {
			Path.Combine(EngineDirectory, "Plugins", "Animation", "CurveExpression", "Source"),
			Path.Combine(EngineDirectory, "Plugins", "Experimental", "Animation", "CurveExpression", "Source")
		}) {
			if (Directory.Exists(CurveExpressionRoot)
				&& Directory.GetFiles(CurveExpressionRoot, "CurveExpressionsDataAsset.h", SearchOption.AllDirectories).Length > 0) {
				bCurveExpression = true;

				break;
			}
		}
#endif

		PublicDefinitions.Add("REFLECTION_CURVE_EXPRESSION=" + (bCurveExpression ? "1" : "0"));

#if UE_5_1_OR_LATER
	    CppStandard = CppStandardVersion.Cpp20;
#else
		CppStandard = CppStandardVersion.Cpp17;

#if !UE_4_26_OR_LATER
		PCHUsage = PCHUsageMode.NoPCHs;
#endif
#endif

		PublicDependencyModuleNames.AddRange(new[] {
			"Core",
			"Json",
			"JsonUtilities",
			"UMG",
			"RenderCore",
			"HTTP",
			"Niagara",
			"UnrealEd",
			"MainFrame",
			"AIModule",
				"GameplayTags",
			"GameplayTagsEditor",
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
			"ContentBrowserData",

			"BSPUtils"
#endif
		});

		PrivateDependencyModuleNames.AddRange(new[] {
			"Projects",
			"InputCore",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"MaterialEditor",

			"PropertyEditor",

			"NiagaraEditor",
			"Landscape",
			"AssetTools",
			"EditorStyle",

			"ContentBrowser",
			"Settings",
			"RHI",
			"ReflectionDetex",
			"ReflectionNVTT",
			"RenderCore",
			"AnimGraphRuntime",
			"AnimGraph",

			"GraphEditor",

			"BlueprintGraph",

#if !UE_4_25_OR_LATER
			"Kismet",
#endif

			"Foliage",

			"MeshDescription",

#if UE_4_24_OR_LATER
			"StaticMeshDescription",
#endif

#if UE_4_24_OR_LATER
			"ScriptDisassembler",
#endif

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

		if (Directory.Exists(Path.Combine(EngineDirectory, "Source", "Runtime", "PhysicsCore"))) {
			PrivateDependencyModuleNames.Add("PhysicsCore");
		}

		if (bRigVM && !bControlRig) {
			PrivateDependencyModuleNames.AddRange(new[] {
				"ControlRig",
				"ControlRigDeveloper",
				"RigVMDeveloper"
			});
		}

		if (bControlRig) {
			PrivateDependencyModuleNames.AddRange(new[] {
				"ControlRig",

				"ControlRigDeveloper",
				"RigVMDeveloper",

				"RigVM",

				"ControlRigEditor"
			});
		}

		if (bRigLogic) {
			PrivateDependencyModuleNames.AddRange(new[] {
				"RigLogicModule",

				"RigLogicLib",

				"RigLogicDeveloper"
			});
		}

		if (bCurveExpression) {
			PrivateDependencyModuleNames.AddRange(new[] {
				"CurveExpression"
			});
		}

		if (!bIsLinux) {
			PrivateDependencyModuleNames.AddRange(new[] {
				"ReflectionDetex",
				"ReflectionNVTT"
			});
		}
	}
}
