#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Texture/TextureCreator.h"

#include "Importers/Constructor/ImportIssues.h"

#include "Engine/TextureCube.h"
#include "Engine/TextureLightProfile.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/VolumeTexture.h"
#include "Factories/TextureFactory.h"
#include "Factories/TextureRenderTargetFactoryNew.h"
#include "Settings/Runtime.h"
#include "Engine/EngineUtilities.h"
#include "Utilities/JsonHelpers.h"

bool FTextureCreator::Create(const FString& Type, const TSharedPtr<FJsonObject>& Export, TArray<uint8>& Data, UTexture*& OutTexture) {
	if (Type == TEXT("Texture2D")) {
		return CreateTexture2D<UTexture2D>(OutTexture, Data, Export);
	}

	if (Type == TEXT("TextureLightProfile")) {
		return CreateTexture2D<UTextureLightProfile>(OutTexture, Data, Export);
	}

#if !UE4_23_BELOW
	if (Type == TEXT("Texture2DArray")) {
		return CreateTexture2DArray(OutTexture, Data, Export);
	}
#endif

	if (Type == TEXT("TextureCube")) {
		return CreateTextureCube(OutTexture, Data, Export);
	}

	if (Type == TEXT("VolumeTexture")) {
		return CreateVolumeTexture(OutTexture, Data, Export);
	}

	if (Type == TEXT("TextureRenderTarget2D")) {
		return CreateRenderTarget2D(OutTexture, Export);
	}

	UE_LOG(LogReflection, Error, TEXT("\"%s\" is a %s, which has no texture creator"), *AssetName, *Type);

	return false;
}

bool FTextureCreator::IsRawMipData() const {

#if PLATFORM_LINUX
	return false;
#else
	return UseRawMipData;
#endif
}

bool FTextureCreator::RequireRawMipData(const TCHAR* What) const {
	if (IsRawMipData()) {
		return true;
	}

	UE_LOG(LogReflection, Error, TEXT("%s can only be rebuilt from raw mip data, which \"%s\" didn't come down as"), What, *AssetName);

	return false;
}

template <typename T>
bool FTextureCreator::CreateTexture2D(UTexture*& OutTexture, TArray<uint8>& Data, const TSharedPtr<FJsonObject>& Export) {
	UTexture2D* Texture2D;

	if (IsRawMipData()) {
		Texture2D = NewObject<T>(Package, T::StaticClass(), *AssetName, RF_Standalone | RF_Public);
	} else {
		UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
		TextureFactory->AddToRoot();
		TextureFactory->SuppressImportOverwriteDialog();

		const uint8* ImageData = Data.GetData();
		Texture2D = Cast<T>(TextureFactory->FactoryCreateBinary(T::StaticClass(), Package, *AssetName, RF_Standalone | RF_Public, nullptr,
			*FPaths::GetExtension(AssetName + ".png").ToLower(), ImageData, ImageData + Data.Num(), GWarn));
	}

	if (Texture2D == nullptr) {
		return false;
	}

	DeserializeTexture2D(Texture2D, Export);

	if (!IsRawMipData()
		&& GReflectionRuntime.IsOlderUE4Target()
		&&
		(
			Texture2D->LODGroup == TEXTUREGROUP_CharacterNormalMap
			|| Texture2D->LODGroup == TEXTUREGROUP_VehicleNormalMap
			|| Texture2D->LODGroup == TEXTUREGROUP_WorldNormalMap
			|| Texture2D->CompressionSettings == TC_Normalmap
		)) {

		const int32 Width = Texture2D->Source.GetSizeX();
		const int32 Height = Texture2D->Source.GetSizeY();

		uint8* Src = Texture2D->Source.LockMip(0);
		for (int32 i = 0, n = Width * Height; i < n; i++)
		{
			uint8* p = Src + i * 4;
			const uint8 R = p[2], G = p[1], A = p[3];
			p[0] = R; p[1] = G; p[2] = A; p[3] = 255;
		}

		Texture2D->Source.UnlockMip(0);
	}

	else {
		SetPlatformData(Texture2D, new FTexturePlatformData());
	}

	if (HasSingleMip(Export)) {
		Texture2D->MipGenSettings = TMGS_NoMipmaps;
	}

	if (IsRawMipData()) {
		FTextureCookedLayout Cooked;
		if (!GetCookedLayout(Export, 1, Cooked)) {
			return false;
		}

		if (FTexturePlatformData* PlatformData = GetPlatformData(Texture2D)) {
			PlatformData->PixelFormat = Cooked.PixelFormat;
		}

		if (!BuildSourceFromRawMip(Texture2D, Data, Cooked)) {
			return false;
		}
	}

	OutTexture = Texture2D;

	return true;
}

