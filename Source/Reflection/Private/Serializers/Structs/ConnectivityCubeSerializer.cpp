#include "Engine/SharedPtrCompatibility.h"
#include "Serializers/Structs/ConnectivityCubeSerializer.h"

#include "Dom/JsonObject.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Class.h"

static const TCHAR* ConnectivityCubeFaceNames[] = {
	TEXT("Front"),
	TEXT("Left"),
	TEXT("Back"),
	TEXT("Right"),
	TEXT("Upper"),
	TEXT("Lower")
};

void FConnectivityCubeSerializer::Deserialize(UScriptStruct* Struct, void* StructData, const TSharedPtr<FJsonObject> JsonValue, UObject* OptionalOuter) {
	TArray<uint8> CubeBytes;
	FMemoryWriter Writer(CubeBytes);

	for (const TCHAR* FaceName : ConnectivityCubeFaceNames) {
		const TArray<TSharedPtr<FJsonValue>>* Bits = nullptr;

		int32 NumBits = JsonValue->TryGetArrayField(FaceName, Bits) ? Bits->Num() : 0;

		Writer << NumBits;

		const int32 NumWords = FMath::DivideAndRoundUp(NumBits, 32);

		for (int32 WordIndex = 0; WordIndex < NumWords; ++WordIndex) {
			uint32 Word = 0;

			for (int32 WordBitIndex = 0; WordBitIndex < 32; ++WordBitIndex) {
				const int32 BitIndex = WordIndex * 32 + WordBitIndex;

				if (BitIndex < NumBits && (*Bits)[BitIndex]->AsBool()) {
					Word |= 1u << WordBitIndex;
				}
			}

			Writer << Word;
		}
	}

	FMemoryReader Reader(CubeBytes);
	Struct->SerializeItem(Reader, StructData, nullptr);
}
