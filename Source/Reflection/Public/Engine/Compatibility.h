#pragma once

#include "Misc/EngineVersionComparison.h"

#ifndef REFLECTION_EXPERIMENTAL
#define REFLECTION_EXPERIMENTAL 0
#endif

#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/VolumeTexture.h"

#ifndef REFLECTION_CLOUD_SERVER
#define REFLECTION_CLOUD_SERVER 1
#endif

#if ENGINE_MAJOR_VERSION == 5
	#define ENGINE_UE5 1
#else
	#define ENGINE_UE5 0
#endif

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 8
	#define UE5_8_BEYOND 1
#else
	#define UE5_8_BEYOND 0
#endif

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 7
	#define UE5_7_BEYOND 1
#else
	#define UE5_7_BEYOND 0
#endif

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 6
	#define UE5_6_BEYOND 1
#else
	#define UE5_6_BEYOND 0
#endif

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 5
	#define UE5_5_BEYOND 1
#else
	#define UE5_5_BEYOND 0
#endif

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 3
	#define UE5_3_BEYOND 1
#else
	#define UE5_3_BEYOND 0
#endif

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 4
	#define UE5_4_BEYOND 1
#else
	#define UE5_4_BEYOND 0
#endif

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 1
	#define UE5_1_BEYOND 1
#else
	#define UE5_1_BEYOND 0
#endif

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 2
	#define UE5_2_BEYOND 1
#else
	#define UE5_2_BEYOND 0
#endif

#if ENGINE_MAJOR_VERSION == 4
	#define ENGINE_UE4 1
#else
	#define ENGINE_UE4 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION == 26 && ENGINE_PATCH_VERSION == 0
	#define UE4_26_0 1
#else
	#define UE4_26_0 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION == 26
	#define UE4_26 1
#else
	#define UE4_26 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION <= 27
	#define UE4_27_BELOW 1
#else
	#define UE4_27_BELOW 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION < 26
	#define UE4_25_BELOW 1
#else
	#define UE4_25_BELOW 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION < 23
	#define UE4_22_BELOW 1
#else
	#define UE4_22_BELOW 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION < 24
	#define UE4_23_BELOW 1
#else
	#define UE4_23_BELOW 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION < 25
	#define UE4_24_BELOW 1
#else
	#define UE4_24_BELOW 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION < 27
	#define UE4_27_ONLY_BELOW 1
#else
	#define UE4_27_ONLY_BELOW 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION >= 27
	#define UE4_27 1
#else
	#define UE4_27 0
#endif

#if (ENGINE_UE4 && ENGINE_MINOR_VERSION >= 27) || ENGINE_UE5
	#define UE4_27_AND_UE5 1
#else
	#define UE4_27_AND_UE5 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION <= 26
	#define UE4_26_BELOW 1
#else
	#define UE4_26_BELOW 0
#endif

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 2
	#define UE5_2_BEYOND 1
#else
	#define UE5_2_BEYOND 0
#endif

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 4
	#define UE5_4_BEYOND 1
#else
	#define UE5_4_BEYOND 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION <= 24
	#define UE4_24_BELOW 1
#else
	#define UE4_24_BELOW 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION <= 23
	#define UE4_23_BELOW 1
#else
	#define UE4_23_BELOW 0
#endif

#if ENGINE_UE4 && ENGINE_MINOR_VERSION <= 22
	#define UE4_22_BELOW 1
#else
	#define UE4_22_BELOW 0
#endif

#if ENGINE_UE5 && ENGINE_MINOR_VERSION < 2
	#define UE5_1_BELOW 1
#else
	#define UE5_1_BELOW 0
#endif

#if !UE4_23_BELOW
#include "Engine/Texture2DArray.h"
#endif

#if UE4_24_BELOW
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"

using FField = UField;
using FProperty = UProperty;
using FNumericProperty = UNumericProperty;
using FByteProperty = UByteProperty;
using FInt8Property = UInt8Property;
using FInt16Property = UInt16Property;
using FIntProperty = UIntProperty;
using FInt64Property = UInt64Property;
using FUInt16Property = UUInt16Property;
using FUInt32Property = UUInt32Property;
using FUInt64Property = UUInt64Property;
using FFloatProperty = UFloatProperty;
using FDoubleProperty = UDoubleProperty;
using FBoolProperty = UBoolProperty;
using FObjectPropertyBase = UObjectPropertyBase;
using FObjectProperty = UObjectProperty;
using FWeakObjectProperty = UWeakObjectProperty;
using FLazyObjectProperty = ULazyObjectProperty;
using FSoftObjectProperty = USoftObjectProperty;
using FClassProperty = UClassProperty;
using FSoftClassProperty = USoftClassProperty;
using FInterfaceProperty = UInterfaceProperty;
using FNameProperty = UNameProperty;
using FStrProperty = UStrProperty;
using FTextProperty = UTextProperty;
using FArrayProperty = UArrayProperty;
using FMapProperty = UMapProperty;
using FSetProperty = USetProperty;
using FStructProperty = UStructProperty;
using FEnumProperty = UEnumProperty;
using FDelegateProperty = UDelegateProperty;
using FMulticastDelegateProperty = UMulticastDelegateProperty;

