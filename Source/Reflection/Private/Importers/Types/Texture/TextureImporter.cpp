#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Texture/TextureImporter.h"

#include "Importers/Types/Texture/TextureCreator.h"
#include "Importers/Types/Texture/TextureTypes.h"
#include "Importers/Constructor/Asset.h"

#include "Engine/TextureLightProfile.h"
#include "Engine/EngineUtilities.h"
#include "Engine/Log.h"
#include "Modules/Cloud/Cloud.h"
#include "Settings/ReflectionSettings.h"
#include "Utilities/ContentBrowser.h"

#if UE4_25_BELOW
#include "AssetRegistryModule.h"
#else
#include "AssetRegistry/AssetRegistryModule.h"
#endif

template class ITextureImporter<UTexture>;
template class ITextureImporter<UTextureLightProfile>;

template <typename AssetType>
bool ITextureImporter<AssetType>::Import() {
	TObjectPtr<AssetType> Texture = nullptr;
	DownloadWrapper<AssetType>(Texture, GetAssetType(), GetAssetName(), GetPackage()->GetPathName());

	return true;
}

static bool DownloadPixels(const FString& FetchPath, const FString& Type, const TSharedPtr<FJsonObject>& Export, TArray<uint8>& OutData) {
	const bool UseRawMipData = FTextureTypes::RequiresRawMipData(Type, FTextureTypes::IsVectorDisplacementMap(Export));

	FString Named;

	Export->TryGetStringField(TEXT("Name"), Named);

	return Cloud::Export::GetBinaryBlocking(FetchPath, UseRawMipData ? TEXT("application/octet-stream") : TEXT("image/png"), OutData, Named);
}

bool FTextureImport::FromCloud(const FString& Path, const FString& FetchPath, UTexture*& OutTexture) {
	if (Path.IsEmpty()) {
		return false;
	}

	const TSharedPtr<FJsonObject> Response = Cloud::Export::GetRawBlocking(FetchPath);
	if (Response == nullptr) {
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>> Exports = Response->GetArrayField(TEXT("exports"));
	if (Exports.Num() == 0) {
		return false;
	}

	int32 Wanted = INDEX_NONE;

	if (FString Leaf; Path.Split(TEXT("."), nullptr, &Leaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) {
		if (Leaf.IsNumeric()) {
			if (const int32 At = FCString::Atoi(*Leaf); Exports.IsValidIndex(At)) {
				Wanted = At;
			}
		} else {
			for (int32 At = 0; At < Exports.Num() && Wanted == INDEX_NONE; ++At) {
				const TSharedPtr<FJsonObject> One = Exports[At].IsValid() ? Exports[At]->AsObject() : nullptr;

				FString Named;

				if (One.IsValid() && One->TryGetStringField(TEXT("Name"), Named) && Named == Leaf) {
					Wanted = At;
				}
			}
		}
	}

	for (int32 At = 0; At < Exports.Num() && Wanted == INDEX_NONE; ++At) {
		const TSharedPtr<FJsonObject> One = Exports[At].IsValid() ? Exports[At]->AsObject() : nullptr;

		FString Named;

		if (One.IsValid() && One->TryGetStringField(TEXT("Type"), Named) && FTextureTypes::IsSupported(Named)) {
			Wanted = At;
		}
	}

	const TSharedPtr<FJsonObject> Export = Exports[Wanted == INDEX_NONE ? 0 : Wanted]->AsObject();
	const FString Type = Export->GetStringField(TEXT("Type"));

	TArray<uint8> Data;

	if (FTextureTypes::HasPixelPayload(Type) && !DownloadPixels(FetchPath, Type, Export, Data)) {
		return false;
	}

	return FromExport(Export, Path, Type, Data, OutTexture);
}

bool FTextureImport::FromExport(const TSharedPtr<FJsonObject>& Export, const FString& Path, const FString& Type, TArray<uint8> Data, UTexture*& OutTexture) {
	const UReflectionSettings* Settings = GetSettings();

	FString PackagePath;
	FString AssetName; {
		Path.Split(".", &PackagePath, &AssetName);
	}

	if (FString Named; AssetName.IsNumeric() && Export->TryGetStringField(TEXT("Name"), Named)) {
		AssetName = Named;
	}

	const FString PackageName = Settings->AssetSettings.PackagedAssets == ERPackagedAssets::Separate
		? AssetName
		: FPaths::GetBaseFilename(PackagePath);

	FString FailureReason;
	UPackage* Package = FAssetUtilities::CreateAssetPackage(PackageName, PackagePath, FailureReason);

	if (Package == nullptr) {
		UE_LOG(LogReflection, Error, TEXT("No package for \"%s\": %s"), *Path, *FailureReason);

		return false;
	}

	const bool UseRawMipData = FTextureTypes::RequiresRawMipData(Type, FTextureTypes::IsVectorDisplacementMap(Export));

	FTextureCreator TextureCreator = FTextureCreator(AssetName, Path, Package, UseRawMipData);

	UTexture* Texture = nullptr;
	if (!TextureCreator.Create(Type, Export, Data, Texture) || Texture == nullptr) {
		return false;
	}

	FAssetRegistryModule::AssetCreated(Texture);
	if (!Texture->MarkPackageDirty()) {
		return false;
	}

	Package->SetDirtyFlag(true);
	Texture->PostEditChange();
	Texture->AddToRoot();
	Package->FullyLoad();

	if (FAssetUtilities::ImportDepth <= 1) BrowseToAsset(Texture);

	if (Settings->AssetSettings.SaveAssets) {
		SavePackage(Package);
	}

	OutTexture = Texture;

	return true;
}
