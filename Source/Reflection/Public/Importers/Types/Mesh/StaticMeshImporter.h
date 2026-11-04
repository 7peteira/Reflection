#pragma once

#include "Importers/Constructor/Importer.h"

class IStaticMeshImporter : public IImporter {
public:
	virtual UObject* CreateAsset(UObject* CreatedAsset) override;
	virtual bool Import() override;

private:
	bool BuildLod(UStaticMesh* StaticMesh, const FUObjectJsonValueExport& Lod, int32 LodIndex);

	void NormalizeLegacyStaticMeshMaterials(UStaticMesh* StaticMesh, const TSharedPtr<FJsonObject>& Properties);

	static void BuildMaterialSlots(UStaticMesh* StaticMesh, const TArray<TSharedPtr<FJsonValue>>& Slots);

	void BuildCollisionAndSockets(UStaticMesh* StaticMesh);

	int32 FirstColoredLod = INDEX_NONE;
};

REGISTER_IMPORTER(IStaticMeshImporter, {
	"StaticMesh"
}, "Mesh Assets");
