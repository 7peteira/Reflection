// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "Reflection/Public/Modules/EngineFactory.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeEngineFactory() {}
// Cross Module References
	REFLECTION_API UClass* Z_Construct_UClass_UREngineImplementation_NoRegister();
	REFLECTION_API UClass* Z_Construct_UClass_UREngineImplementation();
	UNREALED_API UClass* Z_Construct_UClass_UFactory();
	UPackage* Z_Construct_UPackage__Script_Reflection();
// End Cross Module References
	void UREngineImplementation::StaticRegisterNativesUREngineImplementation()
	{
	}
	UClass* Z_Construct_UClass_UREngineImplementation_NoRegister()
	{
		return UREngineImplementation::StaticClass();
	}
	struct Z_Construct_UClass_UREngineImplementation_Statics
	{
		static UObject* (*const DependentSingletons[])();
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UREngineImplementation_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UFactory,
		(UObject* (*)())Z_Construct_UPackage__Script_Reflection,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UREngineImplementation_Statics::Class_MetaDataParams[] = {
		{ "Comment", "/* Engine implementation to allow importing by dragging files into the Content Browser */" },
		{ "IncludePath", "Modules/EngineFactory.h" },
		{ "ModuleRelativePath", "Public/Modules/EngineFactory.h" },
		{ "ToolTip", "Engine implementation to allow importing by dragging files into the Content Browser" },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UREngineImplementation_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UREngineImplementation>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UREngineImplementation_Statics::ClassParams = {
		&UREngineImplementation::StaticClass,
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
		METADATA_PARAMS(Z_Construct_UClass_UREngineImplementation_Statics::Class_MetaDataParams, ARRAY_COUNT(Z_Construct_UClass_UREngineImplementation_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UREngineImplementation()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UREngineImplementation_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UREngineImplementation, 879020083);
	template<> REFLECTION_API UClass* StaticClass<UREngineImplementation>()
	{
		return UREngineImplementation::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UREngineImplementation(Z_Construct_UClass_UREngineImplementation, &UREngineImplementation::StaticClass, TEXT("/Script/Reflection"), TEXT("UREngineImplementation"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UREngineImplementation);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
