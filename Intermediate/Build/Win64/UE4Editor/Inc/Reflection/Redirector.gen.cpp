// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "Reflection/Public/Settings/Redirector.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeRedirector() {}
// Cross Module References
	REFLECTION_API UScriptStruct* Z_Construct_UScriptStruct_FRRedirector();
	UPackage* Z_Construct_UPackage__Script_Reflection();
	REFLECTION_API UScriptStruct* Z_Construct_UScriptStruct_FRRedirectorPoint();
// End Cross Module References
class UScriptStruct* FRRedirector::StaticStruct()
{
	static class UScriptStruct* Singleton = NULL;
	if (!Singleton)
	{
		extern REFLECTION_API uint32 Get_Z_Construct_UScriptStruct_FRRedirector_Hash();
		Singleton = GetStaticStruct(Z_Construct_UScriptStruct_FRRedirector, Z_Construct_UPackage__Script_Reflection(), TEXT("RRedirector"), sizeof(FRRedirector), Get_Z_Construct_UScriptStruct_FRRedirector_Hash());
	}
	return Singleton;
}
template<> REFLECTION_API UScriptStruct* StaticStruct<FRRedirector>()
{
	return FRRedirector::StaticStruct();
}
static FCompiledInDeferStruct Z_CompiledInDeferStruct_UScriptStruct_FRRedirector(FRRedirector::StaticStruct, TEXT("/Script/Reflection"), TEXT("RRedirector"), false, nullptr, nullptr);
static struct FScriptStruct_Reflection_StaticRegisterNativesFRRedirector
{
	FScriptStruct_Reflection_StaticRegisterNativesFRRedirector()
	{
		UScriptStruct::DeferCppStructOps(FName(TEXT("RRedirector")),new UScriptStruct::TCppStructOps<FRRedirector>);
	}
} ScriptStruct_Reflection_StaticRegisterNativesFRRedirector;
	struct Z_Construct_UScriptStruct_FRRedirector_Statics
	{
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[];
#endif
		static void* NewStructOps();
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_Profiles_MetaData[];
#endif
		static const UE4CodeGen_Private::FArrayPropertyParams NewProp_Profiles;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_Profiles_Inner;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_Points_MetaData[];
#endif
		static const UE4CodeGen_Private::FArrayPropertyParams NewProp_Points;
		static const UE4CodeGen_Private::FStructPropertyParams NewProp_Points_Inner;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_Enable_MetaData[];
#endif
		static void NewProp_Enable_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_Enable;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_Name_MetaData[];
#endif
		static const UE4CodeGen_Private::FNamePropertyParams NewProp_Name;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
		static const UE4CodeGen_Private::FStructParams ReturnStructParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRRedirector_Statics::Struct_MetaDataParams[] = {
		{ "ModuleRelativePath", "Public/Settings/Redirector.h" },
	};
#endif
	void* Z_Construct_UScriptStruct_FRRedirector_Statics::NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FRRedirector>();
	}
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Profiles_MetaData[] = {
		{ "Category", "Metadata" },
		{ "Comment", "/* Optional whitelist of profile names that can use this redirector.\n\x09 * Compared against the current cloud profile (exact match).\n\x09 *\n\x09 * If empty, the redirector is enabled for all profiles. */" },
		{ "ModuleRelativePath", "Public/Settings/Redirector.h" },
		{ "ToolTip", "Optional whitelist of profile names that can use this redirector.\n       * Compared against the current cloud profile (exact match).\n       *\n       * If empty, the redirector is enabled for all profiles." },
	};
