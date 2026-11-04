#pragma once

#include "Importers/Constructor/ObjectImporter.h"

class INiagaraParameterCollectionImporter final : public IObjectImporter {
public:
	virtual void Repair(UObject* Asset) const override;

	virtual void Validate(UObject* Asset) const override;
};

REGISTER_IMPORTER(INiagaraParameterCollectionImporter, TArray<FString>{
	TEXT("NiagaraParameterCollection")
}, TEXT("Niagara Assets"));