#if !UE4_23_BELOW
bool FTextureCreator::CreateTexture2DArray(UTexture*& OutTexture2DArray, TArray<uint8>& Data, const TSharedPtr<FJsonObject>& Export) {
	if (!RequireRawMipData(TEXT("An array"))) {
		return false;
	}

	UTexture2DArray* Texture2DArray = NewObject<UTexture2DArray>(Package, UTexture2DArray::StaticClass(), *AssetName, RF_Public | RF_Standalone);

	DeserializeTexture(Texture2DArray, Export);
	SetPlatformData(Texture2DArray, new FTexturePlatformData());

	FTextureCookedLayout Cooked;
	if (!GetCookedArrayLayout(Export, Cooked)) {
		return false;
	}

	if (FTexturePlatformData* PlatformData = GetPlatformData(Texture2DArray)) {
		PlatformData->PixelFormat = Cooked.PixelFormat;
	}

	if (HasSingleMip(Export)) {
		Texture2DArray->MipGenSettings = TMGS_NoMipmaps;
	}

	if (!BuildSourceFromRawMip(Texture2DArray, Data, Cooked)) {
		return false;
	}

	OutTexture2DArray = Texture2DArray;

	return true;
}
#endif

bool FTextureCreator::CreateTextureCube(UTexture*& OutTextureCube, TArray<uint8>& Data, const TSharedPtr<FJsonObject>& Export) {
	if (!RequireRawMipData(TEXT("A cube"))) {
		return false;
	}

	UTextureCube* TextureCube = NewObject<UTextureCube>(Package, UTextureCube::StaticClass(), *AssetName, RF_Public | RF_Standalone);

	DeserializeTexture(TextureCube, Export);
	SetPlatformData(TextureCube, new FTexturePlatformData());

	FTextureCookedLayout Cooked;
	if (!GetCookedLayout(Export, 6, Cooked)) {
		return false;
	}

	if (FTexturePlatformData* PlatformData = GetPlatformData(TextureCube)) {
		PlatformData->PixelFormat = Cooked.PixelFormat;
	}

	if (HasSingleMip(Export)) {
		TextureCube->MipGenSettings = TMGS_NoMipmaps;
	}

	if (!BuildSourceFromRawMip(TextureCube, Data, Cooked)) {
		return false;
	}

	OutTextureCube = TextureCube;

	return true;
}

bool FTextureCreator::CreateVolumeTexture(UTexture*& OutVolumeTexture, TArray<uint8>& Data, const TSharedPtr<FJsonObject>& Export) {
	if (!RequireRawMipData(TEXT("A volume"))) {
		return false;
	}

	UVolumeTexture* VolumeTexture = NewObject<UVolumeTexture>(Package, UVolumeTexture::StaticClass(), *AssetName, RF_Public | RF_Standalone);

	DeserializeTexture(VolumeTexture, Export);
	SetPlatformData(VolumeTexture, new FTexturePlatformData());

	FTextureCookedLayout Cooked;
	if (!GetCookedLayout(Export, 0, Cooked)) {
		return false;
	}

	Cooked.SlicesHalvePerMip = true;

	if (FTexturePlatformData* PlatformData = GetPlatformData(VolumeTexture)) {
		PlatformData->PixelFormat = Cooked.PixelFormat;
	}

	if (HasSingleMip(Export)) {
		VolumeTexture->MipGenSettings = TMGS_NoMipmaps;
	}

	if (!BuildSourceFromRawMip(VolumeTexture, Data, Cooked)) {
		return false;
	}

	OutVolumeTexture = VolumeTexture;

	return true;
}

