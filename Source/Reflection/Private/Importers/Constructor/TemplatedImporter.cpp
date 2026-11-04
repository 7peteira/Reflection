#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/TemplatedImporter.h"

template class ITemplatedImporter<UObject>;

template <typename AssetType>
UObject* ITemplatedImporter<AssetType>::CreateAsset(UObject* CreatedAsset) {
	return IImporter::CreateAsset(NewObject<AssetType>(GetPackage(), GetAssetClass() ? GetAssetClass() : AssetType::StaticClass(), StringToName(GetAssetName()), RF_Public | RF_Standalone));
}

template <typename AssetType>
bool ITemplatedImporter<AssetType>::Import() {
	AssetType* Asset = Create<AssetType>();

	Asset->MarkPackageDirty();

	GetObjectSerializer()->SetExportForDeserialization(GetAssetExport(), Asset);
	GetObjectSerializer()->Parent = Asset;

	GetObjectSerializer()->bUseExperimentalSpawning = true;
	GetObjectSerializer()->SpawnExport(AssetExport, true);

	return OnAssetCreation(Asset);
}
