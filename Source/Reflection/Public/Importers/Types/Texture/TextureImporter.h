#pragma once

#include "Importers/Constructor/Importer.h"

struct REFLECTION_API FTextureImport {
	static bool FromCloud(const FString& Path, const FString& FetchPath, UTexture*& OutTexture);

	static bool FromExport(const TSharedPtr<FJsonObject>& Export, const FString& Path, const FString& Type, TArray<uint8> Data, UTexture*& OutTexture);
};

template <typename AssetType>
class ITextureImporter : public IImporter {
public:
	virtual bool Import() override;
};