bool FTextureCreator::CreateRenderTarget2D(UTexture*& OutRenderTarget2D, const TSharedPtr<FJsonObject>& Export) {
	UTextureRenderTargetFactoryNew* TextureFactory = NewObject<UTextureRenderTargetFactoryNew>();
	TextureFactory->AddToRoot();
	UTextureRenderTarget2D* RenderTarget2D = Cast<UTextureRenderTarget2D>(TextureFactory->FactoryCreateNew(UTextureRenderTarget2D::StaticClass(), Package, *AssetName, RF_Standalone | RF_Public, nullptr, GWarn));

	if (RenderTarget2D == nullptr) {
		return false;
	}

	DeserializeTexture(RenderTarget2D, Export);

	const TSharedPtr<FJsonObject> Properties = GetProperties(Export);

	int SizeX;
	if (Properties->TryGetNumberField(TEXT("SizeX"), SizeX)) RenderTarget2D->SizeX = SizeX;
	int SizeY;
	if (Properties->TryGetNumberField(TEXT("SizeY"), SizeY)) RenderTarget2D->SizeY = SizeY;

	FString AddressX;
	if (Properties->TryGetStringField(TEXT("AddressX"), AddressX)) RenderTarget2D->AddressX = static_cast<TextureAddress>(StaticEnum<TextureAddress>()->GetValueByNameString(AddressX));
	FString AddressY;
	if (Properties->TryGetStringField(TEXT("AddressY"), AddressY)) RenderTarget2D->AddressY = static_cast<TextureAddress>(StaticEnum<TextureAddress>()->GetValueByNameString(AddressY));
	FString RenderTargetFormat;
	if (Properties->TryGetStringField(TEXT("RenderTargetFormat"), RenderTargetFormat)) RenderTarget2D->RenderTargetFormat = static_cast<ETextureRenderTargetFormat>(StaticEnum<ETextureRenderTargetFormat>()->GetValueByNameString(RenderTargetFormat));

	bool bAutoGenerateMips = false;
	if (Properties->TryGetBoolField(TEXT("bAutoGenerateMips"), bAutoGenerateMips)) RenderTarget2D->bAutoGenerateMips = bAutoGenerateMips;
#if !UE4_23_BELOW
	if (bAutoGenerateMips) {
		FString MipsSamplerFilter;

		if (Properties->TryGetStringField(TEXT("MipsSamplerFilter"), MipsSamplerFilter))
			RenderTarget2D->MipsSamplerFilter = static_cast<TextureFilter>(StaticEnum<TextureFilter>()->GetValueByNameString(MipsSamplerFilter));
	}
#endif

	OutRenderTarget2D = RenderTarget2D;

	return true;
}

int64 FTextureCookedLayout::GetEncodedSize() const {
	return Source.GetEncodedSliceSize(SizeX, SizeY) * SizeZ;
}

bool FTextureCookedLayout::DropMip() {
	const int32 Smallest = Source.BlockDim;

	if (SizeX <= Smallest && SizeY <= Smallest) {
		return false;
	}

	SizeX = FMath::Max(Smallest, SizeX / 2);
	SizeY = FMath::Max(Smallest, SizeY / 2);

	if (SlicesHalvePerMip) {
		SizeZ = FMath::Max(1, SizeZ / 2);
	}

	return true;
}