using FFieldClass = UClass;

template <typename To, typename From>
FORCEINLINE To* CastField(From* Src) {
	return Cast<To>(Src);
}

template <typename To, typename From>
FORCEINLINE To* CastField(const From* Src) {
	return Cast<To>(const_cast<From*>(Src));
}

template <typename To, typename From>
FORCEINLINE To* CastFieldChecked(From* Src) {
	return CastChecked<To>(Src);
}

template <typename To, typename From>
FORCEINLINE To* CastFieldChecked(const From* Src) {
	return CastChecked<To>(const_cast<From*>(Src));
}

template <typename T>
FORCEINLINE T* FindFProperty(const UStruct* Owner, const TCHAR* FieldName) {
	return FindField<T>(Owner, FieldName);
}

template <typename T>
FORCEINLINE T* FindFProperty(const UStruct* Owner, const FName FieldName) {
	return FindField<T>(Owner, FieldName);
}
#endif

#if UE4_22_BELOW
#include "Templates/SharedPointer.h"

template <typename ObjectType, ESPMode Mode>
FORCEINLINE bool operator==(const TSharedPtr<ObjectType, Mode>& Ptr, TYPE_OF_NULLPTR) {
	return !Ptr.IsValid();
}

template <typename ObjectType, ESPMode Mode>
FORCEINLINE bool operator==(TYPE_OF_NULLPTR, const TSharedPtr<ObjectType, Mode>& Ptr) {
	return !Ptr.IsValid();
}

template <typename ObjectType, ESPMode Mode>
FORCEINLINE bool operator!=(const TSharedPtr<ObjectType, Mode>& Ptr, TYPE_OF_NULLPTR) {
	return Ptr.IsValid();
}

template <typename ObjectType, ESPMode Mode>
FORCEINLINE bool operator!=(TYPE_OF_NULLPTR, const TSharedPtr<ObjectType, Mode>& Ptr) {
	return Ptr.IsValid();
}
#endif

#include "Misc/Guid.h"
#include "UObject/NameTypes.h"

inline FName StringToName(const FString& String) {
#if UE4_24_BELOW
	return FName(*String);
#else
	return FName(String);
#endif
}

inline FString OutputNameToString(const FString& Name) {
	return Name;
}

inline FString OutputNameToString(const FName Name) {
	return Name.ToString();
}

inline void SetExpressionName(FString& Name, const FString& Value) {
	Name = Value;
}

inline void SetExpressionName(FName& Name, const FString& Value) {
	Name = StringToName(Value);
}

inline void LeftInline(FString& String, const int32 Count) {
#if UE4_24_BELOW
	String = String.Left(Count);
#else
	String.LeftInline(Count);
#endif
}

#if UE4_24_BELOW
inline UClass* ToNewObjectClass(const UClass* Class) {
	return const_cast<UClass*>(Class);
}
#else
inline const UClass* ToNewObjectClass(const UClass* Class) {
	return Class;
}
#endif

inline FGuid StringToGuid(const FString& GuidString) {
#if UE4_25_BELOW
	FGuid Guid;

	if (!FGuid::Parse(GuidString, Guid)) {
		Guid.Invalidate();
	}

	return Guid;
#else
	return FGuid(GuidString);
#endif
}

#if UE5_8_BEYOND
#include "Containers/SharedString.h"
#endif

#if UE5_8_BEYOND
inline FString JsonKeyToString(const FString& Key) {
	return Key;
}

inline FString JsonKeyToString(const UE::FSharedString& Key) {
	return FString(*Key);
}
#else
inline const FString& JsonKeyToString(const FString& Key) {
	return Key;
}
#endif

#if UE5_8_BEYOND
inline UE::FSharedString StringToJsonKey(const FString& Key) {
	return UE::FSharedString(*Key);
}
#else
inline const FString& StringToJsonKey(const FString& Key) {
	return Key;
}
#endif

#if UE4_26_0
#include "AssetRegistry/Public/AssetRegistryModule.h"
#endif

#if UE4_25_BELOW
#include "AssetRegistryModule.h"
#endif

#if (ENGINE_UE5 && ENGINE_MINOR_VERSION < 4) || ((ENGINE_UE4 && ENGINE_MINOR_VERSION >= 26) && !(ENGINE_MINOR_VERSION == 26 && ENGINE_PATCH_VERSION == 0))
#include "AssetRegistry/AssetRegistryModule.h"
#endif

