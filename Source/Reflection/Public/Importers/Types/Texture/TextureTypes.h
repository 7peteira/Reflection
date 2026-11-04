#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct REFLECTION_API FTextureTypes {
	static const TArray<FString>& All();

	static bool IsSupported(const FString& Type);

	static bool HasPixelPayload(const FString& Type);

	static bool RequiresRawMipData(const FString& Type, bool bIsVectorDisplacementMap);

	static bool IsVectorDisplacementMap(const TSharedPtr<FJsonObject>& Export);
};
