#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Texture/TextureTypes.h"
#include "Engine/Compatibility.h"

const TArray<FString>& FTextureTypes::All() {
	static const TArray<FString> Types = {
		TEXT("Texture2D"),
#if !UE4_23_BELOW
		TEXT("Texture2DArray"),
#endif
		TEXT("TextureCube"),
		TEXT("VolumeTexture"),
		TEXT("TextureRenderTarget2D"),
		TEXT("TextureLightProfile"),
	};

	return Types;
}

bool FTextureTypes::IsSupported(const FString& Type) {
	return All().Contains(Type);
}

bool FTextureTypes::HasPixelPayload(const FString& Type) {
	return Type != TEXT("TextureRenderTarget2D");
}

bool FTextureTypes::RequiresRawMipData(const FString& Type, const bool bIsVectorDisplacementMap) {
#if PLATFORM_LINUX
	return false;
#elif UE4_26_BELOW || UE5_5_BEYOND
	return true;
#else
	if (Type == TEXT("Texture2DArray")
	 || Type == TEXT("TextureCube")
	 || Type == TEXT("VolumeTexture")
	 || Type == TEXT("TextureLightProfile")) {
		return true;
	}

	return bIsVectorDisplacementMap;
#endif
}

bool FTextureTypes::IsVectorDisplacementMap(const TSharedPtr<FJsonObject>& Export) {
	const TSharedPtr<FJsonObject>* Properties = nullptr;
	if (!Export.IsValid() || !Export->TryGetObjectField(TEXT("Properties"), Properties)) {
		return false;
	}

	FString CompressionSettings;
	if (!(*Properties)->TryGetStringField(TEXT("CompressionSettings"), CompressionSettings)) {
		return false;
	}

	return CompressionSettings.Contains(TEXT("TC_VectorDisplacementmap"))
		|| CompressionSettings.Contains(TEXT("TC_HDR"));
}
