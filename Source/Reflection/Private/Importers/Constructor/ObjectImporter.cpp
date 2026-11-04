#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/ObjectImporter.h"

UObject* IObjectImporter::CreateAsset(UObject* CreatedAsset) {
	return IImporter::CreateAsset(NewObject<UObject>(GetPackage(), GetAssetClass(), StringToName(GetAssetName()), RF_Public | RF_Standalone));
}

bool IObjectImporter::Import() {
	UObject* Asset = Create<UObject>();
	if (Asset == nullptr) return false;

	auto _ = Asset->MarkPackageDirty();

	DeserializeExports(Asset);
	GetObjectSerializer()->DeserializeObjectProperties(GetAssetData(), Asset);

	Repair(Asset);

	Asset->PostEditChange();

	return OnAssetCreation(Asset);
}
