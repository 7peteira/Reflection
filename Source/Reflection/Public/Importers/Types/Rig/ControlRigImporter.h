#pragma once

#include "Importers/Constructor/Importer.h"

#if REFLECTION_CONTROL_RIG

class UControlRigBlueprint;

class IControlRigImporter final : public IImporter {
public:
	virtual UObject* CreateAsset(UObject* CreatedAsset) override;
	virtual bool Import() override;

private:
	int32 ConstructHierarchy(UControlRigBlueprint* Blueprint) const;

	int32 ConstructVariables(UControlRigBlueprint* Blueprint);

	void ConstructGraph(UControlRigBlueprint* Blueprint) const;

	void ConstructSettings(UControlRigBlueprint* Blueprint) const;

	void ApplyVariableDefaults(const UControlRigBlueprint* Blueprint) const;
};

REGISTER_IMPORTER(IControlRigImporter, {
	TEXT("ControlRigBlueprintGeneratedClass")
}, "Animation Assets");

#endif
