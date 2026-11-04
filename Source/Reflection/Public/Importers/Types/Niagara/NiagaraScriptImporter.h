#pragma once

#include "Importers/Constructor/ObjectImporter.h"

class INiagaraScriptImporter final : public IObjectImporter {
public:
	virtual void Repair(UObject* Asset) const override;

	virtual void Validate(UObject* Asset) const override;
};

REGISTER_IMPORTER(INiagaraScriptImporter, TArray<FString>{
	TEXT("NiagaraScript")
}, TEXT("Niagara Assets"));
