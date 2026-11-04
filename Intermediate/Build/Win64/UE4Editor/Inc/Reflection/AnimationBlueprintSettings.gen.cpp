// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "Reflection/Public/Settings/Types/AnimationBlueprintSettings.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeAnimationBlueprintSettings() {}
// Cross Module References
	REFLECTION_API UScriptStruct* Z_Construct_UScriptStruct_FRAnimationBlueprintSettings();
	UPackage* Z_Construct_UPackage__Script_Reflection();
// End Cross Module References
class UScriptStruct* FRAnimationBlueprintSettings::StaticStruct()
{
	static class UScriptStruct* Singleton = NULL;
	if (!Singleton)
	{
		extern REFLECTION_API uint32 Get_Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Hash();
		Singleton = GetStaticStruct(Z_Construct_UScriptStruct_FRAnimationBlueprintSettings, Z_Construct_UPackage__Script_Reflection(), TEXT("RAnimationBlueprintSettings"), sizeof(FRAnimationBlueprintSettings), Get_Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Hash());
	}
	return Singleton;
}
template<> REFLECTION_API UScriptStruct* StaticStruct<FRAnimationBlueprintSettings>()
{
	return FRAnimationBlueprintSettings::StaticStruct();
}
static FCompiledInDeferStruct Z_CompiledInDeferStruct_UScriptStruct_FRAnimationBlueprintSettings(FRAnimationBlueprintSettings::StaticStruct, TEXT("/Script/Reflection"), TEXT("RAnimationBlueprintSettings"), false, nullptr, nullptr);
static struct FScriptStruct_Reflection_StaticRegisterNativesFRAnimationBlueprintSettings
{
	FScriptStruct_Reflection_StaticRegisterNativesFRAnimationBlueprintSettings()
	{
		UScriptStruct::DeferCppStructOps(FName(TEXT("RAnimationBlueprintSettings")),new UScriptStruct::TCppStructOps<FRAnimationBlueprintSettings>);
	}
} ScriptStruct_Reflection_StaticRegisterNativesFRAnimationBlueprintSettings;
	struct Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics
	{
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[];
#endif
		static void* NewStructOps();
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_NodeIDComments_MetaData[];
#endif
		static void NewProp_NodeIDComments_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_NodeIDComments;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
		static const UE4CodeGen_Private::FStructParams ReturnStructParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::Struct_MetaDataParams[] = {
		{ "Comment", "/* Settings for animation blueprints */" },
		{ "ModuleRelativePath", "Public/Settings/Types/AnimationBlueprintSettings.h" },
		{ "ToolTip", "Settings for animation blueprints" },
	};
#endif
	void* Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FRAnimationBlueprintSettings>();
	}
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::NewProp_NodeIDComments_MetaData[] = {
		{ "Category", "AnimationBlueprintSettings" },
		{ "Comment", "/* Saves IDs in Node's comment. */" },
		{ "DisplayName", "ID in Comments" },
		{ "ModuleRelativePath", "Public/Settings/Types/AnimationBlueprintSettings.h" },
		{ "ToolTip", "Saves IDs in Node's comment." },
	};
#endif
	void Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::NewProp_NodeIDComments_SetBit(void* Obj)
	{
		((FRAnimationBlueprintSettings*)Obj)->NodeIDComments = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::NewProp_NodeIDComments = { "NodeIDComments", nullptr, (EPropertyFlags)0x0010040000004001, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(FRAnimationBlueprintSettings), &Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::NewProp_NodeIDComments_SetBit, METADATA_PARAMS(Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::NewProp_NodeIDComments_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::NewProp_NodeIDComments_MetaData)) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::NewProp_NodeIDComments,
	};
	const UE4CodeGen_Private::FStructParams Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::ReturnStructParams = {
		(UObject* (*)())Z_Construct_UPackage__Script_Reflection,
		nullptr,
		&NewStructOps,
		"RAnimationBlueprintSettings",
		sizeof(FRAnimationBlueprintSettings),
		alignof(FRAnimationBlueprintSettings),
		Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::PropPointers,
		ARRAY_COUNT(Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::PropPointers),
		RF_Public|RF_Transient|RF_MarkAsNative,
		EStructFlags(0x00000001),
		METADATA_PARAMS(Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::Struct_MetaDataParams, ARRAY_COUNT(Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::Struct_MetaDataParams))
	};
	UScriptStruct* Z_Construct_UScriptStruct_FRAnimationBlueprintSettings()
	{
#if WITH_HOT_RELOAD
		extern uint32 Get_Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Hash();
		UPackage* Outer = Z_Construct_UPackage__Script_Reflection();
		static UScriptStruct* ReturnStruct = FindExistingStructIfHotReloadOrDynamic(Outer, TEXT("RAnimationBlueprintSettings"), sizeof(FRAnimationBlueprintSettings), Get_Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Hash(), false);
#else
		static UScriptStruct* ReturnStruct = nullptr;
#endif
		if (!ReturnStruct)
		{
			UE4CodeGen_Private::ConstructUScriptStruct(ReturnStruct, Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Statics::ReturnStructParams);
		}
		return ReturnStruct;
	}
	uint32 Get_Z_Construct_UScriptStruct_FRAnimationBlueprintSettings_Hash() { return 3942466854U; }
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
