#pragma once

#include "Importers/Constructor/Importer.h"

class IVectorFieldStaticImporter : public IImporter {
public:
	virtual UObject* CreateAsset(UObject* CreatedAsset) override;
	virtual bool Import() override;

private:
	bool DownloadVolumeData(int32 ExpectedSize, TArray<uint8>& OutData) const;
};

REGISTER_IMPORTER(IVectorFieldStaticImporter, {
	"VectorFieldStatic"
}, "Cascade Assets");
