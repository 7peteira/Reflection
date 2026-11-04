#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Audio/SoundWaveImporter.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"

#include "Engine/EngineUtilities.h"
#include "Engine/Log.h"
#include "Importers/Constructor/Graph/SoundGraph.h"
#include "Modules/Cloud/Cloud.h"
#include "Settings/Redirector.h"
#include "Utilities/JsonHelpers.h"

#if UE4_25_BELOW
#include "Sound/SoundWave.h"
#endif

namespace {
	bool DownloadAudioFile(const FString& FetchPath, FString& OutSavePath) {
		const TSharedPtr<FJsonObject> Response = Cloud::Export::GetRawBlocking(FetchPath, {
			{
				"save",
				"true"
			}
		});

		if (Response == nullptr) {
			return false;
		}

		return Response->TryGetStringField(TEXT("file"), OutSavePath) && !OutSavePath.IsEmpty();
	}

	USoundWave* ImportAudioFile(const FString& SavePath, const FString& ObjectPath) {
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

		UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
		ImportData->Filenames.Add(SavePath);
		ImportData->DestinationPath = FPaths::GetPath(ObjectPath);
		ImportData->bReplaceExisting = true;

		const TArray<UObject*> AssetsImported = AssetTools.ImportAssetsAutomated(ImportData);

		if (!AssetsImported.IsValidIndex(0)) {
			return LoadObjectByPath<USoundWave>(ObjectPath);
		}

		return Cast<USoundWave>(AssetsImported[0]);
	}
}

bool ISoundWaveImporter::Import() {
	ISoundGraph::NotifySoundGroupsUnconfigured();

	FString FetchPath = GetPackage()->GetPathName();
	FRRedirects::Reverse(FetchPath);

	FString SavePath;

	if (!DownloadAudioFile(FetchPath, SavePath)) {
		FImportIssues::Report(EImportIssue::MissingAsset, TEXT("Cloud wrote no audio file for this wave"), FetchPath);

		return false;
	}

	if (!FPaths::FileExists(SavePath)) {
		FImportIssues::Report(EImportIssue::MissingAsset, TEXT("Couldn't download a sound wave"), SavePath);

		return false;
	}

	FString ObjectPath = FetchPath + TEXT(".") + GetAssetName();
	FRRedirects::Redirect(ObjectPath);

	USoundWave* SoundWave = ImportAudioFile(SavePath, ObjectPath);

	if (SoundWave == nullptr) {
		FImportIssues::Report(EImportIssue::Failed, TEXT("The audio factory built no wave from the file"), SavePath);

		return false;
	}

	SoundWave->AssetImportData = nullptr;

	GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(GetAssetData(), TArray<FString> {
		"NumChannels",
		"SampleRate",
		"ImportedSampleRate",
		"Duration",
		"TotalSamples",
		"RawPCMDataSize",
		"CompressedDataGuid",

		"CuePoints"
	}), SoundWave);

	SoundWave->PostEditChange();

	CreateAsset(SoundWave);
	SetPackage(SoundWave->GetOutermost());

	return OnAssetCreation(SoundWave);
}
