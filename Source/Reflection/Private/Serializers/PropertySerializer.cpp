
/* Copyright Reflection Contributors 2024-2026 */

#include "Serializers/PropertySerializer.h"

#include "GameplayTagContainer.h"
#include "Importers/Constructor/Importer.h"
#include "Serializers/ObjectSerializer.h"
#include "UObject/TextProperty.h"

/* Struct Serializers */
#include "Distributions.h"
#include "MovieSceneSection.h"
#include "Engine/FontFace.h"
#include "Importers/Types/Cascade/ParticleSystemDecooking.h"
#include "Serializers/Structs/DateTimeSerializer.h"
#include "Serializers/Structs/FallbackStructSerializer.h"
#include "Serializers/Structs/TimeSpanSerializer.h"

#include "Settings/Runtime.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionPropertySerializer, Error, Log);

/* Material Attributes ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

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
	if (Object != nullptr && ObjectProperty->PropertyClass != nullptr && !Object->IsA(ObjectProperty->PropertyClass)) {
		UE_LOG(LogReflectionPropertySerializer, Warning, TEXT("Skipped '%s': '%s' is a %s, not a %s."),
			*ObjectProperty->GetName(), *Object->GetPathName(), *Object->GetClass()->GetName(), *ObjectProperty->PropertyClass->GetName());

		return;
	}

	ObjectProperty->SetObjectPropertyValue(OutValue, Object);
}

void UPropertySerializer::RemapConvertOutput(const UStruct* Struct, void* StructValue) const {
	FObjectProperty* ExpressionProperty = FindFProperty<FObjectProperty>(Struct, TEXT("Expression"));
	FIntProperty* OutputIndexProperty = FindFProperty<FIntProperty>(Struct, TEXT("OutputIndex"));

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
		const TArray<TSharedPtr<FJsonValue>>& SetArray = NewJsonValue->AsArray();
		ArrayHelper.EmptyValues();

		for (int32 i = 0; i < SetArray.Num(); i++) {
			const TSharedPtr<FJsonValue>& Element = SetArray[i];
			const uint32 AddedIndex = ArrayHelper.AddValue();
			uint8* ValuePtr = ArrayHelper.GetRawPtr(AddedIndex);
			DeserializePropertyValue(ElementProperty, Element.ToSharedRef(), ValuePtr, OptionalOuter);
		}
	}
	else if (Property->IsA<FMulticastDelegateProperty>()) {
	} else if (Property->IsA<FDelegateProperty>()) {
	} else if (CastField<const FInterfaceProperty>(Property)) {
	}
	else if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property)) {
		TSharedPtr<FJsonObject> SoftJsonObjectProperty;
		FString PathString = "";

		switch (NewJsonValue->Type) {
			case EJson::Object:
				SoftJsonObjectProperty = NewJsonValue->AsObject();
				PathString = SoftJsonObjectProperty->GetStringField(TEXT("AssetPathName"));
			break;

			default:
				PathString = NewJsonValue->AsString();
			break;
		}

		if (PathString != "") {
			FSoftObjectPtr* ObjectPtr = static_cast<FSoftObjectPtr*>(OutValue);
			*ObjectPtr = FSoftObjectPath(PathString);

			if (!ObjectPtr->LoadSynchronous()) {
				FString PackagePath;
				FString AssetName;
				PathString.Split(".", &PackagePath, &AssetName);
				TObjectPtr<UObject> T = nullptr;

				FString PropertyClassName = SoftObjectProperty->PropertyClass->GetName();

				IImporter::DownloadWrapper(T, PropertyClassName, AssetName, PackagePath);
			}
		}
	}
	else if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property)) {
		TObjectPtr<UObject> Object = nullptr;

		if (NewJsonValue->IsNull()) {
			SetObjectPropertyValueChecked(ObjectProperty, OutValue, nullptr);
		}

		if (NewJsonValue->Type == EJson::Object) {
			auto JsonValueAsObject = NewJsonValue->AsObject();

			if (Importer == nullptr) {
				Importer = new IImporter();
			}

			Importer->SetParent(ObjectSerializer->Parent);
			Importer->LoadExport(&JsonValueAsObject, Object);

			if (UObject* CurrentObject = ObjectProperty->GetObjectPropertyValue(OutValue)) {
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
				bool bIsActorComponent = Object.Get()->IsA(UActorComponent::StaticClass());

				if (!bIsActorComponent) {
					SetObjectPropertyValueChecked(ObjectProperty, OutValue, Object);
				} else {
					if (FUObjectExport* TargetExport = ExportsContainer->GetExportByObjectPath(JsonValueAsObject)) {
						FUObjectJsonValueExport Properties = TargetExport->GetObject(TEXT("Properties"));

						if (TargetExport->Has(TEXT("LODData"))) {
							Properties.SetArray(TEXT("LODData"), TargetExport->GetArray(TEXT("LODData")));
						}

						if (GetPropertyName(ObjectProperty) != "AttachParent") {
							ObjectSerializer->DeserializeObjectProperties(Properties.JsonObject, Object);
						} else {
							SetObjectPropertyValueChecked(ObjectProperty, OutValue, Object);
						}
					}

					if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Object.Get())) {
						StaticMeshComponent->PostEditImport();
					}
				}
			}

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
				FUObjectExport* Export = ExportsContainer->Find(ObjectName);
				if (Export != nullptr && Export->IsJsonValid() && Export->Object != nullptr) {
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

				FUObjectExport* Export = ExportsContainer->Find(ObjectName, ObjectOuter);
				if (Export != nullptr && Export->IsJsonValid() && Export->Object != nullptr) {
					if (UObject* FoundObject = Export->Object) {
						SetObjectPropertyValueChecked(ObjectProperty, OutValue, FoundObject);
					}
				}
			}

			if (FallbackToParentTrace) {
				if (UObject* Parent = ObjectSerializer->Parent) {
					FString Name = Parent->GetName();

					FUObjectExport* Export = ExportsContainer->Find(ObjectName, Name);
					if (Export != nullptr && Export->IsJsonValid() && Export->Object != nullptr) {
						if (UObject* FoundObject = Export->Object) {
							SetObjectPropertyValueChecked(ObjectProperty, OutValue, FoundObject);
						}
					}
				}
			}

			if (ObjectIndex != -1 && ExportsContainer) {
				FUObjectExport* Export = ExportsContainer->FindByPositionAndName(ObjectIndex, ObjectName);
				if (Export != nullptr && Export->IsJsonValid() && Export->Object != nullptr) {
					if (UObject* FoundObject = Export->Object) {
						SetObjectPropertyValueChecked(ObjectProperty, OutValue, FoundObject);
					}
				}
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
		if (StructProperty->Struct == FGameplayTag::StaticStruct()) {
			FGameplayTag* GameplayTagStr = static_cast<FGameplayTag*>(OutValue);
			FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(FName(*NewJsonValue->AsObject()->GetStringField(TEXT("TagName"))), false);
			*GameplayTagStr = NewTag;
			return;
		}

		if (StructProperty->Struct == FGameplayTagContainer::StaticStruct()) {
			FGameplayTagContainer* GameplayTagContainerStr = static_cast<FGameplayTagContainer*>(OutValue);

			auto GameplayTags = JsonValue->AsArray();

			for (const TSharedPtr<FJsonValue>& GameplayTagValue : GameplayTags) {
				FString GameplayTagString = GameplayTagValue->AsString();
				FGameplayTag GameplayTag = FGameplayTag::RequestGameplayTag(FName(*GameplayTagString));

				GameplayTagContainerStr->AddTag(GameplayTag);
			}

			return;
		}

		if (StructProperty->Struct == FMovieSceneFrameRange::StaticStruct()) {
			FMovieSceneFrameRange* MovieSceneFrameRange = static_cast<FMovieSceneFrameRange*>(OutValue);
			TSharedPtr<FJsonObject> JsonObject = NewJsonValue->AsObject()->GetObjectField(TEXT("Value"));

			FMovieSceneFrameRange Range;

			if (JsonObject->HasField(TEXT("LowerBound"))) {
				TSharedPtr<FJsonObject> Bound = JsonObject->GetObjectField(TEXT("LowerBound"));
				TSharedPtr<FJsonObject> BoundValue = Bound->GetObjectField(TEXT("Value"));

				int32 Type = Bound->GetIntegerField(TEXT("Type"));
				int32 Value = BoundValue->GetIntegerField(TEXT("Value"));

				FFrameNumber BoundFrame;
				BoundFrame.Value = Value;

				if (Type == 0) {
					Range.Value.SetLowerBound(TRangeBound<FFrameNumber>::Exclusive(BoundFrame));
				} else if (Type == 1) {
					Range.Value.SetLowerBound(TRangeBound<FFrameNumber>::Inclusive(BoundFrame));
				}
			}

			if (JsonObject->HasField(TEXT("UpperBound"))) {
				TSharedPtr<FJsonObject> Bound = JsonObject->GetObjectField(TEXT("UpperBound"));
				TSharedPtr<FJsonObject> BoundValue = Bound->GetObjectField(TEXT("Value"));

				int32 Type = Bound->GetIntegerField(TEXT("Type"));
				int32 Value = BoundValue->GetIntegerField(TEXT("Value"));

				FFrameNumber BoundFrame;
				BoundFrame.Value = Value;

				if (Type == 0) {
					Range.Value.SetUpperBound(TRangeBound<FFrameNumber>::Exclusive(BoundFrame));
				} else if (Type == 1) {
					Range.Value.SetUpperBound(TRangeBound<FFrameNumber>::Inclusive(BoundFrame));
				}
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
				FSoftObjectPtr* ObjectPtr = static_cast<FSoftObjectPtr*>(OutValue);
				*ObjectPtr = FSoftObjectPath(PathString);

				if (!ObjectPtr->LoadSynchronous()) {
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
			SharedObject->SetNumberField(TEXT("A"), GUID.A);
			SharedObject->SetNumberField(TEXT("B"), GUID.B);
			SharedObject->SetNumberField(TEXT("C"), GUID.C);
			SharedObject->SetNumberField(TEXT("D"), GUID.D);

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

			check(ByteProperty->Enum);
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
		FieldPath.Generate(*NewJsonValue->AsString());
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
	TSharedPtr<FStructSerializer> const* StructSerializer = StructSerializers.Find(Struct);
	return StructSerializer && ensure(StructSerializer->IsValid()) ? StructSerializer->Get() : FallbackStructSerializer.Get();
}

#if UE5_2_BEYOND
UE_ENABLE_OPTIMIZATION
#else
PRAGMA_ENABLE_OPTIMIZATION
#endif
