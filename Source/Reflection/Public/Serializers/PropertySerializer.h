#pragma once

#include "Engine/SharedPtrCompatibility.h"

#include "ObjectSerializer.h"
#include "Containers/ExportContainer.h"
#include "Dom/JsonObject.h"
#include "Structs/StructSerializer.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"
#include "Engine/Compatibility.h"
#include "PropertySerializer.generated.h"

class IImporter;

UCLASS()
class REFLECTION_API UPropertySerializer : public UObject
{
	GENERATED_BODY()
public:
	UPropertySerializer();

	void DisablePropertySerialization(const UStruct* Struct, FName PropertyName);
	void AddStructSerializer(UScriptStruct* Struct, const TSharedPtr<FStructSerializer>& Serializer);

	bool ShouldDeserializeProperty(FProperty* Property) const;

	void DeserializePropertyValue(FProperty* Property, const TSharedRef<FJsonValue>& Value, void* OutValue, UObject* OptionalOuter = nullptr);
	void DeserializeStruct(UScriptStruct* Struct, const TSharedRef<FJsonObject>& Value, void* OutValue, UObject* OptionalOuter = nullptr) const;

	void RemapConvertOutput(const UStruct* Struct, void* StructValue) const;
private:
	FStructSerializer* GetStructSerializer(const UScriptStruct* Struct) const;

public:
	friend class UObjectSerializer;

	UPROPERTY()
	UObjectSerializer* ObjectSerializer;

	IImporter* Importer;

	TArray<FProperty*> BlacklistedProperties;
	TSharedPtr<FStructSerializer> FallbackStructSerializer;
	TMap<UScriptStruct*, TSharedPtr<FStructSerializer>> StructSerializers;

	bool FallbackToParentTrace = true;

	FUObjectExportContainer* ExportsContainer = nullptr;
	TArray<FString> BlacklistedPropertyNames;

	TMap<UObject*, TArray<UObject*>> ConvertOutputRoots;
};

inline bool PassthroughPropertyHandler(FProperty* Property, const FString& PropertyName, void* PropertyValue, const TSharedPtr<FJsonObject>& Properties, UPropertySerializer* PropertySerializer) {
	if (Property->ArrayDim != 1) {
		TArray<TSharedPtr<FJsonValue>> ArrayElements;
		ArrayElements.SetNum(Property->ArrayDim);

		for (const auto& Pair : Properties->Values) {
			const FString Key = JsonKeyToString(Pair.Key);
			const TSharedPtr<FJsonValue> Value = Pair.Value;

			if (Key != PropertyName && !Key.StartsWith(PropertyName + TEXT("["))) continue;

			int32 CurrentArrayIndex = 0;

			if (Key.Contains("[") && Key.Contains("]")) {
				int32 OpenBracketPos, CloseBracketPos;

				if (Key.FindChar('[', OpenBracketPos) && Key.FindChar(']', CloseBracketPos) && CloseBracketPos > OpenBracketPos) {
					FString IndexStr = Key.Mid(OpenBracketPos + 1, CloseBracketPos - OpenBracketPos - 1);

					CurrentArrayIndex = FCString::Atoi(*IndexStr);
				}
			}

			if (!ArrayElements.IsValidIndex(CurrentArrayIndex)) {
				continue;
			}

			ArrayElements[CurrentArrayIndex] = Value;
		}

		for (int32 ArrayIndex = 0; ArrayIndex < ArrayElements.Num(); ArrayIndex++) {
			uint8* ArrayPropertyValue = static_cast<uint8*>(PropertyValue) + GetElementSize(Property) * ArrayIndex;

			if (!ArrayElements.IsValidIndex(ArrayIndex)) continue;

			TSharedPtr<FJsonValue> ArrayJsonElement = ArrayElements[ArrayIndex];

			if (ArrayJsonElement == nullptr || ArrayJsonElement->IsNull()) continue;

			const TSharedRef<FJsonValue> ArrayJsonValue = ArrayJsonElement.ToSharedRef();

			PropertySerializer->DeserializePropertyValue(Property, ArrayJsonValue, ArrayPropertyValue);
		}

		return true;
	}

	return false;
}