bool FTextureCreator::GetCookedLayout(const TSharedPtr<FJsonObject>& Export, const int32 FallbackSlices, FTextureCookedLayout& OutCooked) const {
	int32 SizeX = 0;
	int32 ReportedSizeY = 0;

	Export->TryGetNumberField(TEXT("SizeX"), SizeX);
	Export->TryGetNumberField(TEXT("SizeY"), ReportedSizeY);

	int32 MipSlices = 0;
	int64 MipPayload = 0;
	GetFirstMipDescription(Export, MipSlices, MipPayload);

	int32 Slices = MipSlices > 1 ? MipSlices : 0;

	if (Slices <= 0 && (!Export->TryGetNumberField(TEXT("SizeZ"), Slices) || Slices <= 0)) {
		Slices = FallbackSlices > 0 ? FallbackSlices : GetReportedSliceCount(Export);

		if (Slices <= 0) {
			Slices = SizeX > 0 ? ReportedSizeY / SizeX : 0;

			UE_LOG(LogReflection, Warning, TEXT("\"%s\" was sent without a slice count, reading %d off its width. Update Mirror to import this reliably."), *AssetName, Slices);
		}
	}

	if (Slices <= 0) {
		UE_LOG(LogReflection, Error, TEXT("\"%s\" was cooked at an unusable size (%d x %d over %d slices)"), *AssetName, SizeX, ReportedSizeY, Slices);

		return false;
	}

	int32 SizeY = ReportedSizeY;

	FTextureSourceLayout Layout;
	FString PixelFormatName;
	Export->TryGetStringField(TEXT("PixelFormat"), PixelFormatName);

	if (MipPayload > 0 && FTextureFormats::GetLayout(FTextureFormats::FromName(PixelFormatName), Layout)
		&& Layout.GetEncodedSliceSize(SizeX, ReportedSizeY) == MipPayload) {
		SizeY = ReportedSizeY / Slices;

		if (SizeY * Slices != ReportedSizeY) {
			UE_LOG(LogReflection, Error, TEXT("\"%s\" was cooked at an unusable size (%d x %d over %d slices)"), *AssetName, SizeX, ReportedSizeY, Slices);

			return false;
		}
	}

	return ReadCookedLayout(Export, SizeX, SizeY, Slices, OutCooked);
}

void FTextureCreator::GetFirstMipDescription(const TSharedPtr<FJsonObject>& Export, int32& OutSlices, int64& OutPayload) {
	OutSlices = 0;
	OutPayload = 0;

	const TArray<TSharedPtr<FJsonValue>>* Mips;
	if (!Export->TryGetArrayField(TEXT("Mips"), Mips) || Mips->Num() == 0) return;

	const TSharedPtr<FJsonObject> Mip = (*Mips)[0]->AsObject();
	if (!Mip.IsValid()) return;

	Mip->TryGetNumberField(TEXT("SizeZ"), OutSlices);

	const TSharedPtr<FJsonObject>* BulkData;
	if (Mip->TryGetObjectField(TEXT("BulkData"), BulkData)) {
		(*BulkData)->TryGetNumberField(TEXT("ElementCount"), OutPayload);
	}
}

bool FTextureCreator::GetCookedArrayLayout(const TSharedPtr<FJsonObject>& Export, FTextureCookedLayout& OutCooked) const {
	int32 SizeX = 0;
	int32 SizeY = 0;

	Export->TryGetNumberField(TEXT("SizeX"), SizeX);
	Export->TryGetNumberField(TEXT("SizeY"), SizeY);

	const int32 Slices = GetReportedSliceCount(Export);

	return ReadCookedLayout(Export, SizeX, SizeY, Slices > 0 ? Slices : 1, OutCooked);
}

