#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Audio/SoundCueImporter.h"
#include "Sound/SoundCue.h"
#include "Utilities/JsonHelpers.h"

UObject* ISoundCueImporter::CreateAsset(UObject* CreatedAsset) {
	return IImporter::CreateAsset(NewObject<USoundCue>(GetPackage(), *GetAssetName(), RF_Public | RF_Standalone));
}

bool ISoundCueImporter::Import() {
	USoundCue* SoundCue = Create<USoundCue>();

	if (!SoundCue) {
		return false;
	}

	SoundCue->PreEditChange(nullptr);

	{
		TMap<FString, USoundNode*> SoundCueNodes;

		ConstructNodes(SoundCue, SoundCueNodes);
		SetupNodes(SoundCue, SoundCueNodes);
	}

	GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(GetAssetData(), TArray<FString> {
		"FirstNode"
	}), SoundCue);

	SoundCue->PostEditChange();
	SoundCue->CompileSoundNodesFromGraphNodes();

	return OnAssetCreation(SoundCue);
}