#if ENGINE_UE5
#include "Animation/AnimData/IAnimationDataController.h"
#if ENGINE_MINOR_VERSION >= 4
#include "Animation/AnimData/IAnimationDataModel.h"
#endif
#include "AnimDataController.h"
#endif

#if ENGINE_UE5
#include "Styling/AppStyle.h"
using FAppStyle = FAppStyle;
#else

#include "EditorStyleSet.h"

class FAppStyle {
public:
	static const ISlateStyle& Get() {
		return FEditorStyle::Get();
	}

	static FName GetAppStyleSetName() {
		return FEditorStyle::GetStyleSetName();
	}

	static const FSlateBrush* GetBrush(const FName PropertyName) {
		return FEditorStyle::GetBrush(PropertyName);
	}
};

template <typename TObjectType>
class TObjectPtr {
private:
	TWeakObjectPtr<TObjectType> WeakPtr;

public:
	TObjectPtr() {}
	TObjectPtr(TObjectType* InObject) : WeakPtr(InObject) {}

	TObjectType* Get() const { return WeakPtr.Get(); }

	bool IsValid() const { return WeakPtr.IsValid(); }

	void Reset() { WeakPtr.Reset(); }

	void Set(TObjectType* InObject) { WeakPtr = InObject; }

	TObjectPtr(TObjectType* InObject, bool bRawPointer) : WeakPtr(InObject) {}

	operator TObjectType*() const { return Get(); }

	TObjectPtr<TObjectType>* operator&() { return this; }

	TObjectPtr& operator=(TObjectType* InObject) {
		WeakPtr = InObject;
		return *this;
	}

	bool operator==(std::nullptr_t) const { return Get() == nullptr; }
	bool operator!=(std::nullptr_t) const { return Get() != nullptr; }
};
#endif

template <typename T>
bool IsObjectPtrValid(TObjectPtr<T> ObjectPtr) {
#if ENGINE_UE5
	return ObjectPtr.Get() != nullptr;
#else
	return ObjectPtr.IsValid();
#endif
}

inline FName GetPropertyName(const FProperty* Property) {
#if UE4_24_BELOW
	return Property->GetFName();
#else
	return Property->NamePrivate;
#endif
}

inline int32 GetElementSize(FProperty* Property) {
#if ENGINE_UE5 && UE5_6_BEYOND
	return Property->GetElementSize();
#else
	return Property->ElementSize;
#endif
}

inline const UObject* GetClassDefaultObject(UClass* Class) {
#if UE5_1_BEYOND
	return GetDefault<UObject>(Class);
#else
	return Class->ClassDefaultObject;
#endif
}

#if !ENGINE_UE5
using FVector2f = FVector2D;
using FVector3f = FVector;
using FVector4f = FVector4;
#endif

inline TArray<FStaticMaterial>& GetStaticMaterials(UStaticMesh* Mesh) {
#if UE4_27_AND_UE5
	return Mesh->GetStaticMaterials();
#else
	return Mesh->StaticMaterials;
#endif
}

inline UBodySetup* GetBodySetup(const UStaticMesh* Mesh) {
#if UE4_27_AND_UE5
	return Mesh->GetBodySetup();
#else
	return Mesh->BodySetup;
#endif
}

inline FStaticMeshRenderData* GetStaticMeshRenderData(UStaticMesh* Mesh) {
#if UE4_27_AND_UE5
	return Mesh->GetRenderData();
#else
	return Mesh->RenderData.Get();
#endif
}

#if UE4_23_BELOW
#include "MeshDescription.h"
#include "MeshAttributes.h"

struct FStaticMeshAttributes {
	explicit FStaticMeshAttributes(FMeshDescription& InMeshDescription) : MeshDescription(InMeshDescription) {}

	void Register() const { UStaticMesh::RegisterMeshAttributes(MeshDescription); }

	TVertexAttributesRef<FVector> GetVertexPositions() const {
		return MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	}

	TVertexInstanceAttributesRef<FVector> GetVertexInstanceNormals() const {
		return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	}

	TVertexInstanceAttributesRef<FVector> GetVertexInstanceTangents() const {
		return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	}

	TVertexInstanceAttributesRef<float> GetVertexInstanceBinormalSigns() const {
		return MeshDescription.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
	}

	TVertexInstanceAttributesRef<FVector4> GetVertexInstanceColors() const {
		return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);
	}

	TVertexInstanceAttributesRef<FVector2D> GetVertexInstanceUVs() const {
		return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
	}

	TPolygonGroupAttributesRef<FName> GetPolygonGroupMaterialSlotNames() const {
		return MeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
	}

private:
	FMeshDescription& MeshDescription;
};
#endif

