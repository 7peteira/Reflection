#pragma once

#include "Importers/Constructor/ObjectImporter.h"

class INiagaraEmitterImporter final : public IObjectImporter {
public:
	virtual void Repair(UObject* Asset) const override;

	virtual void Validate(UObject* Asset) const override;
};

REGISTER_IMPORTER(INiagaraEmitterImporter, TArray<FString>{
	TEXT("NiagaraEmitter")
}, TEXT("Niagara Assets"));
