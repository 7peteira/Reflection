#include "Engine/SharedPtrCompatibility.h"
#include "Serializers/PropertySerializer.h"

#include "GameplayTagContainer.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "Importers/Constructor/Importer.h"
#include "Serializers/ObjectSerializer.h"
#include "UObject/TextProperty.h"
#include "Misc/Base64.h"

#include "Distributions.h"

#if ENGINE_UE4
#include "MaterialExpressionIO.h"
#endif

#include "Materials/MaterialExpression.h"

#include "MovieSceneSection.h"
#include "Engine/FontFace.h"
#include "Importers/Types/Cascade/ParticleSystemDecooking.h"
#include "Serializers/Structs/DateTimeSerializer.h"
#include "Serializers/Structs/FallbackStructSerializer.h"
#include "Serializers/Structs/ConnectivityCubeSerializer.h"
#include "Serializers/Structs/NiagaraVariableSerializer.h"
#include "Serializers/Structs/TimeSpanSerializer.h"

#include "Settings/Runtime.h"
#include "Utilities/AssetPaths.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionPropertySerializer, Log, All);

namespace {
	bool AlreadyHere(const FSoftObjectPath& Reference) {
		if (Reference.ResolveObject() != nullptr) {
			return true;
		}

		FString Held;

		return PackageFileOf(Reference.GetLongPackageName(), Held);
	}

	TArray<UObject*> GBeingFilled;

	struct FFillScope {
		explicit FFillScope(UObject* InObject) : Object(InObject) { GBeingFilled.Add(Object); }

		~FFillScope() { GBeingFilled.Remove(Object); }

		UObject* Object;
	};
}

static FGameplayTag RequestOrMakeGameplayTag(const FString& Named) {
	if (Named.IsEmpty() || Named == TEXT("None")) return FGameplayTag();

	FGameplayTag Asked = FGameplayTag::RequestGameplayTag(FName(*Named), false);

	if (Asked.IsValid()) return Asked;

#if WITH_EDITOR
	if (IGameplayTagsEditorModule* Tags = FModuleManager::Get().LoadModulePtr<IGameplayTagsEditorModule>(TEXT("GameplayTagsEditor"))) {
		if (Tags->AddNewGameplayTagToINI(Named, TEXT(""))) {
			Asked = FGameplayTag::RequestGameplayTag(FName(*Named), false);

			UE_LOG(LogReflectionPropertySerializer, Display, TEXT("\"%s\" is a tag this project hadn't got, so it was added to the list"), *Named);
		}
	}
#endif

	if (!Asked.IsValid()) {
		UE_LOG(LogReflectionPropertySerializer, Warning, TEXT("\"%s\" is a tag this project hasn't got, and it could not be added"), *Named);
	}

	return Asked;
}

namespace {
	constexpr int32 ToVersionKey(const int32 Major, const int32 Minor) {
		return Major * 100 + Minor;
	}

	struct FBreakAttributeSlot {
		const TCHAR* Name;

		int32 First;
		int32 Last;

		bool IsPresentIn(const int32 Version) const {
			return (First == 0 || Version >= First) && (Last == 0 || Version <= Last);
		}
	};

	const FBreakAttributeSlot BreakAttributeSlots[] = {
		{ TEXT("BaseColor"),              0,   0   },
		{ TEXT("Metallic"),               0,   0   },
		{ TEXT("Specular"),               0,   0   },
		{ TEXT("Roughness"),              0,   0   },
		{ TEXT("Anisotropy"),             425, 0   },
		{ TEXT("EmissiveColor"),          0,   0   },
		{ TEXT("Opacity"),                0,   0   },
		{ TEXT("OpacityMask"),            0,   0   },
		{ TEXT("Normal"),                 0,   0   },
		{ TEXT("Tangent"),                425, 0   },
		{ TEXT("WorldPositionOffset"),    0,   0   },
		{ TEXT("WorldDisplacement"),      0,   427 },
		{ TEXT("TessellationMultiplier"), 0,   427 },
		{ TEXT("SubsurfaceColor"),        0,   0   },
		{ TEXT("ClearCoat"),              0,   0   },
		{ TEXT("ClearCoatRoughness"),     0,   0   },
		{ TEXT("AmbientOcclusion"),       0,   0   },
		{ TEXT("Refraction"),             0,   0   },
		{ TEXT("CustomizedUV0"),          0,   0   },
		{ TEXT("CustomizedUV1"),          0,   0   },
		{ TEXT("CustomizedUV2"),          0,   0   },
		{ TEXT("CustomizedUV3"),          0,   0   },
		{ TEXT("CustomizedUV4"),          0,   0   },
		{ TEXT("CustomizedUV5"),          0,   0   },
		{ TEXT("CustomizedUV6"),          0,   0   },
		{ TEXT("CustomizedUV7"),          0,   0   },
		{ TEXT("PixelDepthOffset"),       0,   0   },
		{ TEXT("ShadingModel"),           423, 0   },
		{ TEXT("Displacement"),           503, 0   },
	};

	FString GetSourceBreakOutputName(const int32 OutputIndex) {
		if (OutputIndex < 0 || GReflectionRuntime.MajorVersion <= 0 || GReflectionRuntime.MinorVersion < 0) {
			return FString();
		}

		const int32 SourceVersion = ToVersionKey(GReflectionRuntime.MajorVersion, GReflectionRuntime.MinorVersion);

		int32 Index = 0;

		for (const FBreakAttributeSlot& Slot : BreakAttributeSlots) {
			if (!Slot.IsPresentIn(SourceVersion)) {
				continue;
			}

			if (Index == OutputIndex) {
				return Slot.Name;
			}

			Index++;
		}

		return FString();
	}

