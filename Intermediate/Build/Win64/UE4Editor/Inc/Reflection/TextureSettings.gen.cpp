// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "Reflection/Public/Settings/Types/TextureSettings.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeTextureSettings() {}
// Cross Module References
	REFLECTION_API UScriptStruct* Z_Construct_UScriptStruct_FRTextureSettings();
	UPackage* Z_Construct_UPackage__Script_Reflection();
// End Cross Module References
class UScriptStruct* FRTextureSettings::StaticStruct()
{
	static class UScriptStruct* Singleton = NULL;
	if (!Singleton)
	{
		extern REFLECTION_API uint32 Get_Z_Construct_UScriptStruct_FRTextureSettings_Hash();
		Singleton = GetStaticStruct(Z_Construct_UScriptStruct_FRTextureSettings, Z_Construct_UPackage__Script_Reflection(), TEXT("RTextureSettings"), sizeof(FRTextureSettings), Get_Z_Construct_UScriptStruct_FRTextureSettings_Hash());
	}
	return Singleton;
}
template<> REFLECTION_API UScriptStruct* StaticStruct<FRTextureSettings>()
{
	return FRTextureSettings::StaticStruct();
}
static FCompiledInDeferStruct Z_CompiledInDeferStruct_UScriptStruct_FRTextureSettings(FRTextureSettings::StaticStruct, TEXT("/Script/Reflection"), TEXT("RTextureSettings"), false, nullptr, nullptr);
static struct FScriptStruct_Reflection_StaticRegisterNativesFRTextureSettings
{
	FScriptStruct_Reflection_StaticRegisterNativesFRTextureSettings()
	{
		UScriptStruct::DeferCppStructOps(FName(TEXT("RTextureSettings")),new UScriptStruct::TCppStructOps<FRTextureSettings>);
	}
} ScriptStruct_Reflection_StaticRegisterNativesFRTextureSettings;
	struct Z_Construct_UScriptStruct_FRTextureSettings_Statics
	{
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[];
#endif
		static void* NewStructOps();
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ReflectExistingTextures_MetaData[];
#endif
		static void NewProp_ReflectExistingTextures_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReflectExistingTextures;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
		static const UE4CodeGen_Private::FStructParams ReturnStructParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRTextureSettings_Statics::Struct_MetaDataParams[] = {
		{ "Comment", "/* Settings for textures */" },
		{ "ModuleRelativePath", "Public/Settings/Types/TextureSettings.h" },
		{ "ToolTip", "Settings for textures" },
	};
#endif
	void* Z_Construct_UScriptStruct_FRTextureSettings_Statics::NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FRTextureSettings>();
	}
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRTextureSettings_Statics::NewProp_ReflectExistingTextures_MetaData[] = {
		{ "Category", "TextureSettings" },
		{ "Comment", "/* Reflects textures that already exist.\n\x09 * Significantly worse for import time.\n\x09 * Do not use this unless you are intentionally reimporting textures that were updated since last updated. */" },
		{ "ModuleRelativePath", "Public/Settings/Types/TextureSettings.h" },
		{ "ToolTip", "Reflects textures that already exist.\n       * Significantly worse for import time.\n       * Do not use this unless you are intentionally reimporting textures that were updated since last updated." },
	};
#endif
	void Z_Construct_UScriptStruct_FRTextureSettings_Statics::NewProp_ReflectExistingTextures_SetBit(void* Obj)
	{
		((FRTextureSettings*)Obj)->ReflectExistingTextures = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FRTextureSettings_Statics::NewProp_ReflectExistingTextures = { "ReflectExistingTextures", nullptr, (EPropertyFlags)0x0010040000004001, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(FRTextureSettings), &Z_Construct_UScriptStruct_FRTextureSettings_Statics::NewProp_ReflectExistingTextures_SetBit, METADATA_PARAMS(Z_Construct_UScriptStruct_FRTextureSettings_Statics::NewProp_ReflectExistingTextures_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRTextureSettings_Statics::NewProp_ReflectExistingTextures_MetaData)) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FRTextureSettings_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRTextureSettings_Statics::NewProp_ReflectExistingTextures,
	};
	const UE4CodeGen_Private::FStructParams Z_Construct_UScriptStruct_FRTextureSettings_Statics::ReturnStructParams = {
		(UObject* (*)())Z_Construct_UPackage__Script_Reflection,
		nullptr,
		&NewStructOps,
		"RTextureSettings",
		sizeof(FRTextureSettings),
		alignof(FRTextureSettings),
		Z_Construct_UScriptStruct_FRTextureSettings_Statics::PropPointers,
		ARRAY_COUNT(Z_Construct_UScriptStruct_FRTextureSettings_Statics::PropPointers),
		RF_Public|RF_Transient|RF_MarkAsNative,
		EStructFlags(0x00000001),
		METADATA_PARAMS(Z_Construct_UScriptStruct_FRTextureSettings_Statics::Struct_MetaDataParams, ARRAY_COUNT(Z_Construct_UScriptStruct_FRTextureSettings_Statics::Struct_MetaDataParams))
	};
	UScriptStruct* Z_Construct_UScriptStruct_FRTextureSettings()
	{
#if WITH_HOT_RELOAD
		extern uint32 Get_Z_Construct_UScriptStruct_FRTextureSettings_Hash();
		UPackage* Outer = Z_Construct_UPackage__Script_Reflection();
		static UScriptStruct* ReturnStruct = FindExistingStructIfHotReloadOrDynamic(Outer, TEXT("RTextureSettings"), sizeof(FRTextureSettings), Get_Z_Construct_UScriptStruct_FRTextureSettings_Hash(), false);
#else
		static UScriptStruct* ReturnStruct = nullptr;
#endif
		if (!ReturnStruct)
		{
			UE4CodeGen_Private::ConstructUScriptStruct(ReturnStruct, Z_Construct_UScriptStruct_FRTextureSettings_Statics::ReturnStructParams);
		}
		return ReturnStruct;
	}
	uint32 Get_Z_Construct_UScriptStruct_FRTextureSettings_Hash() { return 587067569U; }
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
