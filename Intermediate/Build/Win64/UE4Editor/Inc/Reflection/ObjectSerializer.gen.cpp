// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "Reflection/Public/Serializers/ObjectSerializer.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeObjectSerializer() {}
// Cross Module References
	REFLECTION_API UClass* Z_Construct_UClass_UObjectSerializer_NoRegister();
	REFLECTION_API UClass* Z_Construct_UClass_UObjectSerializer();
	COREUOBJECT_API UClass* Z_Construct_UClass_UObject();
	UPackage* Z_Construct_UPackage__Script_Reflection();
	REFLECTION_API UClass* Z_Construct_UClass_UPropertySerializer_NoRegister();
	COREUOBJECT_API UClass* Z_Construct_UClass_UObject_NoRegister();
// End Cross Module References
	void UObjectSerializer::StaticRegisterNativesUObjectSerializer()
	{
	}
	UClass* Z_Construct_UClass_UObjectSerializer_NoRegister()
	{
		return UObjectSerializer::StaticClass();
	}
	struct Z_Construct_UClass_UObjectSerializer_Statics
	{
		static UObject* (*const DependentSingletons[])();
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_WhitelistedTypes_MetaData[];
#endif
		static const UE4CodeGen_Private::FArrayPropertyParams NewProp_WhitelistedTypes;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_WhitelistedTypes_Inner;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ExportsToNotDeserialize_MetaData[];
#endif
		static const UE4CodeGen_Private::FArrayPropertyParams NewProp_ExportsToNotDeserialize;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ExportsToNotDeserialize_Inner;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_PropertySerializer_MetaData[];
#endif
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_PropertySerializer;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ConstructedObjects_MetaData[];
#endif
		static const UE4CodeGen_Private::FMapPropertyParams NewProp_ConstructedObjects;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ConstructedObjects_Key_KeyProp;
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_ConstructedObjects_ValueProp;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_Parent_MetaData[];
#endif
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Parent;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UObjectSerializer_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UObject,
		(UObject* (*)())Z_Construct_UPackage__Script_Reflection,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UObjectSerializer_Statics::Class_MetaDataParams[] = {
		{ "IncludePath", "Serializers/ObjectSerializer.h" },
		{ "ModuleRelativePath", "Public/Serializers/ObjectSerializer.h" },
	};
#endif
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UObjectSerializer_Statics::NewProp_WhitelistedTypes_MetaData[] = {
		{ "ModuleRelativePath", "Public/Serializers/ObjectSerializer.h" },
	};
#endif
	const UE4CodeGen_Private::FArrayPropertyParams Z_Construct_UClass_UObjectSerializer_Statics::NewProp_WhitelistedTypes = { "WhitelistedTypes", nullptr, (EPropertyFlags)0x0010000000000000, UE4CodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(UObjectSerializer, WhitelistedTypes), METADATA_PARAMS(Z_Construct_UClass_UObjectSerializer_Statics::NewProp_WhitelistedTypes_MetaData, ARRAY_COUNT(Z_Construct_UClass_UObjectSerializer_Statics::NewProp_WhitelistedTypes_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UClass_UObjectSerializer_Statics::NewProp_WhitelistedTypes_Inner = { "WhitelistedTypes", nullptr, (EPropertyFlags)0x0000000000000000, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, 0, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ExportsToNotDeserialize_MetaData[] = {
		{ "ModuleRelativePath", "Public/Serializers/ObjectSerializer.h" },
	};
#endif
	const UE4CodeGen_Private::FArrayPropertyParams Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ExportsToNotDeserialize = { "ExportsToNotDeserialize", nullptr, (EPropertyFlags)0x0010000000000000, UE4CodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(UObjectSerializer, ExportsToNotDeserialize), METADATA_PARAMS(Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ExportsToNotDeserialize_MetaData, ARRAY_COUNT(Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ExportsToNotDeserialize_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ExportsToNotDeserialize_Inner = { "ExportsToNotDeserialize", nullptr, (EPropertyFlags)0x0000000000000000, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, 0, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UObjectSerializer_Statics::NewProp_PropertySerializer_MetaData[] = {
		{ "ModuleRelativePath", "Public/Serializers/ObjectSerializer.h" },
	};
#endif
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UClass_UObjectSerializer_Statics::NewProp_PropertySerializer = { "PropertySerializer", nullptr, (EPropertyFlags)0x0010000000000000, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(UObjectSerializer, PropertySerializer), Z_Construct_UClass_UPropertySerializer_NoRegister, METADATA_PARAMS(Z_Construct_UClass_UObjectSerializer_Statics::NewProp_PropertySerializer_MetaData, ARRAY_COUNT(Z_Construct_UClass_UObjectSerializer_Statics::NewProp_PropertySerializer_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ConstructedObjects_MetaData[] = {
		{ "ModuleRelativePath", "Public/Serializers/ObjectSerializer.h" },
	};
#endif
	const UE4CodeGen_Private::FMapPropertyParams Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ConstructedObjects = { "ConstructedObjects", nullptr, (EPropertyFlags)0x0010000000000000, UE4CodeGen_Private::EPropertyGenFlags::Map, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(UObjectSerializer, ConstructedObjects), METADATA_PARAMS(Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ConstructedObjects_MetaData, ARRAY_COUNT(Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ConstructedObjects_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ConstructedObjects_Key_KeyProp = { "ConstructedObjects_Key", nullptr, (EPropertyFlags)0x0000000000000000, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, 0, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ConstructedObjects_ValueProp = { "ConstructedObjects", nullptr, (EPropertyFlags)0x0000000000000000, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, 1, Z_Construct_UClass_UObject_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UObjectSerializer_Statics::NewProp_Parent_MetaData[] = {
		{ "ModuleRelativePath", "Public/Serializers/ObjectSerializer.h" },
	};
#endif
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UClass_UObjectSerializer_Statics::NewProp_Parent = { "Parent", nullptr, (EPropertyFlags)0x0010000000000000, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(UObjectSerializer, Parent), Z_Construct_UClass_UObject_NoRegister, METADATA_PARAMS(Z_Construct_UClass_UObjectSerializer_Statics::NewProp_Parent_MetaData, ARRAY_COUNT(Z_Construct_UClass_UObjectSerializer_Statics::NewProp_Parent_MetaData)) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UObjectSerializer_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UObjectSerializer_Statics::NewProp_WhitelistedTypes,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UObjectSerializer_Statics::NewProp_WhitelistedTypes_Inner,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ExportsToNotDeserialize,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ExportsToNotDeserialize_Inner,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UObjectSerializer_Statics::NewProp_PropertySerializer,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ConstructedObjects,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ConstructedObjects_Key_KeyProp,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UObjectSerializer_Statics::NewProp_ConstructedObjects_ValueProp,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UObjectSerializer_Statics::NewProp_Parent,
	};
	const FCppClassTypeInfoStatic Z_Construct_UClass_UObjectSerializer_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UObjectSerializer>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UObjectSerializer_Statics::ClassParams = {
		&UObjectSerializer::StaticClass,
		nullptr,
		&StaticCppClassTypeInfo,
		DependentSingletons,
		nullptr,
		Z_Construct_UClass_UObjectSerializer_Statics::PropPointers,
		nullptr,
		ARRAY_COUNT(DependentSingletons),
		0,
		ARRAY_COUNT(Z_Construct_UClass_UObjectSerializer_Statics::PropPointers),
		0,
		0x001000A0u,
		METADATA_PARAMS(Z_Construct_UClass_UObjectSerializer_Statics::Class_MetaDataParams, ARRAY_COUNT(Z_Construct_UClass_UObjectSerializer_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UObjectSerializer()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UObjectSerializer_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UObjectSerializer, 2328099788);
	template<> REFLECTION_API UClass* StaticClass<UObjectSerializer>()
	{
		return UObjectSerializer::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UObjectSerializer(Z_Construct_UClass_UObjectSerializer, &UObjectSerializer::StaticClass, TEXT("/Script/Reflection"), TEXT("UObjectSerializer"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UObjectSerializer);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