	int32 FindBreakOutputIndex(const UMaterialExpression* Break, const FString& Name) {
		for (int32 Index = 0; Index < Break->Outputs.Num(); Index++) {
			if (OutputNameToString(Break->Outputs[Index].OutputName) == Name) {
				return Index;
			}
		}

		return INDEX_NONE;
	}

	void RemapBreakMaterialAttributesOutput(FExpressionInput* Input) {
		if (Input == nullptr || Input->Expression == nullptr) {
			return;
		}

		if (Input->Expression->GetClass()->GetName() != TEXT("MaterialExpressionBreakMaterialAttributes")) {
			return;
		}

		const FString AttributeName = GetSourceBreakOutputName(Input->OutputIndex);

		if (AttributeName.IsEmpty()) {
			return;
		}

		const int32 TargetIndex = FindBreakOutputIndex(Input->Expression, AttributeName);

		if (TargetIndex == INDEX_NONE) {
			UE_LOG(LogReflectionPropertySerializer, Warning, TEXT("Disconnected a material attribute input: this engine has no \"%s\""), *AttributeName);

			Input->Expression = nullptr;
			Input->OutputIndex = 0;

			return;
		}

		Input->OutputIndex = TargetIndex;
	}
}

static void SetObjectPropertyValueChecked(const FObjectPropertyBase* ObjectProperty, void* OutValue, UObject* Object) {
	if (Object == nullptr) {
		const UObject* Current = ObjectProperty->GetObjectPropertyValue(OutValue);

		if (Current != nullptr && Current->GetOuter() != nullptr && !Current->GetOuter()->IsA<UPackage>()) {
			return;
		}
	}

	if (Object != nullptr && ObjectProperty->PropertyClass != nullptr && !Object->IsA(ObjectProperty->PropertyClass)) {
		UE_LOG(LogReflectionPropertySerializer, Warning, TEXT("Skipped '%s': '%s' is a %s, not a %s."),
			*ObjectProperty->GetName(), *Object->GetPathName(), *Object->GetClass()->GetName(), *ObjectProperty->PropertyClass->GetName());

		return;
	}

	ObjectProperty->SetObjectPropertyValue(OutValue, Object);
}

void UPropertySerializer::RemapConvertOutput(const UStruct* Struct, void* StructValue) const {
	const FObjectProperty* ExpressionProperty = FindFProperty<FObjectProperty>(Struct, TEXT("Expression"));
	const FIntProperty* OutputIndexProperty = FindFProperty<FIntProperty>(Struct, TEXT("OutputIndex"));

	if (ExpressionProperty == nullptr || OutputIndexProperty == nullptr) {
		return;
	}

	void* ExpressionValue = ExpressionProperty->ContainerPtrToValuePtr<void>(StructValue);

	const TArray<UObject*>* Roots = ConvertOutputRoots.Find(ExpressionProperty->GetObjectPropertyValue(ExpressionValue));
	if (Roots == nullptr) {
		return;
	}

	int32* OutputIndexValue = OutputIndexProperty->ContainerPtrToValuePtr<int32>(StructValue);
	if (!Roots->IsValidIndex(*OutputIndexValue)) {
		return;
	}

	ExpressionProperty->SetObjectPropertyValue(ExpressionValue, (*Roots)[*OutputIndexValue]);
	*OutputIndexValue = 0;
}

#if UE5_2_BEYOND
UE_DISABLE_OPTIMIZATION
#else
PRAGMA_DISABLE_OPTIMIZATION
#endif

UPropertySerializer::UPropertySerializer() {
	FallbackStructSerializer = MakeShared<FFallbackStructSerializer>(this);

	UScriptStruct* DateTimeStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.DateTime"));
	UScriptStruct* TimespanStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.TimeSpan"));
	check(DateTimeStruct);
	check(TimespanStruct);

	StructSerializers.Add(DateTimeStruct, MakeShared<FDateTimeSerializer>());
	StructSerializers.Add(TimespanStruct, MakeShared<FTimeSpanSerializer>());

	if (UScriptStruct* NiagaraVariableStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Niagara.NiagaraVariableBase"))) {
		StructSerializers.Add(NiagaraVariableStruct, MakeShared<FNiagaraVariableSerializer>(this));
	}

	if (UScriptStruct* ConnectivityCubeStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/FortniteGame.ConnectivityCube"))) {
		StructSerializers.Add(ConnectivityCubeStruct, MakeShared<FConnectivityCubeSerializer>());
	}
}

