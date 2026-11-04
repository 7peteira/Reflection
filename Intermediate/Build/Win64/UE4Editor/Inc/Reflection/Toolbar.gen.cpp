// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "Reflection/Public/Modules/Toolbar/Toolbar.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeToolbar() {}
// Cross Module References
	REFLECTION_API UClass* Z_Construct_UClass_UReflectionToolbar_NoRegister();
	REFLECTION_API UClass* Z_Construct_UClass_UReflectionToolbar();
	COREUOBJECT_API UClass* Z_Construct_UClass_UObject();
	UPackage* Z_Construct_UPackage__Script_Reflection();
// End Cross Module References
	void UReflectionToolbar::StaticRegisterNativesUReflectionToolbar()
	{
	}
	UClass* Z_Construct_UClass_UReflectionToolbar_NoRegister()
	{
		return UReflectionToolbar::StaticClass();
	}
	struct Z_Construct_UClass_UReflectionToolbar_Statics
	{
		static UObject* (*const DependentSingletons[])();
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UReflectionToolbar_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UObject,
		(UObject* (*)())Z_Construct_UPackage__Script_Reflection,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UReflectionToolbar_Statics::Class_MetaDataParams[] = {
		{ "Comment", "/* 4.25 and below build this module without the engine's shared PCH (see Reflection.Build.cs),\n * which is where the menu types below used to come in from. FToolMenuSection is only named at\n * all on the versions that have ToolMenus, so 4.24 and 4.25 are the ones that need it. */" },
		{ "IncludePath", "Modules/Toolbar/Toolbar.h" },
		{ "ModuleRelativePath", "Public/Modules/Toolbar/Toolbar.h" },
		{ "ToolTip", "4.25 and below build this module without the engine's shared PCH (see Reflection.Build.cs),\n* which is where the menu types below used to come in from. FToolMenuSection is only named at\n* all on the versions that have ToolMenus, so 4.24 and 4.25 are the ones that need it." },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UReflectionToolbar_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UReflectionToolbar>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UReflectionToolbar_Statics::ClassParams = {
		&UReflectionToolbar::StaticClass,
		nullptr,
		&StaticCppClassTypeInfo,
		DependentSingletons,
		nullptr,
		nullptr,
		nullptr,
		ARRAY_COUNT(DependentSingletons),
		0,
		0,
		0,
		0x001000A0u,
		METADATA_PARAMS(Z_Construct_UClass_UReflectionToolbar_Statics::Class_MetaDataParams, ARRAY_COUNT(Z_Construct_UClass_UReflectionToolbar_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UReflectionToolbar()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UReflectionToolbar_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UReflectionToolbar, 3031011045);
	template<> REFLECTION_API UClass* StaticClass<UReflectionToolbar>()
	{
		return UReflectionToolbar::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UReflectionToolbar(Z_Construct_UClass_UReflectionToolbar, &UReflectionToolbar::StaticClass, TEXT("/Script/Reflection"), TEXT("UReflectionToolbar"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UReflectionToolbar);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
