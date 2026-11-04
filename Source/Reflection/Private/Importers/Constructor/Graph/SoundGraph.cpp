#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/Graph/SoundGraph.h"

#include "Misc/MessageDialog.h"
#include "Modules/Cloud/Cloud.h"
#include "Sound/SoundCue.h"

#include "Engine/Log.h"
#include "Importers/Constructor/Graph/Utilities/SoundNodeLayoutUtilities.h"
#include "Importers/Constructor/ImportIssues.h"
#include "Modules/UI/StyleModule.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

#if UE4_25_BELOW
#include "Sound/SoundWave.h"
#endif
#include "Engine/EngineUtilities.h"
#include "Utilities/JsonHelpers.h"

namespace {
	const TCHAR* SoundGroupsSection = TEXT("[/Script/Engine.SoundGroups]");

	int8 GSoundGroupsDeclared = -1;

	bool GWarnedAboutSoundGroups = false;

	bool ProjectDeclaresSoundGroups() {
		TArray<FString> ConfigFiles;
		IFileManager::Get().FindFilesRecursive(ConfigFiles, *FPaths::ProjectConfigDir(), TEXT("*.ini"), true, false);

		for (const FString& ConfigFile : ConfigFiles) {
			FString Contents;

			if (FFileHelper::LoadFileToString(Contents, *ConfigFile) && Contents.Contains(SoundGroupsSection)) {
				return true;
			}
		}

		return false;
	}

	FString ExtractNodeName(const FString& ObjectName) {
		const int32 ColonIndex = ObjectName.Find(TEXT(":"));
		const int32 QuoteIndex = ObjectName.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);

		return ObjectName.Mid(ColonIndex + 1, QuoteIndex - ColonIndex - 1);
	}
}

void ISoundGraph::NotifySoundGroupsUnconfigured() {
	if (GSoundGroupsDeclared < 0) {
		GSoundGroupsDeclared = ProjectDeclaresSoundGroups() ? 1 : 0;
	}

	if (GSoundGroupsDeclared == 1) {
		return;
	}

	FImportIssues::ReportFor(
		TEXT("Project Settings"),
		FString(),
		FString(),
		EImportIssue::Setting,
		TEXT("No [/Script/Engine.SoundGroups] in this project's config"),
		TEXT("Imported audio uses the engine's sound group defaults until the profiles the source game ships with are added to Config/DefaultEngine.ini.")
	);

	if (GWarnedAboutSoundGroups) {
		return;
	}

	GWarnedAboutSoundGroups = true;

	UE_LOG(LogReflection, Warning, TEXT("This project has no %s section in its config, so imported audio falls back on the engine's sound group defaults. Add the profiles the source game uses to Config/DefaultEngine.ini."), SoundGroupsSection);

	AppendNotificationWithHandler(
		FText::FromString("Sound Groups Not Configured"),
		FText::FromString("No [/Script/Engine.SoundGroups] in this project's config, so imported audio uses the engine defaults."),
		10.0f,
		FReflectionStyle::Get().GetBrush("Toolbar.Icon"),
		SNotificationItem::CS_None,
		false,
		420.0f
	);
}

void ISoundGraph::ConstructNodes(USoundCue* SoundCue, TMap<FString, USoundNode*>& OutNodes) {
	for (const FUObjectExport* Export : GetContainer()->Exports) {
		FString Name = Export->GetName().ToString();
		FString Type = Export->GetType().ToString();

		if (Type.StartsWith("SoundNode")) {
			USoundNode* SoundCueNode = CreateEmptyNode(FName(*Name), FName(*Type), SoundCue);

			if (SoundCueNode == nullptr) {
				continue;
			}

			OutNodes.Add(Name, SoundCueNode);
		}
	}
}

