#pragma once

#include "Importers/Constructor/Importer.h"
#include "Sound/SoundNodeWavePlayer.h"

class ISoundGraph : public IImporter {
public:
	static void ConnectEdGraphNode(UEdGraphNode* NodeToConnect, UEdGraphNode* NodeToConnectTo, int Pin);
	static void ConnectSoundNode(const USoundNode* NodeToConnect, const USoundNode* NodeToConnectTo, int Pin);

	static USoundNode* CreateEmptyNode(FName Name, FName Type, USoundCue* SoundCue);

	static void NotifySoundGroupsUnconfigured();

	void ConstructNodes(USoundCue* SoundCue, TMap<FString, USoundNode*>& OutNodes);
	void SetupNodes(const USoundCue* SoundCueAsset, TMap<FString, USoundNode*> SoundCueNodes);
	void OrganizeNodes(const USoundCue* SoundCueAsset);
};
