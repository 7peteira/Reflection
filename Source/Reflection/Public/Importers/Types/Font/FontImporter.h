#pragma once

#include "Importers/Constructor/ObjectImporter.h"

class IFontImporter final : public IObjectImporter {
public:
	virtual void Repair(UObject* Asset) const override;

	virtual void Validate(UObject* Asset) const override;
};

REGISTER_IMPORTER(IFontImporter, TArray<FString>{
	TEXT("Font")
}, TEXT("Font Assets"));