#if UE4_23_BELOW
#include "AnimGraphNode_SubInput.h"

using UAnimGraphNode_LinkedInputPose = UAnimGraphNode_SubInput;
#endif

inline int32 NumSourceModelsOf(UStaticMesh* Mesh) {
#if UE4_23_BELOW
	return Mesh->SourceModels.Num();
#else
	return Mesh->GetNumSourceModels();
#endif
}

inline FStaticMeshSourceModel& SourceModelOf(UStaticMesh* Mesh, const int32 Lod) {
#if UE4_23_BELOW
	return Mesh->SourceModels[Lod];
#else
	return Mesh->GetSourceModel(Lod);
#endif
}

inline FMeshSectionInfoMap& SectionInfoMapOf(UStaticMesh* Mesh) {
#if UE4_23_BELOW
	return Mesh->SectionInfoMap;
#else
	return Mesh->GetSectionInfoMap();
#endif
}

inline UClass* FindClassByType(const FString& Type) {
#if UE5_1_BEYOND
	UClass* Class = FindFirstObject<UClass>(*Type);
#else
	UClass* Class = FindObject<UClass>(ANY_PACKAGE, *Type);
#endif

	return Class;
}

inline UScriptStruct* FindStructByType(const FString& Type) {
#if UE5_1_BEYOND
	UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*Type);
#else
	UScriptStruct* Struct = FindObject<UScriptStruct>(ANY_PACKAGE, *Type);
#endif

	return Struct;
}

inline UEnum* FindEnumByType(const FString& Type) {
#if UE5_1_BEYOND
	UEnum* Enum = FindFirstObject<UEnum>(*Type);
#else
	UEnum* Enum = FindObject<UEnum>(ANY_PACKAGE, *Type);
#endif

	return Enum;
}

inline FTexturePlatformData* GetPlatformData(UTexture* Texture) {
	if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture)) {
#if ENGINE_UE5
		return Texture2D->GetPlatformData();
#else
		return Texture2D->PlatformData;
#endif
	}

	if (UTextureCube* TextureCube = Cast<UTextureCube>(Texture)) {
#if ENGINE_UE5
		return TextureCube->GetPlatformData();
#else
		return TextureCube->PlatformData;
#endif
	}

	if (UVolumeTexture* VolumeTexture = Cast<UVolumeTexture>(Texture)) {
#if UE5_1_BEYOND
		return VolumeTexture->GetPlatformData();
#else
		return VolumeTexture->PlatformData;
#endif
	}

#if !UE4_23_BELOW
	if (UTexture2DArray* Texture2DArray = Cast<UTexture2DArray>(Texture)) {
#if UE5_1_BEYOND
		return Texture2DArray->GetPlatformData();
#else
		return Texture2DArray->PlatformData;
#endif
	}
#endif

	return nullptr;
}

inline void SetPlatformData(UTexture* Texture, FTexturePlatformData* PlatformData) {
	if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture)) {
#if ENGINE_UE5
		Texture2D->SetPlatformData(PlatformData);
#else
		Texture2D->PlatformData = PlatformData;
#endif
	}

	if (UTextureCube* TextureCube = Cast<UTextureCube>(Texture)) {
#if ENGINE_UE5
		TextureCube->SetPlatformData(PlatformData);
#else
		TextureCube->PlatformData = PlatformData;
#endif
	}

	if (UVolumeTexture* VolumeTexture = Cast<UVolumeTexture>(Texture)) {
#if UE5_1_BEYOND
		VolumeTexture->SetPlatformData(PlatformData);
#else
		VolumeTexture->PlatformData = PlatformData;
#endif
	}

#if !UE4_23_BELOW
	if (UTexture2DArray* Texture2DArray = Cast<UTexture2DArray>(Texture)) {
#if UE5_1_BEYOND
		Texture2DArray->SetPlatformData(PlatformData);
#else
		Texture2DArray->PlatformData = PlatformData;
#endif
	}
#endif
}

#if UE4_25_BELOW
#include "Animation/AnimSequence.h"
#endif

inline void UpdateAnimationCaching(UAnimSequenceBase* AnimationSequenceBase) {
	if (UAnimSequence* AnimationSequence = Cast<UAnimSequence>(AnimationSequenceBase)) {
#if UE5_2_BEYOND
		if (ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform()) {
#if UE5_6_BEYOND
			AnimationSequence->CacheDerivedDataForPlatform(RunningPlatform);
#else
			AnimationSequence->CacheDerivedData(RunningPlatform);
#endif
		}
#else
		if (AnimationSequence) {
			AnimationSequence->RequestSyncAnimRecompression();
		}
#endif
	}

#if ENGINE_UE4
    AnimationSequenceBase->MarkRawDataAsModified();
#endif
}
