#pragma once

#include "Importers/Constructor/Importer.h"

class REFLECTION_API IObjectImporter : public IImporter {
public:
	virtual UObject* CreateAsset(UObject* CreatedAsset = nullptr) override;
	virtual bool Import() override;

	virtual void Repair(UObject* Asset) const {}
};