void UPropertySerializer::DeserializePropertyValue(FProperty* Property, const TSharedRef<FJsonValue>& JsonValue, void* OutValue, UObject* OptionalOuter) {
	const FMapProperty* MapProperty = CastField<const FMapProperty>(Property);
	const FSetProperty* SetProperty = CastField<const FSetProperty>(Property);
	const FArrayProperty* ArrayProperty = CastField<const FArrayProperty>(Property);

	TSharedRef<FJsonValue> NewJsonValue = JsonValue;

	if (BlacklistedPropertyNames.Contains(Property->GetName())) {
		return;
	}

	if (MapProperty) {
		if (NewJsonValue->IsNull()) {
			return;
		}

		FProperty* KeyProperty = MapProperty->KeyProp;
		FProperty* ValueProperty = MapProperty->ValueProp;
		FScriptMapHelper MapHelper(MapProperty, OutValue);

		if (NewJsonValue->Type == EJson::Object) {
			for (const auto& Pair : NewJsonValue->AsObject()->Values) {
				const int32 Index = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
				uint8* PairPtr = MapHelper.GetPairPtr(Index);

				const TSharedRef<FJsonValue> EntryKey = MakeShared<FJsonValueString>(JsonKeyToString(Pair.Key));

				DeserializePropertyValue(KeyProperty, EntryKey, PairPtr);
				DeserializePropertyValue(ValueProperty, Pair.Value.ToSharedRef(), PairPtr + MapHelper.MapLayout.ValueOffset, OptionalOuter);
			}

			MapHelper.Rehash();

			return;
		}

		const TArray<TSharedPtr<FJsonValue>>& PairArray = NewJsonValue->AsArray();

		for (int32 i = 0; i < PairArray.Num(); i++) {
			const TSharedPtr<FJsonObject>& Pair = PairArray[i]->AsObject();
			const TSharedPtr<FJsonValue>& EntryKey = Pair->Values.FindChecked(TEXT("Key"));
			const TSharedPtr<FJsonValue>& EntryValue = Pair->Values.FindChecked(TEXT("Value"));
			const int32 Index = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
			uint8* PairPtr = MapHelper.GetPairPtr(Index);

			DeserializePropertyValue(KeyProperty, EntryKey.ToSharedRef(), PairPtr);
			DeserializePropertyValue(ValueProperty, EntryValue.ToSharedRef(), PairPtr + MapHelper.MapLayout.ValueOffset);
		}
		MapHelper.Rehash();

	} else if (SetProperty) {
		FProperty* ElementProperty = SetProperty->ElementProp;
		FScriptSetHelper SetHelper(SetProperty, OutValue);
		const TArray<TSharedPtr<FJsonValue>>& SetArray = NewJsonValue->AsArray();
		SetHelper.EmptyElements();
		uint8* TempElementStorage = static_cast<uint8*>(FMemory::Malloc(GetElementSize(ElementProperty)));
		ElementProperty->InitializeValue(TempElementStorage);

		for (int32 i = 0; i < SetArray.Num(); i++) {
			const TSharedPtr<FJsonValue>& Element = SetArray[i];
			DeserializePropertyValue(ElementProperty, Element.ToSharedRef(), TempElementStorage);

			const int32 NewElementIndex = SetHelper.AddDefaultValue_Invalid_NeedsRehash();
			uint8* NewElementPtr = SetHelper.GetElementPtr(NewElementIndex);

			ElementProperty->CopyCompleteValue_InContainer(NewElementPtr, TempElementStorage);
		}
		SetHelper.Rehash();

		ElementProperty->DestroyValue(TempElementStorage);
		FMemory::Free(TempElementStorage);
	} else if (ArrayProperty) {
		FProperty* ElementProperty = ArrayProperty->Inner;
		FScriptArrayHelper ArrayHelper(ArrayProperty, OutValue);

		if (TArray<uint8> Bytes; NewJsonValue->Type == EJson::String && ElementProperty->IsA<FByteProperty>()
			&& FBase64::Decode(NewJsonValue->AsString(), Bytes)) {
			ArrayHelper.EmptyAndAddUninitializedValues(Bytes.Num());

			if (Bytes.Num() > 0) {
				FMemory::Memcpy(ArrayHelper.GetRawPtr(0), Bytes.GetData(), Bytes.Num());
			}
		} else {
			const TArray<TSharedPtr<FJsonValue>>& SetArray = NewJsonValue->AsArray();

			uint8* TempElementStorage = SetArray.Num() > 0
				? static_cast<uint8*>(FMemory::Malloc(static_cast<SIZE_T>(GetElementSize(ElementProperty)) * SetArray.Num(),
					ElementProperty->GetMinAlignment()))
				: nullptr;

			for (int32 i = 0; i < SetArray.Num(); i++) {
				uint8* ElementPtr = TempElementStorage + static_cast<SIZE_T>(GetElementSize(ElementProperty)) * i;

				ElementProperty->InitializeValue(ElementPtr);
				DeserializePropertyValue(ElementProperty, SetArray[i].ToSharedRef(), ElementPtr, OptionalOuter);
			}

			ArrayHelper.EmptyValues();

			for (int32 i = 0; i < SetArray.Num(); i++) {
				uint8* ElementPtr = TempElementStorage + static_cast<SIZE_T>(GetElementSize(ElementProperty)) * i;

				ElementProperty->CopyCompleteValue(ArrayHelper.GetRawPtr(ArrayHelper.AddValue()), ElementPtr);
				ElementProperty->DestroyValue(ElementPtr);
			}

			if (TempElementStorage != nullptr) FMemory::Free(TempElementStorage);
		}
	}
	else if (Property->IsA<FMulticastDelegateProperty>()) {
	} else if (Property->IsA<FDelegateProperty>()) {
	} else if (CastField<const FInterfaceProperty>(Property)) {
	}
	else if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property)) {
		TSharedPtr<FJsonObject> SoftJsonObjectProperty;
		FString PathString = "";

		FString Kind;

		switch (NewJsonValue->Type) {
			case EJson::Object:
				SoftJsonObjectProperty = NewJsonValue->AsObject();

				if (!SoftJsonObjectProperty->TryGetStringField(TEXT("AssetPathName"), PathString)) {
					FString Named;
					FString Where;

					SoftJsonObjectProperty->TryGetStringField(TEXT("ObjectName"), Named);
					SoftJsonObjectProperty->TryGetStringField(TEXT("ObjectPath"), Where);

					if (Named.Split(TEXT("'"), &Kind, &Named)) {
						Named.Split(TEXT("'"), &Named, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
					}

					Named = Named.Replace(TEXT(":"), TEXT("."));

					Where.Split(TEXT("."), &Where, nullptr);

					if (!Named.IsEmpty() && !Where.IsEmpty()) {
						PathString = ToEditorPackagePath(Where) + TEXT(".") + Named;
					}
				}
			break;

			default:
				PathString = NewJsonValue->AsString();
			break;
		}

		if (PathString != "") {
			const FSoftObjectPath Reference(PathString);

			FSoftObjectPtr* ObjectPtr = static_cast<FSoftObjectPtr*>(OutValue);
			*ObjectPtr = Reference;

			if (!AlreadyHere(Reference)) {
				FString PackagePath;
				FString AssetName;
				PathString.Split(".", &PackagePath, &AssetName);
				TObjectPtr<UObject> T = nullptr;

				FString PropertyClassName = Kind.IsEmpty() ? SoftObjectProperty->PropertyClass->GetName() : Kind;

				IImporter::DownloadWrapper(T, PropertyClassName, AssetName, PackagePath);
			}
		}
	}
	else if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property)) {
		TObjectPtr<UObject> Object = nullptr;

		if (NewJsonValue->IsNull()) {
			SetObjectPropertyValueChecked(ObjectProperty, OutValue, nullptr);
		}

		if (NewJsonValue->Type == EJson::String) {
			FString Named = NewJsonValue->AsString();

			FString Kind;

			if (Named.Split(TEXT("'"), &Kind, &Named)) {
				Named.Split(TEXT("'"), &Named, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			}

			if (Named.IsEmpty() || Named == TEXT("None")) return;

			const FString Where = ToEditorPackagePath(Named);

			TObjectPtr<UObject> Points = LoadObjectByPath<UObject>(Where);

			if (Points == nullptr && ExportsContainer != nullptr && Named.Contains(TEXT(":"))) {
				FString Leaf;

				if (!Named.Split(TEXT(":"), nullptr, &Leaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) {
					Leaf = Named;
				}

				if (FString Deeper; Leaf.Split(TEXT("."), nullptr, &Deeper, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) {
					Leaf = Deeper;
				}

				if (FUObjectExport* Held = ExportsContainer->Find(Leaf); Held != nullptr && Held->IsJsonValid()) {
					if (Held->Object == nullptr && ObjectSerializer != nullptr) {
						ObjectSerializer->SpawnExport(Held);
					}

					Points = Held->Object;
				}
			}

			if (Points == nullptr && !Named.StartsWith(TEXT("/Script/"))) {
				FString PackagePath;
				FString AssetName;

				if (Named.Split(TEXT("."), &PackagePath, &AssetName)) {
					IImporter::DownloadWrapper(Points, Kind.IsEmpty() ? ObjectProperty->PropertyClass->GetName() : Kind, AssetName, PackagePath);
				}

				if (Points == nullptr) Points = LoadObjectByPath<UObject>(Where);
			}

			if (Points == nullptr) {
				UE_LOG(LogReflectionPropertySerializer, Warning, TEXT("\"%s\" names %s, and nothing of that came back"),
					*GetPropertyName(Property).ToString(), *Named);

				return;
			}

			SetObjectPropertyValueChecked(ObjectProperty, OutValue, Points);

			return;
		}

		if (NewJsonValue->Type == EJson::Object) {
			auto JsonValueAsObject = NewJsonValue->AsObject();

			UObject* const Into = OptionalOuter != nullptr
				? OptionalOuter
				: (ObjectSerializer != nullptr ? ObjectSerializer->Parent : nullptr);

			const TWeakObjectPtr<UObject> Filling(Into);

			FString Reference;

			if (ExportsContainer != nullptr && JsonValueAsObject->TryGetStringField(TEXT("ObjectName"), Reference)) {
				FUObjectExport* Held = ExportsContainer->FindByFullObjectName(Reference);

				if (Held->IsJsonValid() && Held->Object == nullptr && ObjectSerializer != nullptr) {
					ObjectSerializer->SpawnExport(Held);
				}

				if (Held->IsJsonValid() && Held->Object != nullptr) {
					Object = Held->Object;
				}
			}

			if (Importer == nullptr) {
				Importer = new IImporter();
			}

			Importer->SetParent(ObjectSerializer->Parent);

			if (!Object) Importer->LoadExport(&JsonValueAsObject, Object);

			if (Into != nullptr && !Filling.IsValid()) {
				UE_LOG(LogReflectionPropertySerializer, Warning, TEXT("\"%s\" was being read into something an import took apart, and was left"),
					*GetPropertyName(Property).ToString());

				return;
			}

			UObject* CurrentObject = ObjectProperty->GetObjectPropertyValue(OutValue);

			if (CurrentObject != nullptr && (!CurrentObject->IsValidLowLevelFast() || !IsValid(CurrentObject))) {
				CurrentObject = nullptr;
			}

			if (CurrentObject != nullptr) {
				if (!Object && CurrentObject->IsA(UActorComponent::StaticClass())) {
					Object = CurrentObject;
				}
			}

			if (!Object && ObjectSerializer->bUseExperimentalSpawning) {
				if (FUObjectExport* TargetExport = ExportsContainer->GetExportByObjectPath(JsonValueAsObject)) {
					FUObjectJsonValueExport Properties = TargetExport->GetObject(TEXT("Properties"));

					if (TargetExport->Has(TEXT("LODData"))) {
						Properties.SetArray(TEXT("LODData"), TargetExport->GetArray(TEXT("LODData")));
					}

					ObjectSerializer->SpawnExport(TargetExport);

					if (TargetExport->Object) {
						Object = TargetExport->Object;
					}
				}
			}

			if (Object != nullptr) {
				const bool bIsActorComponent = Object.Get()->IsA(UActorComponent::StaticClass());

				if (!bIsActorComponent || GetPropertyName(ObjectProperty) == "AttachParent" || GetPropertyName(ObjectProperty) == "ComponentTemplate") {
					SetObjectPropertyValueChecked(ObjectProperty, OutValue, Object);
				}

				if (bIsActorComponent && GetPropertyName(ObjectProperty) != "AttachParent" && !GBeingFilled.Contains(Object)) {
					const FFillScope BeingFilled(Object);

					if (FUObjectExport* TargetExport = ExportsContainer->GetExportByObjectPath(JsonValueAsObject)) {
						FUObjectJsonValueExport Properties = TargetExport->GetObject(TEXT("Properties"));

						if (TargetExport->Has(TEXT("LODData"))) {
							Properties.SetArray(TEXT("LODData"), TargetExport->GetArray(TEXT("LODData")));
						}

						ObjectSerializer->DeserializeObjectProperties(Properties.JsonObject, Object);
					}
				}

				if (bIsActorComponent) {
					if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Object.Get())) {
						StaticMeshComponent->PostEditImport();
					}
				}
			}

			if (Object != nullptr) return;

			FString ObjectName = JsonValueAsObject->GetStringField(TEXT("ObjectName"));
			FString ObjectPath = JsonValueAsObject->GetStringField(TEXT("ObjectPath"));
			FString ObjectOuter;
			int ObjectIndex = -1;

			if (ObjectName.Contains(".")) {
				ObjectName.Split(".", &ObjectOuter, &ObjectName);
				ObjectName.Split("'", &ObjectName, nullptr);
			}

			if (ObjectName.Contains(":")) {
				ObjectName.Split(":", nullptr, &ObjectName);
				ObjectName.Split("'", &ObjectName, nullptr);
			}

			if (ObjectPath.Contains(".")) {
				FString ObjectIndexString;
				ObjectPath.Split(".", nullptr, &ObjectIndexString);

				ObjectIndex = FCString::Atoi(*ObjectIndexString);
			}

			if (ExportsContainer) {
				if (FUObjectExport* Export = ExportsContainer->Find(ObjectName); Export->IsJsonValid() && Export->Object != nullptr) {
					if (UObject* FoundObject = Export->Object) {
						SetObjectPropertyValueChecked(ObjectProperty, OutValue, FoundObject);
					}
				}
			}

			if (ObjectName.Contains(".")) {
				TArray<FString> Parts;
				ObjectName.ParseIntoArray(Parts, TEXT("."), true);

				FString Penultimate = Parts.Num() > 1 ? Parts[Parts.Num() - 2] : TEXT("");
				FString LastSegment = Parts.Num() > 0 ? Parts.Last() : TEXT("");

				ObjectName = LastSegment;
				ObjectOuter = Penultimate;
			}

			if (!ObjectOuter.IsEmpty()) {
				if (ObjectOuter.Contains(":")) {
					ObjectOuter.Split(":", nullptr, &ObjectOuter);
				}

				if (FUObjectExport* Export = ExportsContainer->Find(ObjectName, ObjectOuter); Export->IsJsonValid() && Export->Object != nullptr) {
					if (UObject* FoundObject = Export->Object) {
						SetObjectPropertyValueChecked(ObjectProperty, OutValue, FoundObject);
					}
				}
			}

			if (FallbackToParentTrace) {
				if (UObject* Parent = ObjectSerializer->Parent) {
					FString Name = Parent->GetName();

					if (FUObjectExport* Export = ExportsContainer->Find(ObjectName, Name); Export->IsJsonValid() && Export->Object != nullptr) {
						if (UObject* FoundObject = Export->Object) {
							SetObjectPropertyValueChecked(ObjectProperty, OutValue, FoundObject);
						}
					}
				}
			}

			if (ObjectIndex != -1 && ExportsContainer) {
				if (FUObjectExport* Export = ExportsContainer->FindByPositionAndName(ObjectIndex, ObjectName); Export->IsJsonValid() && Export->Object != nullptr) {
					if (UObject* FoundObject = Export->Object) {
						SetObjectPropertyValueChecked(ObjectProperty, OutValue, FoundObject);
					}
				}
			}

			if (ObjectProperty->GetObjectPropertyValue(OutValue) == nullptr) {
				FString Named;

				JsonValueAsObject->TryGetStringField(TEXT("ObjectName"), Named);

				UE_LOG(LogReflectionPropertySerializer, Warning, TEXT("\"%s\" names %s, and nothing of that came back"),
					*GetPropertyName(Property).ToString(), Named.IsEmpty() ? TEXT("something it does not name") : *Named);
			}

#if 0
			FUObjectExport NewExport(JsonValueAsObject);

			const TArray<FName> TreeSegments = NewExport.GetOuterTreeSegments();

			if (TreeSegments.Num() > 0) {
				if (FUObjectExport& FoundExport = ExportsContainer->FindByTreeSegment(TreeSegments); FoundExport.Object != nullptr) {
					SetObjectPropertyValueChecked(ObjectProperty, OutValue, FoundExport.Object);
				}
			}
#endif
		}
	}
	else if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property)) {
		if (StructProperty->Struct == TBaseStructure<FGuid>::Get() && NewJsonValue->Type == EJson::String) {
			FGuid::Parse(NewJsonValue->AsString(), *static_cast<FGuid*>(OutValue));

			return;
		}

		if (StructProperty->Struct == FGameplayTag::StaticStruct()) {
			FGameplayTag* GameplayTagStr = static_cast<FGameplayTag*>(OutValue);
			FGameplayTag NewTag = RequestOrMakeGameplayTag(NewJsonValue->AsObject()->GetStringField(TEXT("TagName")));
			*GameplayTagStr = NewTag;
			return;
		}

		if (StructProperty->Struct == FGameplayTagContainer::StaticStruct()) {
			FGameplayTagContainer* GameplayTagContainerStr = static_cast<FGameplayTagContainer*>(OutValue);

			auto GameplayTags = JsonValue->AsArray();

			for (TSharedPtr GameplayTagValue : GameplayTags) {
				FString GameplayTagString = GameplayTagValue->AsString();
				const FGameplayTag GameplayTag = RequestOrMakeGameplayTag(GameplayTagString);

				if (GameplayTag.IsValid()) GameplayTagContainerStr->AddTag(GameplayTag);
			}

			return;
		}

		if (StructProperty->Struct == FMovieSceneFrameRange::StaticStruct()) {
			FMovieSceneFrameRange* MovieSceneFrameRange = static_cast<FMovieSceneFrameRange*>(OutValue);
			TSharedPtr<FJsonObject> JsonObject = NewJsonValue->AsObject()->GetObjectField(TEXT("Value"));

			FMovieSceneFrameRange Range;

			const auto ReadBound = [](const TSharedPtr<FJsonObject>& Bound) {
				int32 Kind = 0;

				if (const TSharedPtr<FJsonValue> Said = Bound->TryGetField(TEXT("Type")); Said.IsValid()) {
					if (Said->Type == EJson::String) {
						FString Spelled = Said->AsString();

						Spelled.Split(TEXT("::"), nullptr, &Spelled);

						Kind = Spelled == TEXT("Inclusive") ? 1 : Spelled == TEXT("Open") ? 2 : 0;
					} else {
						Kind = static_cast<int32>(Said->AsNumber());
					}
				}

				if (Kind == 2) return TRangeBound<FFrameNumber>::Open();

				FFrameNumber BoundFrame;

				if (const TSharedPtr<FJsonObject>* Held = nullptr; Bound->TryGetObjectField(TEXT("Value"), Held)) {
					(*Held)->TryGetNumberField(TEXT("Value"), BoundFrame.Value);
				}

				return Kind == 1 ? TRangeBound<FFrameNumber>::Inclusive(BoundFrame) : TRangeBound<FFrameNumber>::Exclusive(BoundFrame);
			};

			if (const TSharedPtr<FJsonObject>* Bound = nullptr; JsonObject->TryGetObjectField(TEXT("LowerBound"), Bound)) {
				Range.Value.SetLowerBound(ReadBound(*Bound));
			}

			if (const TSharedPtr<FJsonObject>* Bound = nullptr; JsonObject->TryGetObjectField(TEXT("UpperBound"), Bound)) {
				Range.Value.SetUpperBound(ReadBound(*Bound));
			}

			*MovieSceneFrameRange = Range;

			return;
		}

		if (StructProperty->Struct->GetFName() == "SoftObjectPath") {
			TSharedPtr<FJsonObject> SoftJsonObjectProperty;
			FString PathString = "";

			SoftJsonObjectProperty = NewJsonValue->AsObject();
			PathString = SoftJsonObjectProperty->GetStringField(TEXT("AssetPathName"));

			if (PathString != "") {
				const FSoftObjectPath Reference(PathString);

				FSoftObjectPtr* ObjectPtr = static_cast<FSoftObjectPtr*>(OutValue);
				*ObjectPtr = Reference;

				if (!AlreadyHere(Reference)) {
					FString PackagePath;
					FString AssetName;
					PathString.Split(".", &PackagePath, &AssetName);
					TObjectPtr<UObject> T = nullptr;

					FString PropertyClassName = "DataAsset";

					IImporter::DownloadWrapper(T, PropertyClassName, AssetName, PackagePath);
				}
			}
		}

		FString OutString;

		if (JsonValue->TryGetString(OutString)) {
			FGuid GUID = StringToGuid(OutString);

			TSharedRef<FJsonObject> SharedObject = MakeShareable(new FJsonObject());
			SharedObject->SetNumberField(TEXT("A"), GUID.A); SharedObject->SetNumberField(TEXT("B"), GUID.B);
			SharedObject->SetNumberField(TEXT("C"), GUID.C); SharedObject->SetNumberField(TEXT("D"), GUID.D);

			const TSharedRef<FJsonValue> NewValue = MakeShareable(new FJsonValueObject(SharedObject));
			NewJsonValue = NewValue;
		}

		if (StructProperty->Struct == FFontData::StaticStruct()) {
			FFontData* FontData = static_cast<FFontData*>(OutValue);
			TSharedPtr<FJsonObject> JsonObject = NewJsonValue->AsObject();

			if (JsonObject->HasField(TEXT("LocalFontFaceAsset"))) {
				TSharedPtr<FJsonObject> LocalFontFaceExport = JsonObject->GetObjectField(TEXT("LocalFontFaceAsset"));

				if (Importer == nullptr) {
					Importer = new IImporter();
				}

				TObjectPtr<UFontFace> FontFacePtr;

				Importer->SetParent(ObjectSerializer->Parent);
				Importer->LoadExport(&LocalFontFaceExport, FontFacePtr);

				if (UFontFace* FontFace = FontFacePtr.Get()) {
					*FontData = FFontData(FontFace, 0);
				}
			}
		}

		DeserializeStruct(StructProperty->Struct, NewJsonValue->AsObject().ToSharedRef(), OutValue, OptionalOuter);

		if (ConvertOutputRoots.Num() > 0) {
			RemapConvertOutput(StructProperty->Struct, OutValue);
		}

		if (Property->GetCPPType(nullptr, CPPF_None) == TEXT("FExpressionInput")) {
			RemapBreakMaterialAttributesOutput(static_cast<FExpressionInput*>(OutValue));
		}

		if (IsStructPropertyADistribution(StructProperty)) {
			if (FRawDistribution* RawDistribution = static_cast<FRawDistribution*>(OutValue)) {
				const bool IsFloat = IsFloatDistribution(StructProperty);

				const TSharedPtr<FJsonObject> Written = NewJsonValue->Type == EJson::Object ? NewJsonValue->AsObject() : nullptr;

				const TSharedPtr<FJsonValue> Named = Written.IsValid() ? Written->TryGetField(TEXT("Distribution")) : nullptr;

				const bool bPackageNamedOne = Named.IsValid() && !Named->IsNull();

				if (!bPackageNamedOne) {
					SetDistribution(RawDistribution, nullptr, IsFloat);
				}

				if (!GetDistribution(RawDistribution, IsFloat)) {
					if (UDistribution* NewDistribution = DecookDistribution(OptionalOuter, *RawDistribution, IsFloat)) {
						SetDistribution(RawDistribution, NewDistribution, IsFloat);
					}
				}
			}
		}
	}
	else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property)) {
		if (JsonValue->Type == EJson::String) {
			FString EnumAsString = JsonValue->AsString();

			if (!ByteProperty->Enum)
			{
				return;
			}

			int64 EnumerationValue = ByteProperty->Enum->GetValueByNameString(EnumAsString);

			if (EnumerationValue == -1) {
				UE_LOG(LogReflection, Warning, TEXT("Invalid enum value for property '%s'!"), *Property->GetName());

				UE_LOG(LogReflection, Warning, TEXT("Enum name: %s"), *EnumAsString);
				UE_LOG(LogReflection, Warning, TEXT("Enum type: %s"), *ByteProperty->Enum->GetName());
				UE_LOG(LogReflection, Warning, TEXT("Available values:"));

				for (int32 Index = 0; Index < ByteProperty->Enum->NumEnums() - 1; ++Index)
				{
					FString Name = ByteProperty->Enum->GetNameStringByIndex(Index);
					int64 Value = ByteProperty->Enum->GetValueByIndex(Index);
					UE_LOG(LogReflection, Warning, TEXT("  [%d] %s = %lld"), Index, *Name, Value);
				}

				EnumerationValue = 0;
			}

			ByteProperty->SetIntPropertyValue(OutValue, EnumerationValue);
		}
		else {
			const int64 NumberValue = static_cast<int64>(NewJsonValue->AsNumber());

			if (ByteProperty->Enum != nullptr && !ByteProperty->Enum->IsValidEnumValue(NumberValue)) {
				UE_LOG(LogReflectionPropertySerializer, Warning, TEXT("\"%s\" is a %s, which has no %lld, so it was left as it was built"), *GetPropertyName(ByteProperty).ToString(), *ByteProperty->Enum->GetName(), NumberValue);

				return;
			}

			ByteProperty->SetIntPropertyValue(OutValue, NumberValue);
		}
	}
	else if (const FNumericProperty* NumberProperty = CastField<const FNumericProperty>(Property)) {
		const double NumberValue = NewJsonValue->AsNumber();
		if (NumberProperty->IsFloatingPoint()) {
			NumberProperty->SetFloatingPointPropertyValue(OutValue, NumberValue);
		}

		else {
			NumberProperty->SetIntPropertyValue(OutValue, static_cast<int64>(NumberValue));
		}
	}
	else if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property)) {
		const bool BooleanValue = NewJsonValue->AsBool();
		BoolProperty->SetPropertyValue(OutValue, BooleanValue);
	}
	else if (Property->IsA<FStrProperty>()) {
		const FString StringValue = NewJsonValue->AsString();
		*static_cast<FString*>(OutValue) = StringValue;
	}
	else if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property)) {
		if (NewJsonValue->Type == EJson::Number) {
			const int64 NumberValue = static_cast<int64>(NewJsonValue->AsNumber());

			if (EnumProperty->GetEnum() != nullptr && !EnumProperty->GetEnum()->IsValidEnumValue(NumberValue)) {
				UE_LOG(LogReflectionPropertySerializer, Warning, TEXT("\"%s\" is a %s, which has no %lld, so it was left as it was built"), *GetPropertyName(EnumProperty).ToString(), *EnumProperty->GetEnum()->GetName(), NumberValue);

				return;
			}

			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(OutValue, NumberValue);

			return;
		}

		FString EnumAsString = NewJsonValue->AsString();

		if (EnumAsString.Contains("::")) {
			EnumAsString.Split("::", nullptr, &EnumAsString);
		}

		int64 EnumerationValue = EnumProperty->GetEnum()->GetValueByNameString(EnumAsString);

		if (EnumerationValue != INDEX_NONE) {
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(OutValue, EnumerationValue);
		}
	}
	else if (Property->IsA<FNameProperty>()) {
		const FString NameString = NewJsonValue->AsString();
		*static_cast<FName*>(OutValue) = *NameString;
	}
	else if (const FTextProperty* TextProperty = CastField<const FTextProperty>(Property)) {
		const FString SerializedValue = NewJsonValue->AsString();

		if (!SerializedValue.IsEmpty()) {
			FTextStringHelper::ReadFromBuffer(*SerializedValue, *static_cast<FText*>(OutValue));
		} else {
			TSharedPtr<FJsonObject> Object = NewJsonValue->AsObject().ToSharedRef();

			FString TextNamespace = Object->GetStringField(TEXT("Namespace"));
			FString UniqueKey = Object->GetStringField(TEXT("Key"));
			FString SourceString = Object->GetStringField(TEXT("SourceString"));
			FString CultureInvariantString = Object->GetStringField(TEXT("CultureInvariantString"));

			if (Object->HasField(TEXT("CultureInvariantString"))) {
				TextProperty->SetPropertyValue(OutValue, FText::FromString(*Object->GetStringField(TEXT("CultureInvariantString"))));
			} else {
	#if UE5_8_BEYOND
				TextProperty->SetPropertyValue(OutValue, FText::FromString(SourceString));
	#else
				TextProperty->SetPropertyValue(OutValue, FInternationalization::ForUseOnlyByLocMacroAndGraphNodeTextLiterals_CreateText(*SourceString, *TextNamespace, *UniqueKey));
	#endif
			}
		}
	}
