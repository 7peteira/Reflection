#pragma once

#include "Importers/Constructor/ImportIssues.h"

#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "Utilities/Dialog.h"
#include "Engine/Compatibility.h"

#if UE4_25_BELOW
#include "AssetRegistryModule.h"
#else
#include "AssetRegistry/AssetRegistryModule.h"
#endif
#include "Engine/Log.h"
#include "Framework/Application/SlateApplication.h"

#if UE4_25_BELOW
#include "Framework/Docking/TabManager.h"

inline bool HasLiveContentBrowser() {
	for (int32 BrowserIndex = 1; BrowserIndex <= 4; ++BrowserIndex) {
		const FTabId TabId = FTabId(*FString::Printf(TEXT("ContentBrowserTab%d"), BrowserIndex));

		if (FGlobalTabmanager::Get()->FindExistingLiveTab(TabId).IsValid()) {
			return true;
		}
	}

	return false;
}
#endif

inline void BrowseToAsset(UObject* Asset) {
	if (Asset == nullptr) {
		return;
	}

	if (IsRunningCommandlet() || !FSlateApplication::IsInitialized()) {
		return;
	}

#if UE4_25_BELOW
	if (!HasLiveContentBrowser()) {
		UE_LOG(LogReflection, Verbose, TEXT("No Content Browser open, skipping the jump to \"%s\""), *Asset->GetName());

		return;
	}
#endif

	const TArray<FAssetData>& Assets = { Asset };
	const FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	ContentBrowserModule.Get().SyncBrowserToAssets(Assets,   true);
}

template <typename T>
T* GetSelectedAsset(const bool SuppressErrors = false, FString OptionalAssetNameCheck = "") {
	const FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	if (SelectedAssets.Num() == 0) {
		if (SuppressErrors == false) {
			FImportIssues::Report(
				EImportIssue::Setting,
				TEXT("Nothing selected in the Content Browser"),
				FString::Printf(TEXT("This reflects onto a %s that is already in the project, so one has to be selected."), *T::StaticClass()->GetName())
			);
		}

		return nullptr;
	}

	UObject* SelectedAsset = SelectedAssets[0].GetAsset();
	T* CastedAsset = Cast<T>(SelectedAsset);

	if (!CastedAsset) {
		if (SuppressErrors == false) {
			FImportIssues::Report(
				EImportIssue::Setting,
				TEXT("The selected asset is the wrong type"),
				FString::Printf(TEXT("\"%s\" is a %s, and this reflects onto a %s."), *SelectedAsset->GetName(), *SelectedAsset->GetClass()->GetName(), *T::StaticClass()->GetName())
			);
		}

		return nullptr;
	}

	if (OptionalAssetNameCheck != "" && !CastedAsset->GetName().Equals(OptionalAssetNameCheck)) {
		if (SuppressErrors == false) {
			FImportIssues::Report(
				EImportIssue::Setting,
				TEXT("The selected asset is not the one being reflected"),
				FString::Printf(TEXT("\"%s\" is selected, and this reflects onto \"%s\"."), *CastedAsset->GetName(), *OptionalAssetNameCheck)
			);
		}

		return nullptr;
	}

	return CastedAsset;
}

inline FString GetSelectedContentBrowserFolder() {
	const FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	TArray<FString> SelectedFolders;
	ContentBrowserModule.Get().GetSelectedFolders(SelectedFolders);

	if (SelectedFolders.Num() == 0) {
		ContentBrowserModule.Get().GetSelectedPathViewFolders(SelectedFolders);
	}

	if (SelectedFolders.Num() == 0) {
		return FString();
	}

#if ENGINE_UE5
	const UContentBrowserDataSubsystem* ContentBrowserData = GEditor->GetEditorSubsystem<UContentBrowserDataSubsystem>();

	if (!ContentBrowserData) {
		return FString();
	}

	const TArray<FString> InternalPaths = ContentBrowserData->TryConvertVirtualPathsToInternal(SelectedFolders);

	return InternalPaths.Num() > 0 ? InternalPaths[0] : FString();
#else
	return SelectedFolders[0];
#endif
}

inline TArray<FAssetData> GetAssetsInSelectedFolder() {
	TArray<FAssetData> AssetDataList;

	const FString CurrentFolder = GetSelectedContentBrowserFolder();

	if (CurrentFolder.IsEmpty()) {
		UE_LOG(LogReflection, Warning, TEXT("No folder selected in the Content Browser."));
		return AssetDataList;
	}

	if (CurrentFolder == "/All/Game") {
		bool Continue = false;

		SpawnYesNoPrompt(
			TEXT("Large Operation"),
			TEXT("This will stall the editor for a long time. Continue anyway?"),
			[&](const bool Confirmed) {
				Continue = Confirmed;
			}
		);

		if (!Continue) {
			UE_LOG(LogReflection, Warning, TEXT("Action cancelled by user."));
			return AssetDataList;
		}
	}

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().SearchAllAssets(true);

	AssetRegistryModule.Get().GetAssetsByPath(FName(*CurrentFolder), AssetDataList, true);

	return AssetDataList;
}