#endif
	const UE4CodeGen_Private::FArrayPropertyParams Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Profiles = { "Profiles", nullptr, (EPropertyFlags)0x0040000000004001, UE4CodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(FRRedirector, Profiles), METADATA_PARAMS(Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Profiles_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Profiles_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Profiles_Inner = { "Profiles", nullptr, (EPropertyFlags)0x0000000000004000, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, 0, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Points_MetaData[] = {
		{ "Category", "Redirector" },
		{ "Comment", "/* Ordered list of redirect rules applied to the path.\n\x09 * Each point can modify the result of the previous one. */" },
		{ "ModuleRelativePath", "Public/Settings/Redirector.h" },
		{ "ToolTip", "Ordered list of redirect rules applied to the path.\n       * Each point can modify the result of the previous one." },
	};
#endif
	const UE4CodeGen_Private::FArrayPropertyParams Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Points = { "Points", nullptr, (EPropertyFlags)0x0010000000004001, UE4CodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(FRRedirector, Points), METADATA_PARAMS(Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Points_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Points_MetaData)) };
	const UE4CodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Points_Inner = { "Points", nullptr, (EPropertyFlags)0x0000000000004000, UE4CodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, 1, 0, Z_Construct_UScriptStruct_FRRedirectorPoint, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Enable_MetaData[] = {
		{ "Category", "Redirector" },
		{ "Comment", "/* Master toggle for this redirector.\n\x09 * If false, this redirector is always disabled regardless of any other setting. */" },
		{ "ModuleRelativePath", "Public/Settings/Redirector.h" },
		{ "ToolTip", "Master toggle for this redirector.\n       * If false, this redirector is always disabled regardless of any other setting." },
	};
#endif
	void Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Enable_SetBit(void* Obj)
	{
		((FRRedirector*)Obj)->Enable = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Enable = { "Enable", nullptr, (EPropertyFlags)0x0040000000004001, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(FRRedirector), &Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Enable_SetBit, METADATA_PARAMS(Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Enable_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Enable_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Name_MetaData[] = {
		{ "Category", "Redirector" },
		{ "Comment", "/* The name of this redirector. */" },
		{ "ModuleRelativePath", "Public/Settings/Redirector.h" },
		{ "ToolTip", "The name of this redirector." },
	};
#endif
	const UE4CodeGen_Private::FNamePropertyParams Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Name = { "Name", nullptr, (EPropertyFlags)0x0040000000004001, UE4CodeGen_Private::EPropertyGenFlags::Name, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(FRRedirector, Name), METADATA_PARAMS(Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Name_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Name_MetaData)) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FRRedirector_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Profiles,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Profiles_Inner,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Points,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Points_Inner,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Enable,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRRedirector_Statics::NewProp_Name,
	};
	const UE4CodeGen_Private::FStructParams Z_Construct_UScriptStruct_FRRedirector_Statics::ReturnStructParams = {
		(UObject* (*)())Z_Construct_UPackage__Script_Reflection,
		nullptr,
		&NewStructOps,
		"RRedirector",
		sizeof(FRRedirector),
		alignof(FRRedirector),
		Z_Construct_UScriptStruct_FRRedirector_Statics::PropPointers,
		ARRAY_COUNT(Z_Construct_UScriptStruct_FRRedirector_Statics::PropPointers),
		RF_Public|RF_Transient|RF_MarkAsNative,
		EStructFlags(0x00000001),
		METADATA_PARAMS(Z_Construct_UScriptStruct_FRRedirector_Statics::Struct_MetaDataParams, ARRAY_COUNT(Z_Construct_UScriptStruct_FRRedirector_Statics::Struct_MetaDataParams))
	};
	UScriptStruct* Z_Construct_UScriptStruct_FRRedirector()
	{
#if WITH_HOT_RELOAD
		extern uint32 Get_Z_Construct_UScriptStruct_FRRedirector_Hash();
		UPackage* Outer = Z_Construct_UPackage__Script_Reflection();
		static UScriptStruct* ReturnStruct = FindExistingStructIfHotReloadOrDynamic(Outer, TEXT("RRedirector"), sizeof(FRRedirector), Get_Z_Construct_UScriptStruct_FRRedirector_Hash(), false);
#else
		static UScriptStruct* ReturnStruct = nullptr;
#endif
		if (!ReturnStruct)
		{
			UE4CodeGen_Private::ConstructUScriptStruct(ReturnStruct, Z_Construct_UScriptStruct_FRRedirector_Statics::ReturnStructParams);
		}
		return ReturnStruct;
	}
	uint32 Get_Z_Construct_UScriptStruct_FRRedirector_Hash() { return 2932529730U; }
class UScriptStruct* FRRedirectorPoint::StaticStruct()
{
	static class UScriptStruct* Singleton = NULL;
	if (!Singleton)
	{
		extern REFLECTION_API uint32 Get_Z_Construct_UScriptStruct_FRRedirectorPoint_Hash();
		Singleton = GetStaticStruct(Z_Construct_UScriptStruct_FRRedirectorPoint, Z_Construct_UPackage__Script_Reflection(), TEXT("RRedirectorPoint"), sizeof(FRRedirectorPoint), Get_Z_Construct_UScriptStruct_FRRedirectorPoint_Hash());
	}
	return Singleton;
}
template<> REFLECTION_API UScriptStruct* StaticStruct<FRRedirectorPoint>()
{
	return FRRedirectorPoint::StaticStruct();
}
static FCompiledInDeferStruct Z_CompiledInDeferStruct_UScriptStruct_FRRedirectorPoint(FRRedirectorPoint::StaticStruct, TEXT("/Script/Reflection"), TEXT("RRedirectorPoint"), false, nullptr, nullptr);
static struct FScriptStruct_Reflection_StaticRegisterNativesFRRedirectorPoint
{
	FScriptStruct_Reflection_StaticRegisterNativesFRRedirectorPoint()
	{
		UScriptStruct::DeferCppStructOps(FName(TEXT("RRedirectorPoint")),new UScriptStruct::TCppStructOps<FRRedirectorPoint>);
	}
} ScriptStruct_Reflection_StaticRegisterNativesFRRedirectorPoint;
	struct Z_Construct_UScriptStruct_FRRedirectorPoint_Statics
	{
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[];
#endif
		static void* NewStructOps();
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_To_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_To;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_From_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_From;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
		static const UE4CodeGen_Private::FStructParams ReturnStructParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::Struct_MetaDataParams[] = {
		{ "Comment", "/* A point in a redirector */" },
		{ "ModuleRelativePath", "Public/Settings/Redirector.h" },
		{ "ToolTip", "A point in a redirector" },
	};
#endif
	void* Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FRRedirectorPoint>();
	}
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::NewProp_To_MetaData[] = {
		{ "Category", "RedirectorPoint" },
		{ "Comment", "/* Substring that replaces `From` when matched.\n\x09 * Replaces all occurrences in the path.\n\x09 * Example: \"/Game/B/\" */" },
		{ "ModuleRelativePath", "Public/Settings/Redirector.h" },
		{ "ToolTip", "Substring that replaces `From` when matched.\n       * Replaces all occurrences in the path.\n       * Example: \"/Game/B/\"" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::NewProp_To = { "To", nullptr, (EPropertyFlags)0x0010000000004001, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(FRRedirectorPoint, To), METADATA_PARAMS(Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::NewProp_To_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::NewProp_To_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::NewProp_From_MetaData[] = {
		{ "Category", "RedirectorPoint" },
		{ "Comment", "/* Substring to match in the path (case-sensitive).\n\x09 * Supports folder, package, or full asset paths.\n\x09 * Example: \"/Game/A/\" */" },
		{ "ModuleRelativePath", "Public/Settings/Redirector.h" },
		{ "ToolTip", "Substring to match in the path (case-sensitive).\n       * Supports folder, package, or full asset paths.\n       * Example: \"/Game/A/\"" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::NewProp_From = { "From", nullptr, (EPropertyFlags)0x0010000000004001, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(FRRedirectorPoint, From), METADATA_PARAMS(Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::NewProp_From_MetaData, ARRAY_COUNT(Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::NewProp_From_MetaData)) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::NewProp_To,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::NewProp_From,
	};
	const UE4CodeGen_Private::FStructParams Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::ReturnStructParams = {
		(UObject* (*)())Z_Construct_UPackage__Script_Reflection,
		nullptr,
		&NewStructOps,
		"RRedirectorPoint",
		sizeof(FRRedirectorPoint),
		alignof(FRRedirectorPoint),
		Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::PropPointers,
		ARRAY_COUNT(Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::PropPointers),
		RF_Public|RF_Transient|RF_MarkAsNative,
		EStructFlags(0x00000001),
		METADATA_PARAMS(Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::Struct_MetaDataParams, ARRAY_COUNT(Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::Struct_MetaDataParams))
	};
	UScriptStruct* Z_Construct_UScriptStruct_FRRedirectorPoint()
	{
#if WITH_HOT_RELOAD
		extern uint32 Get_Z_Construct_UScriptStruct_FRRedirectorPoint_Hash();
		UPackage* Outer = Z_Construct_UPackage__Script_Reflection();
		static UScriptStruct* ReturnStruct = FindExistingStructIfHotReloadOrDynamic(Outer, TEXT("RRedirectorPoint"), sizeof(FRRedirectorPoint), Get_Z_Construct_UScriptStruct_FRRedirectorPoint_Hash(), false);
#else
		static UScriptStruct* ReturnStruct = nullptr;
#endif
		if (!ReturnStruct)
		{
			UE4CodeGen_Private::ConstructUScriptStruct(ReturnStruct, Z_Construct_UScriptStruct_FRRedirectorPoint_Statics::ReturnStructParams);
		}
		return ReturnStruct;
	}
	uint32 Get_Z_Construct_UScriptStruct_FRRedirectorPoint_Hash() { return 1465338573U; }
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