USoundNode* ISoundGraph::CreateEmptyNode(const FName Name, const FName Type, USoundCue* SoundCue) {
	const UClass* Class = FindClassByType(Type.ToString());

	if (Class == nullptr || !Class->IsChildOf(USoundNode::StaticClass())) {
		UE_LOG(LogReflection, Warning, TEXT("Skipping sound node \"%s\", \"%s\" is not a node type this engine has"), *Name.ToString(), *Type.ToString());

		FImportIssues::Report(
			EImportIssue::MissingClass,
			TEXT("Missing sound node ") + Type.ToString(),
			TEXT("This engine build has no class for it, so the node was left out of the cue.")
		);

		return nullptr;
	}

	USoundNode* SoundNode = NewObject<USoundNode>(SoundCue, ToNewObjectClass(Class), Name, RF_Transactional);
	SoundCue->AllNodes.Add(SoundNode);
	SoundCue->SetupSoundNode(SoundNode, false);

	return SoundNode;
}

void ISoundGraph::SetupNodes(const USoundCue* SoundCueAsset, TMap<FString, USoundNode*> SoundCueNodes) {
	NotifySoundGroupsUnconfigured();

	if (SoundCueAsset == nullptr || SoundCueAsset->SoundCueGraph == nullptr) {
		return;
	}

	const TSharedPtr<FJsonObject> AssetProperties = AssetExport->GetProperties();

	if (AssetProperties.IsValid() && AssetProperties->HasField(TEXT("FirstNode"))) {
		const TSharedPtr<FJsonObject> FirstNodeProp = AssetProperties->TryGetField(TEXT("FirstNode"))->AsObject();

		if (FirstNodeProp.IsValid() && FirstNodeProp->HasTypedField<EJson::String>(TEXT("ObjectName"))) {
			const FString ChildNodeName = ExtractNodeName(FirstNodeProp->GetStringField(TEXT("ObjectName")));

			USoundNode** FirstNode = SoundCueNodes.Find(ChildNodeName);

			UEdGraphNode* RootNode =SoundCueAsset->SoundCueGraph->Nodes.IsValidIndex(0) ? SoundCueAsset->SoundCueGraph->Nodes[0] : nullptr;

			if (FirstNode && *FirstNode && RootNode) {
				ConnectEdGraphNode((*FirstNode)->GetGraphNode(), RootNode, 0);
			}
		}
	}

	for (FUObjectExport* Export : GetContainer()->Exports) {
		if (!Export->JsonObject->HasField(TEXT("Properties")) || !Export->GetType().ToString().StartsWith("SoundNode")) {
			continue;
		}

		TSharedPtr<FJsonObject> NodeProperties = Export->GetProperties();

		USoundNode** CurrentNode = SoundCueNodes.Find(Export->GetName().ToString());

		if (CurrentNode == nullptr || *CurrentNode == nullptr) {
			continue;
		}

		USoundNode* Node = *CurrentNode;

		if (NodeProperties->HasField(TEXT("ChildNodes"))) {
			TArray<TSharedPtr<FJsonValue>> CurrentNodeChildNodes = NodeProperties->TryGetField(TEXT("ChildNodes"))->AsArray();

			int32 ConnectionIndex = 0;

			for (const TSharedPtr<FJsonValue>& CurrentNodeValue : CurrentNodeChildNodes) {
				const TSharedPtr<FJsonObject> CurrentNodeChildNode =CurrentNodeValue.IsValid() ? CurrentNodeValue->AsObject() : nullptr;

				if (!Node->ChildNodes.IsValidIndex(ConnectionIndex)) {
					if (ConnectionIndex > Node->ChildNodes.Num() || Node->ChildNodes.Num() >= Node->GetMaxChildNodes()) {
						UE_LOG(LogReflection, Warning, TEXT("\"%s\" node \"%s\" (%s) lists %d child nodes, the class takes %d - the rest were left unconnected"),
							*GetAssetName(), *Export->GetName().ToString(), *Export->GetType().ToString(), CurrentNodeChildNodes.Num(), Node->GetMaxChildNodes());

						FImportIssues::Report(
							EImportIssue::Data,
							FString::Printf(TEXT("%s lists more child nodes than %s takes"), *Export->GetName().ToString(), *Export->GetType().ToString()),
							FString::Printf(TEXT("%d listed, %d connected - the rest were left off."), CurrentNodeChildNodes.Num(), Node->GetMaxChildNodes())
						);

						break;
					}

					Node->InsertChildNode(ConnectionIndex);
				}

				if (CurrentNodeChildNode.IsValid() && CurrentNodeChildNode->HasTypedField<EJson::String>(TEXT("ObjectName"))) {
					const FString CurrentChildNodeName = ExtractNodeName(CurrentNodeChildNode->GetStringField(TEXT("ObjectName")));

					USoundNode** CurrentChildNode = SoundCueNodes.Find(CurrentChildNodeName);
					const int CurrentPin = ConnectionIndex + 1;

					if (CurrentChildNode && *CurrentChildNode) {
						ConnectSoundNode(*CurrentChildNode, Node, CurrentPin);
					}
				}

				ConnectionIndex++;
			}
		}

		GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(NodeProperties, TArray<FString> {
			"ChildNodes"
		}), *CurrentNode);

		if (Cast<USoundNodeWavePlayer>(Node) != nullptr) {
			const auto WavePlayerNode = Cast<USoundNodeWavePlayer>(Node);

			if (NodeProperties->HasField(TEXT("SoundWaveAssetPtr"))) {
				FString AssetPtr;

				if (NodeProperties->HasTypedField<EJson::String>(TEXT("SoundWaveAssetPtr"))) {
					AssetPtr = NodeProperties->GetStringField(TEXT("SoundWaveAssetPtr"));
				} else {
					const TSharedPtr<FJsonObject> AssetPtrObject = NodeProperties->TryGetField(TEXT("SoundWaveAssetPtr"))->AsObject();

					if (AssetPtrObject.IsValid()) {
						AssetPtrObject->TryGetStringField(TEXT("AssetPathName"), AssetPtr);
					}
				}

				if (AssetPtr.IsEmpty()) {
					continue;
				}

				TObjectPtr<USoundWave> SoundWave = LoadObjectByPath<USoundWave>(AssetPtr);

				if (SoundWave == nullptr) {
					FString WavePath;
					FString WaveName; {
						AssetPtr.Split(".", &WavePath, &WaveName);
					}

					SoundWave = DownloadWrapper<USoundWave>(SoundWave, TEXT("SoundWave"), WaveName, WavePath);
				}

				if (SoundWave != nullptr) {
					WavePlayerNode->SetSoundWave(SoundWave);
				}
			}
		}
	}

	OrganizeNodes(SoundCueAsset);
}

void ISoundGraph::OrganizeNodes(const USoundCue* SoundCueAsset) {
	if (SoundCueAsset != nullptr) {
		OrganizeSoundCueGraph(SoundCueAsset->SoundCueGraph);
	}
}

void ISoundGraph::ConnectEdGraphNode(UEdGraphNode* NodeToConnect, UEdGraphNode* NodeToConnectTo, const int Pin = 1) {
	if (NodeToConnect == nullptr || NodeToConnectTo == nullptr) {
		return;
	}

	if (!NodeToConnect->Pins.IsValidIndex(0) || !NodeToConnectTo->Pins.IsValidIndex(Pin)) {
		return;
	}

	NodeToConnect->Pins[0]->MakeLinkTo(NodeToConnectTo->Pins[Pin]);
}

void ISoundGraph::ConnectSoundNode(const USoundNode* NodeToConnect, const USoundNode* NodeToConnectTo, const int Pin = 1) {
	if (NodeToConnect == nullptr || NodeToConnectTo == nullptr) {
		return;
	}

	ConnectEdGraphNode(NodeToConnect->GetGraphNode(), NodeToConnectTo->GetGraphNode(), Pin);
}
