#pragma once

#include "Engine/Compatibility.h"
#include "Engine/Texture.h"
#include "PixelFormat.h"

struct FTextureSourceLayout {
	ETextureSourceFormat SourceFormat = TSF_BGRA8;
	int32 BytesPerVoxel = 4;

	int32 BlockDim = 1;
	int32 BytesPerBlock = 4;
	bool RequiresDecode = false;

	int64 GetEncodedSliceSize(int32 SizeX, int32 SizeY) const;

	int64 GetDecodedSliceSize(int32 SizeX, int32 SizeY) const;
};

struct REFLECTION_API FTextureFormats {
	static EPixelFormat FromName(const FString& Name);

	static bool GetLayout(EPixelFormat Format, FTextureSourceLayout& OutLayout);

	static void Decode(const uint8* Data, uint8* OutData, int32 SizeX, int32 SizeY, int64 DecodedSize, EPixelFormat Format);
};
