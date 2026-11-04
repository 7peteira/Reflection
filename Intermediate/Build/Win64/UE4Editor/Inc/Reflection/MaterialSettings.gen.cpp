// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "Reflection/Public/Settings/Types/MaterialSettings.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeMaterialSettings() {}
// Cross Module References
	REFLECTION_API UScriptStruct* Z_Construct_UScriptStruct_FRMaterialSettings();
	UPackage* Z_Construct_UPackage__Script_Reflection();
// End Cross Module References
class UScriptStruct* FRMaterialSettings::StaticStruct()
{
	static class UScriptStruct* Singleton = NULL;
	if (!Singleton)
	{
		extern REFLECTION_API uint32 Get_Z_Construct_UScriptStruct_FRMaterialSettings_Hash();
		Singleton = GetStaticStruct(Z_Construct_UScriptStruct_FRMaterialSettings, Z_Construct_UPackage__Script_Reflection(), TEXT("RMaterialSettings"), sizeof(FRMaterialSettings), Get_Z_Construct_UScriptStruct_FRMaterialSettings_Hash());
	}
	return Singleton;
}
template<> REFLECTION_API UScriptStruct* StaticStruct<FRMaterialSettings>()
{
	return FRMaterialSettings::StaticStruct();
}
static FCompiledInDeferStruct Z_CompiledInDeferStruct_UScriptStruct_FRMaterialSettings(FRMaterialSettings::StaticStruct, TEXT("/Script/Reflection"), TEXT("RMaterialSettings"), false, nullptr, nullptr);
static struct FScriptStruct_Reflection_StaticRegisterNativesFRMaterialSettings
{
	FScriptStruct_Reflection_StaticRegisterNativesFRMaterialSettings()
	{
		UScriptStruct::DeferCppStructOps(FName(TEXT("RMaterialSettings")),new UScriptStruct::TCppStructOps<FRMaterialSettings>);
	}
} ScriptStruct_Reflection_StaticRegisterNativesFRMaterialSettings;
	struct Z_Construct_UScriptStruct_FRMaterialSettings_Statics
	{
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[];
#endif
		static void* NewStructOps();
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_Stubs_MetaData[];
#endif
		static void NewProp_Stubs_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_Stubs;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
		static const UE4CodeGen_Private::FStructParams ReturnStructParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRMaterialSettings_Statics::Struct_MetaDataParams[] = {
		{ "Comment", "/* Settings for materials */" },
		{ "ModuleRelativePath", "Public/Settings/Types/MaterialSettings.h" },
		{ "ToolTip", "Settings for materials" },
	};
#endif
	void* Z_Construct_UScriptStruct_FRMaterialSettings_Statics::NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FRMaterialSettings>();
	}
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRMaterialSettings_Statics::NewProp_Stubs_MetaData[] = {
		{ "Category", "MaterialSettings" },
		{ "Comment", "/* Creates stub versions of materials that have parameters (for Modding) */" },
		{ "ModuleRelativePath", "Public/Settings/Types/MaterialSettings.h" },
		{ "ToolTip", "Creates stub versions of materials that have parameters (for Modding)" },
	};
#endif
	void Z_Construct_UScriptStruct_FRMaterialSettings_Statics::NewProp_Stubs_SetBit(void* Obj)
	{
		((FRMaterialSettings*)Obj)->Stubs = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FRMaterialSettings_Statics::NewProp_Stubs = { "Stubs", nullptr, (EPropertyFlags)0x0010000000004001, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(FRMaterialSettings), &Z_Construct_UScriptStruct_FRMaterialSettings_Statics::NewProp_Stubs_SetBit, METADATA_PARAMS(Z_Construct_UScriptStruct_FRMaterialSettings_Statics::NewProp_Stubs_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRMaterialSettings_Statics::NewProp_Stubs_MetaData)) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FRMaterialSettings_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRMaterialSettings_Statics::NewProp_Stubs,
	};
	const UE4CodeGen_Private::FStructParams Z_Construct_UScriptStruct_FRMaterialSettings_Statics::ReturnStructParams = {
		(UObject* (*)())Z_Construct_UPackage__Script_Reflection,
		nullptr,
		&NewStructOps,
		"RMaterialSettings",
		sizeof(FRMaterialSettings),
		alignof(FRMaterialSettings),
		Z_Construct_UScriptStruct_FRMaterialSettings_Statics::PropPointers,
		ARRAY_COUNT(Z_Construct_UScriptStruct_FRMaterialSettings_Statics::PropPointers),
		RF_Public|RF_Transient|RF_MarkAsNative,
		EStructFlags(0x00000001),
		METADATA_PARAMS(Z_Construct_UScriptStruct_FRMaterialSettings_Statics::Struct_MetaDataParams, ARRAY_COUNT(Z_Construct_UScriptStruct_FRMaterialSettings_Statics::Struct_MetaDataParams))
	};
	UScriptStruct* Z_Construct_UScriptStruct_FRMaterialSettings()
	{
#if WITH_HOT_RELOAD
		extern uint32 Get_Z_Construct_UScriptStruct_FRMaterialSettings_Hash();
		UPackage* Outer = Z_Construct_UPackage__Script_Reflection();
		static UScriptStruct* ReturnStruct = FindExistingStructIfHotReloadOrDynamic(Outer, TEXT("RMaterialSettings"), sizeof(FRMaterialSettings), Get_Z_Construct_UScriptStruct_FRMaterialSettings_Hash(), false);
#else
		static UScriptStruct* ReturnStruct = nullptr;
#endif
		if (!ReturnStruct)
		{
			UE4CodeGen_Private::ConstructUScriptStruct(ReturnStruct, Z_Construct_UScriptStruct_FRMaterialSettings_Statics::ReturnStructParams);
		}
		return ReturnStruct;
	}
	uint32 Get_Z_Construct_UScriptStruct_FRMaterialSettings_Hash() { return 3094398033U; }
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