#if !UE4_24_BELOW
	else if (CastField<const FFieldPathProperty>(Property)) {
		FFieldPath FieldPath;

		if (NewJsonValue->Type == EJson::String) {
			FieldPath.Generate(*NewJsonValue->AsString());
		} else if (NewJsonValue->Type == EJson::Object) {
			const TSharedPtr<FJsonObject> Said = NewJsonValue->AsObject();

			FString Name;
			FString Where;

			Said->TryGetStringField(TEXT("ObjectName"), Name);
			Said->TryGetStringField(TEXT("ObjectPath"), Where);

			if (Name.Contains(TEXT("'"))) {
				Name.Split(TEXT("'"), nullptr, &Name);
				Name.Split(TEXT("'"), &Name, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			}

			FString Holder;
			FString Named;

			if (!Name.Split(TEXT(":"), &Holder, &Named)) {
				Named = Name;
			}

			FString Package = Where;

			if (int32 Dot; Where.FindChar(TEXT('.'), Dot)) {
				Package = Where.Left(Dot);
			}

			UStruct* Holds = nullptr;

			if (!Holder.IsEmpty() && !Package.IsEmpty()) {
				Holds = FindObject<UStruct>(nullptr, *(Package + TEXT(".") + Holder));

				if (Holds == nullptr) {
					if (Importer == nullptr) {
						Importer = new IImporter();
					}

					Importer->SetParent(ObjectSerializer->Parent);

					TSharedPtr<FJsonObject> Points = MakeShared<FJsonObject>();

					Points->SetStringField(TEXT("ObjectName"), FString::Printf(TEXT("Class'%s'"), *Holder));
					Points->SetStringField(TEXT("ObjectPath"), Where);

					TObjectPtr<UObject> Found = nullptr;

					Importer->LoadExport(&Points, Found);

					Holds = Cast<UStruct>(Found.Get());
				}
			}

			if (FProperty* Points = Holds != nullptr && !Named.IsEmpty() ? FindFProperty<FProperty>(Holds, *Named) : nullptr) {
				FieldPath = FFieldPath(Points);
			} else if (!Named.IsEmpty()) {
				UE_LOG(LogReflectionPropertySerializer, Warning, TEXT("\"%s\" points at \"%s\" of \"%s\", and %s"),
					*GetPropertyName(Property).ToString(), *Named, Holder.IsEmpty() ? TEXT("nothing") : *Holder,
					Holds != nullptr ? TEXT("what holds it has nothing of that name") : TEXT("this build hasn't got what holds it"));
			}
		}

		*static_cast<FFieldPath*>(OutValue) = FieldPath;
	}
#endif
	else {
		UE_LOG(LogReflectionPropertySerializer, Fatal, TEXT("Found unsupported property type when deserializing value: %s"), *Property->GetClass()->GetName());
	}
}