bool FTextureCreator::ReadCookedLayout(const TSharedPtr<FJsonObject>& Export, const int32 SizeX, const int32 SizeY, const int32 Slices, FTextureCookedLayout& OutCooked) const {
	OutCooked.SizeX = SizeX;
	OutCooked.SizeY = SizeY;
	OutCooked.SizeZ = Slices;

	FString PixelFormatName;
	Export->TryGetStringField(TEXT("PixelFormat"), PixelFormatName);
	OutCooked.PixelFormat = FTextureFormats::FromName(PixelFormatName);

	if (SizeX <= 0 || SizeY <= 0 || Slices <= 0) {
		UE_LOG(LogReflection, Error, TEXT("\"%s\" was cooked at an unusable size (%d x %d over %d slices)"), *AssetName, SizeX, SizeY, Slices);

		return false;
	}

	if (!FTextureFormats::GetLayout(OutCooked.PixelFormat, OutCooked.Source)) {
		UE_LOG(LogReflection, Error, TEXT("\"%s\" was cooked as %s, which has no source format mapping yet"), *AssetName, *PixelFormatName);

		return false;
	}

	return true;
}

int32 FTextureCreator::GetReportedSliceCount(const TSharedPtr<FJsonObject>& Export) {
	uint32 PackedData = 0;
	if (Export->TryGetNumberField(TEXT("PackedData"), PackedData)) {
		constexpr uint32 SliceMask = (1u << 30) - 1u;

		return static_cast<int32>(PackedData & SliceMask);
	}

	int32 NumSlices = 0;
	Export->TryGetNumberField(TEXT("NumSlices"), NumSlices);

	return NumSlices;
}

bool FTextureCreator::BuildSourceFromRawMip(UTexture* Texture, const TArray<uint8>& Data, const FTextureCookedLayout& Cooked) const {
	FTextureCookedLayout Mip = Cooked;

	while (Data.Num() != Mip.GetEncodedSize()) {
		if (!Mip.DropMip()) {
			UE_LOG(LogReflection, Error, TEXT("\"%s\" came back with %d bytes, which matches no mip of a %d x %d x %d %s texture"), *AssetName, Data.Num(), Cooked.SizeX, Cooked.SizeY, Cooked.SizeZ, *UTexture::GetPixelFormatEnum()->GetNameStringByValue(Cooked.PixelFormat));

			return false;
		}
	}

	if (Mip.SizeX != Cooked.SizeX) {
		UE_LOG(LogReflection, Error, TEXT("\"%s\" only had mip data down at %d x %d x %d, importing that instead of %d x %d x %d"), *AssetName, Mip.SizeX, Mip.SizeY, Mip.SizeZ, Cooked.SizeX, Cooked.SizeY, Cooked.SizeZ);

		FImportIssues::ReportFor(
			AssetName,
			Package != nullptr ? Package->GetName() : FString(),
			Texture != nullptr ? Texture->GetClass()->GetName() : FString(),
			EImportIssue::Data,
			FString::Printf(
				TEXT("Imported at %d x %d x %d instead of %d x %d x %d"),
				Mip.SizeX, Mip.SizeY, Mip.SizeZ, Cooked.SizeX, Cooked.SizeY, Cooked.SizeZ
			),
			TEXT("The mips above that one weren't in a mounted pak, so this is all there was to import.")
		);
	}

	const int64 EncodedSliceSize = Mip.Source.GetEncodedSliceSize(Mip.SizeX, Mip.SizeY);
	const int64 DecodedSliceSize = Mip.Source.GetDecodedSliceSize(Mip.SizeX, Mip.SizeY);

	uint8* SourceData = static_cast<uint8*>(FMemory::Malloc(DecodedSliceSize * Mip.SizeZ));

	for (int32 SliceIndex = 0; SliceIndex < Mip.SizeZ; SliceIndex++) {
		const uint8* EncodedSlice = Data.GetData() + EncodedSliceSize * SliceIndex;
		uint8* DecodedSlice = SourceData + DecodedSliceSize * SliceIndex;

		if (Mip.Source.RequiresDecode) {
			FTextureFormats::Decode(EncodedSlice, DecodedSlice, Mip.SizeX, Mip.SizeY, DecodedSliceSize, Mip.PixelFormat);
		} else {
			FMemory::Memcpy(DecodedSlice, EncodedSlice, DecodedSliceSize);
		}
	}

	Texture->Source.Init(Mip.SizeX, Mip.SizeY, Mip.SizeZ, 1, Mip.Source.SourceFormat, SourceData);

	FMemory::Free(SourceData);

	if (Texture->LODGroup == 255) {
		Texture->LODGroup = TEXTUREGROUP_World;
	}

	Texture->UpdateResource();

	FlushRenderingCommands();

	return true;
}

