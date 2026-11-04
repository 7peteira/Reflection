// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "Reflection/Public/Settings/ReflectionSettings.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeReflectionSettings() {}
// Cross Module References
	REFLECTION_API UScriptStruct* Z_Construct_UScriptStruct_FRSettings();
	UPackage* Z_Construct_UPackage__Script_Reflection();
	REFLECTION_API UScriptStruct* Z_Construct_UScriptStruct_FRMaterialSettings();
	REFLECTION_API UScriptStruct* Z_Construct_UScriptStruct_FRTextureSettings();
	REFLECTION_API UScriptStruct* Z_Construct_UScriptStruct_FRAnimationBlueprintSettings();
	REFLECTION_API UClass* Z_Construct_UClass_UReflectionSettings_NoRegister();
	REFLECTION_API UClass* Z_Construct_UClass_UReflectionSettings();
	ENGINE_API UClass* Z_Construct_UClass_UDeveloperSettings();
	REFLECTION_API UScriptStruct* Z_Construct_UScriptStruct_FRRedirector();
// End Cross Module References
class UScriptStruct* FRSettings::StaticStruct()
{
	static class UScriptStruct* Singleton = NULL;
	if (!Singleton)
	{
		extern REFLECTION_API uint32 Get_Z_Construct_UScriptStruct_FRSettings_Hash();
		Singleton = GetStaticStruct(Z_Construct_UScriptStruct_FRSettings, Z_Construct_UPackage__Script_Reflection(), TEXT("RSettings"), sizeof(FRSettings), Get_Z_Construct_UScriptStruct_FRSettings_Hash());
	}
	return Singleton;
}
template<> REFLECTION_API UScriptStruct* StaticStruct<FRSettings>()
{
	return FRSettings::StaticStruct();
}
static FCompiledInDeferStruct Z_CompiledInDeferStruct_UScriptStruct_FRSettings(FRSettings::StaticStruct, TEXT("/Script/Reflection"), TEXT("RSettings"), false, nullptr, nullptr);
static struct FScriptStruct_Reflection_StaticRegisterNativesFRSettings
{
	FScriptStruct_Reflection_StaticRegisterNativesFRSettings()
	{
		UScriptStruct::DeferCppStructOps(FName(TEXT("RSettings")),new UScriptStruct::TCppStructOps<FRSettings>);
	}
} ScriptStruct_Reflection_StaticRegisterNativesFRSettings;
	struct Z_Construct_UScriptStruct_FRSettings_Statics
	{
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[];
#endif
		static void* NewStructOps();
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_SaveAssets_MetaData[];
#endif
		static void NewProp_SaveAssets_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_SaveAssets;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_Material_MetaData[];
#endif
		static const UE4CodeGen_Private::FStructPropertyParams NewProp_Material;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_Texture_MetaData[];
#endif
		static const UE4CodeGen_Private::FStructPropertyParams NewProp_Texture;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_AnimationBlueprint_MetaData[];
#endif
		static const UE4CodeGen_Private::FStructPropertyParams NewProp_AnimationBlueprint;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
		static const UE4CodeGen_Private::FStructParams ReturnStructParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRSettings_Statics::Struct_MetaDataParams[] = {
		{ "ModuleRelativePath", "Public/Settings/ReflectionSettings.h" },
	};
#endif
	void* Z_Construct_UScriptStruct_FRSettings_Statics::NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FRSettings>();
	}
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_SaveAssets_MetaData[] = {
		{ "Category", "Settings" },
		{ "ModuleRelativePath", "Public/Settings/ReflectionSettings.h" },
	};
