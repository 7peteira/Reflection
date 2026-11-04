#pragma once

#include "./Importer.h"

template <typename AssetType>
class ITemplatedImporter : public IImporter {
public:
	virtual UObject* CreateAsset(UObject* CreatedAsset) override;
	virtual bool Import() override;
};