void FTextureCreator::DeserializeTexture(UTexture* Texture, const TSharedPtr<FJsonObject>& Export) const {
	if (Texture == nullptr) return;

	GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(GetProperties(Export), {
		"ImportedSize",
		"LODBias"
	}), Texture);
}

void FTextureCreator::DeserializeTexture2D(UTexture2D* Texture2D, const TSharedPtr<FJsonObject>& Export) const {
	if (Texture2D == nullptr) return;

	DeserializeTexture(Texture2D, Export);

	const TSharedPtr<FJsonObject> Properties = GetProperties(Export);

	FString AddressX;
	FString AddressY;
	bool bHasBeenPaintedInEditor;

	if (Properties->TryGetStringField(TEXT("AddressX"), AddressX)) Texture2D->AddressX = static_cast<TextureAddress>(StaticEnum<TextureAddress>()->GetValueByNameString(AddressX));
	if (Properties->TryGetStringField(TEXT("AddressY"), AddressY)) Texture2D->AddressY = static_cast<TextureAddress>(StaticEnum<TextureAddress>()->GetValueByNameString(AddressY));
	if (Properties->TryGetBoolField(TEXT("bHasBeenPaintedInEditor"), bHasBeenPaintedInEditor)) Texture2D->bHasBeenPaintedInEditor = bHasBeenPaintedInEditor;

	int FirstResourceMemMip;
	int LevelIndex;

	if (Properties->TryGetNumberField(TEXT("FirstResourceMemMip"), FirstResourceMemMip)) Texture2D->FirstResourceMemMip = FirstResourceMemMip;
	if (Properties->TryGetNumberField(TEXT("LevelIndex"), LevelIndex)) Texture2D->LevelIndex = LevelIndex;

	FTexturePlatformData* PlatformData = GetPlatformData(Texture2D);
	if (PlatformData == nullptr) return;

	int SizeX;
	int SizeY;
#if !UE4_23_BELOW
	uint32 PackedData;
#endif
	FString PixelFormat;

	if (Export->TryGetNumberField(TEXT("SizeX"), SizeX)) PlatformData->SizeX = SizeX;
	if (Export->TryGetNumberField(TEXT("SizeY"), SizeY)) PlatformData->SizeY = SizeY;
#if UE4_23_BELOW
	int NumSlices;
	if (Export->TryGetNumberField(TEXT("NumSlices"), NumSlices)) PlatformData->NumSlices = NumSlices;
#else
	if (Export->TryGetNumberField(TEXT("PackedData"), PackedData)) PlatformData->PackedData = PackedData;
#endif
	if (Export->TryGetStringField(TEXT("PixelFormat"), PixelFormat)) PlatformData->PixelFormat = FTextureFormats::FromName(PixelFormat);
}

bool FTextureCreator::HasSingleMip(const TSharedPtr<FJsonObject>& Export) {
	const TArray<TSharedPtr<FJsonValue>>* Mips;

	return Export->TryGetArrayField(TEXT("Mips"), Mips) && Mips->Num() == 1;
}

TSharedPtr<FJsonObject> FTextureCreator::GetProperties(const TSharedPtr<FJsonObject>& Export) {
	const TSharedPtr<FJsonObject>* Properties = nullptr;

	if (Export.IsValid() && Export->TryGetObjectField(TEXT("Properties"), Properties)) {
		return *Properties;
	}

	return MakeShared<FJsonObject>();
}
