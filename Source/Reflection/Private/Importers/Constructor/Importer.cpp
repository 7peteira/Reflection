#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/Importer.h"
#include "Importers/Constructor/Asset.h"

#include "Settings/ReflectionSettings.h"

#include "Misc/MessageDialog.h"

#include "Engine/Log.h"
#include "Engine/EngineUtilities.h"

UObject* IImporter::CreateAsset(UObject* CreatedAsset) {
	if (CreatedAsset) {
		AssetExport->Object = CreatedAsset;

		return CreatedAsset;
	}

	return nullptr;
}

void IImporter::Save() const {
	const UReflectionSettings* Settings = GetSettings();

	if (GetPackage() == nullptr) {
		UE_LOG(LogReflection, Error, TEXT("IImporter::Save: Package is null"));
		return;
	}

	if (Settings->AssetSettings.SaveAssets) {
		SavePackage(GetPackage());
	}
}

bool IImporter::OnAssetCreation(UObject* Asset) const {
	Validate(Asset);

	const bool Synced = HandleAssetCreation(Asset, GetPackage(), FAssetUtilities::ImportDepth <= 1);
	if (Synced) {
		Save();
	}

	return Synced;
}