void UPropertySerializer::DisablePropertySerialization(const UStruct* Struct, const FName PropertyName) {
	FProperty* Property = Struct->FindPropertyByName(PropertyName);
	checkf(Property, TEXT("Cannot find Property %s in Struct %s"), *PropertyName.ToString(), *Struct->GetPathName());
	BlacklistedProperties.Add(Property);
}

void UPropertySerializer::AddStructSerializer(UScriptStruct* Struct, const TSharedPtr<FStructSerializer>& Serializer) {
	StructSerializers.Add(Struct, Serializer);
}

bool UPropertySerializer::ShouldDeserializeProperty(FProperty* Property) const {
	if (Property->HasAnyPropertyFlags(CPF_Deprecated)) {
		return false;
	}

	if (this != nullptr && this && BlacklistedProperties.IsValidIndex(0) && BlacklistedProperties.Contains(Property)) {
		return false;
	}

	return true;
}

void UPropertySerializer::DeserializeStruct(UScriptStruct* Struct, const TSharedRef<FJsonObject>& Properties, void* OutValue, UObject* OptionalOuter) const {
	FStructSerializer* StructSerializer = GetStructSerializer(Struct);
	StructSerializer->Deserialize(Struct, OutValue, Properties, OptionalOuter);
}

FStructSerializer* UPropertySerializer::GetStructSerializer(const UScriptStruct* Struct) const {
	check(Struct);

	for (const UStruct* Walk = Struct; Walk != nullptr; Walk = Walk->GetSuperStruct()) {
		const UScriptStruct* AsScriptStruct = Cast<UScriptStruct>(Walk);

		if (AsScriptStruct == nullptr) continue;

		if (TSharedPtr<FStructSerializer> const* StructSerializer = StructSerializers.Find(const_cast<UScriptStruct*>(AsScriptStruct))) {
			if (ensure(StructSerializer->IsValid())) {
				return StructSerializer->Get();
			}
		}
	}

	return FallbackStructSerializer.Get();
}

#if UE5_2_BEYOND
UE_ENABLE_OPTIMIZATION
#else
PRAGMA_ENABLE_OPTIMIZATION
#endif