#endif
	void Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_SaveAssets_SetBit(void* Obj)
	{
		((FRSettings*)Obj)->SaveAssets = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_SaveAssets = { "SaveAssets", nullptr, (EPropertyFlags)0x0010000000004001, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(FRSettings), &Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_SaveAssets_SetBit, METADATA_PARAMS(Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_SaveAssets_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_SaveAssets_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_Material_MetaData[] = {
		{ "Category", "Settings" },
		{ "ModuleRelativePath", "Public/Settings/ReflectionSettings.h" },
	};
#endif
	const UE4CodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_Material = { "Material", nullptr, (EPropertyFlags)0x0010000000004001, UE4CodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(FRSettings, Material), Z_Construct_UScriptStruct_FRMaterialSettings, METADATA_PARAMS(Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_Material_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_Material_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_Texture_MetaData[] = {
		{ "Category", "Settings" },
		{ "ModuleRelativePath", "Public/Settings/ReflectionSettings.h" },
	};
#endif
	const UE4CodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_Texture = { "Texture", nullptr, (EPropertyFlags)0x0010000000004001, UE4CodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(FRSettings, Texture), Z_Construct_UScriptStruct_FRTextureSettings, METADATA_PARAMS(Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_Texture_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_Texture_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_AnimationBlueprint_MetaData[] = {
		{ "Category", "Settings" },
		{ "ModuleRelativePath", "Public/Settings/ReflectionSettings.h" },
	};
#endif
	const UE4CodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_AnimationBlueprint = { "AnimationBlueprint", nullptr, (EPropertyFlags)0x0010000000004001, UE4CodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(FRSettings, AnimationBlueprint), Z_Construct_UScriptStruct_FRAnimationBlueprintSettings, METADATA_PARAMS(Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_AnimationBlueprint_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_AnimationBlueprint_MetaData)) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FRSettings_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_SaveAssets,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_Material,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_Texture,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRSettings_Statics::NewProp_AnimationBlueprint,
	};
	const UE4CodeGen_Private::FStructParams Z_Construct_UScriptStruct_FRSettings_Statics::ReturnStructParams = {
		(UObject* (*)())Z_Construct_UPackage__Script_Reflection,
		nullptr,
		&NewStructOps,
		"RSettings",
		sizeof(FRSettings),
		alignof(FRSettings),
		Z_Construct_UScriptStruct_FRSettings_Statics::PropPointers,
		ARRAY_COUNT(Z_Construct_UScriptStruct_FRSettings_Statics::PropPointers),
		RF_Public|RF_Transient|RF_MarkAsNative,
		EStructFlags(0x00000001),
		METADATA_PARAMS(Z_Construct_UScriptStruct_FRSettings_Statics::Struct_MetaDataParams, ARRAY_COUNT(Z_Construct_UScriptStruct_FRSettings_Statics::Struct_MetaDataParams))
	};
	UScriptStruct* Z_Construct_UScriptStruct_FRSettings()
	{
#if WITH_HOT_RELOAD
		extern uint32 Get_Z_Construct_UScriptStruct_FRSettings_Hash();
		UPackage* Outer = Z_Construct_UPackage__Script_Reflection();
		static UScriptStruct* ReturnStruct = FindExistingStructIfHotReloadOrDynamic(Outer, TEXT("RSettings"), sizeof(FRSettings), Get_Z_Construct_UScriptStruct_FRSettings_Hash(), false);
#else
		static UScriptStruct* ReturnStruct = nullptr;
#endif
		if (!ReturnStruct)
		{
			UE4CodeGen_Private::ConstructUScriptStruct(ReturnStruct, Z_Construct_UScriptStruct_FRSettings_Statics::ReturnStructParams);
		}
		return ReturnStruct;
	}
	uint32 Get_Z_Construct_UScriptStruct_FRSettings_Hash() { return 486821424U; }
	void UReflectionSettings::StaticRegisterNativesUReflectionSettings()
	{
	}
	UClass* Z_Construct_UClass_UReflectionSettings_NoRegister()
	{
		return UReflectionSettings::StaticClass();
	}
	struct Z_Construct_UClass_UReflectionSettings_Statics
	{
		static UObject* (*const DependentSingletons[])();
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_EnableExperiments_MetaData[];
#endif
		static void NewProp_EnableExperiments_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_EnableExperiments;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_AssetSettings_MetaData[];
#endif
		static const UE4CodeGen_Private::FStructPropertyParams NewProp_AssetSettings;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_Redirectors_MetaData[];
#endif
		static const UE4CodeGen_Private::FArrayPropertyParams NewProp_Redirectors;
		static const UE4CodeGen_Private::FStructPropertyParams NewProp_Redirectors_Inner;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UReflectionSettings_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UDeveloperSettings,
		(UObject* (*)())Z_Construct_UPackage__Script_Reflection,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UReflectionSettings_Statics::Class_MetaDataParams[] = {
		{ "Comment", "/* Reconstruction Toolkit for Unreal Engine */" },
		{ "IncludePath", "Settings/ReflectionSettings.h" },
		{ "ModuleRelativePath", "Public/Settings/ReflectionSettings.h" },
		{ "ToolTip", "Reconstruction Toolkit for Unreal Engine" },
	};
#endif
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UReflectionSettings_Statics::NewProp_EnableExperiments_MetaData[] = {
		{ "Category", "Settings" },
		{ "Comment", "/* Enables experimental/developing features. Features may not work as intended. */" },
		{ "DisplayName", "Experiments" },
		{ "ModuleRelativePath", "Public/Settings/ReflectionSettings.h" },
		{ "ToolTip", "Enables experimental/developing features. Features may not work as intended." },
	};
#endif
	void Z_Construct_UClass_UReflectionSettings_Statics::NewProp_EnableExperiments_SetBit(void* Obj)
	{
		((UReflectionSettings*)Obj)->EnableExperiments = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UClass_UReflectionSettings_Statics::NewProp_EnableExperiments = { "EnableExperiments", nullptr, (EPropertyFlags)0x0010040000004001, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(UReflectionSettings), &Z_Construct_UClass_UReflectionSettings_Statics::NewProp_EnableExperiments_SetBit, METADATA_PARAMS(Z_Construct_UClass_UReflectionSettings_Statics::NewProp_EnableExperiments_MetaData, ARRAY_COUNT(Z_Construct_UClass_UReflectionSettings_Statics::NewProp_EnableExperiments_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UReflectionSettings_Statics::NewProp_AssetSettings_MetaData[] = {
		{ "Category", "Settings" },
		{ "ModuleRelativePath", "Public/Settings/ReflectionSettings.h" },
	};
#endif
	const UE4CodeGen_Private::FStructPropertyParams Z_Construct_UClass_UReflectionSettings_Statics::NewProp_AssetSettings = { "AssetSettings", nullptr, (EPropertyFlags)0x0010000000004001, UE4CodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(UReflectionSettings, AssetSettings), Z_Construct_UScriptStruct_FRSettings, METADATA_PARAMS(Z_Construct_UClass_UReflectionSettings_Statics::NewProp_AssetSettings_MetaData, ARRAY_COUNT(Z_Construct_UClass_UReflectionSettings_Statics::NewProp_AssetSettings_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UReflectionSettings_Statics::NewProp_Redirectors_MetaData[] = {
		{ "Category", "Redirectors" },
		{ "ModuleRelativePath", "Public/Settings/ReflectionSettings.h" },
		{ "TitleProperty", "Name" },
	};
#endif
	const UE4CodeGen_Private::FArrayPropertyParams Z_Construct_UClass_UReflectionSettings_Statics::NewProp_Redirectors = { "Redirectors", nullptr, (EPropertyFlags)0x0010000000004001, UE4CodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(UReflectionSettings, Redirectors), METADATA_PARAMS(Z_Construct_UClass_UReflectionSettings_Statics::NewProp_Redirectors_MetaData, ARRAY_COUNT(Z_Construct_UClass_UReflectionSettings_Statics::NewProp_Redirectors_MetaData)) };
	const UE4CodeGen_Private::FStructPropertyParams Z_Construct_UClass_UReflectionSettings_Statics::NewProp_Redirectors_Inner = { "Redirectors", nullptr, (EPropertyFlags)0x0000000000004000, UE4CodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, 1, 0, Z_Construct_UScriptStruct_FRRedirector, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UReflectionSettings_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UReflectionSettings_Statics::NewProp_EnableExperiments,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UReflectionSettings_Statics::NewProp_AssetSettings,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UReflectionSettings_Statics::NewProp_Redirectors,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UReflectionSettings_Statics::NewProp_Redirectors_Inner,
	};
	const FCppClassTypeInfoStatic Z_Construct_UClass_UReflectionSettings_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UReflectionSettings>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UReflectionSettings_Statics::ClassParams = {
		&UReflectionSettings::StaticClass,
		"EditorPerProjectUserSettings",
		&StaticCppClassTypeInfo,
		DependentSingletons,
		nullptr,
		Z_Construct_UClass_UReflectionSettings_Statics::PropPointers,
		nullptr,
		ARRAY_COUNT(DependentSingletons),
		0,
		ARRAY_COUNT(Z_Construct_UClass_UReflectionSettings_Statics::PropPointers),
		0,
		0x001000A6u,
		METADATA_PARAMS(Z_Construct_UClass_UReflectionSettings_Statics::Class_MetaDataParams, ARRAY_COUNT(Z_Construct_UClass_UReflectionSettings_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UReflectionSettings()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UReflectionSettings_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UReflectionSettings, 1732414155);
	template<> REFLECTION_API UClass* StaticClass<UReflectionSettings>()
	{
		return UReflectionSettings::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UReflectionSettings(Z_Construct_UClass_UReflectionSettings, &UReflectionSettings::StaticClass, TEXT("/Script/Reflection"), TEXT("UReflectionSettings"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UReflectionSettings);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
