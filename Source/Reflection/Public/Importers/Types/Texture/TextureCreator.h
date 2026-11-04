#pragma once

#include "Importers/Types/Texture/TextureFormats.h"
#include "Serializers/PropertySerializer.h"
#include "Engine/Texture2D.h"
#include "Dom/JsonObject.h"

struct FTextureCookedLayout {
	int32 SizeX = 0;

	int32 SizeY = 0;

	int32 SizeZ = 1;

	bool SlicesHalvePerMip = false;

	EPixelFormat PixelFormat = PF_Unknown;
	FTextureSourceLayout Source;

	int64 GetEncodedSize() const;

	bool DropMip();
};

struct FTextureCreator {
public:
	FTextureCreator(const FString& AssetName, const FString& FilePath, UPackage* Package, const bool UseRawMipData)
		: UseRawMipData(UseRawMipData), AssetName(AssetName), FilePath(FilePath), Package(Package)
	{
		PropertySerializer = NewObject<UPropertySerializer>();
		ObjectSerializer = NewObject<UObjectSerializer>();

		ObjectSerializer->SetPropertySerializer(PropertySerializer);
	}

	bool Create(const FString& Type, const TSharedPtr<FJsonObject>& Export, TArray<uint8>& Data, UTexture*& OutTexture);

	bool IsRawMipData() const;

private:
	bool RequireRawMipData(const TCHAR* What) const;

	template <class T = UTexture2D>
	bool CreateTexture2D(UTexture*& OutTexture, TArray<uint8>& Data, const TSharedPtr<FJsonObject>& Export);

#if !UE4_23_BELOW
	bool CreateTexture2DArray(UTexture*& OutTexture2DArray, TArray<uint8>& Data, const TSharedPtr<FJsonObject>& Export);
#endif
	bool CreateTextureCube(UTexture*& OutTextureCube, TArray<uint8>& Data, const TSharedPtr<FJsonObject>& Export);
	bool CreateVolumeTexture(UTexture*& OutVolumeTexture, TArray<uint8>& Data, const TSharedPtr<FJsonObject>& Export);
	bool CreateRenderTarget2D(UTexture*& OutRenderTarget2D, const TSharedPtr<FJsonObject>& Export);

	bool GetCookedLayout(const TSharedPtr<FJsonObject>& Export, int32 FallbackSlices, FTextureCookedLayout& OutCooked) const;

	bool GetCookedArrayLayout(const TSharedPtr<FJsonObject>& Export, FTextureCookedLayout& OutCooked) const;

	bool ReadCookedLayout(const TSharedPtr<FJsonObject>& Export, int32 SizeX, int32 SizeY, int32 Slices, FTextureCookedLayout& OutCooked) const;

	static int32 GetReportedSliceCount(const TSharedPtr<FJsonObject>& Export);

	static void GetFirstMipDescription(const TSharedPtr<FJsonObject>& Export, int32& OutSlices, int64& OutPayload);

	bool BuildSourceFromRawMip(UTexture* Texture, const TArray<uint8>& Data, const FTextureCookedLayout& Cooked) const;

	void DeserializeTexture(UTexture* Texture, const TSharedPtr<FJsonObject>& Export) const;
	void DeserializeTexture2D(UTexture2D* Texture2D, const TSharedPtr<FJsonObject>& Export) const;

	static bool HasSingleMip(const TSharedPtr<FJsonObject>& Export);

	static TSharedPtr<FJsonObject> GetProperties(const TSharedPtr<FJsonObject>& Export);

	bool UseRawMipData = true;

protected:
	FString AssetName;
	FString FilePath;
	UPackage* Package;
	UPropertySerializer* PropertySerializer;
	UObjectSerializer* ObjectSerializer;

public:
	FORCEINLINE UObjectSerializer* GetObjectSerializer() const { return ObjectSerializer; }
};
