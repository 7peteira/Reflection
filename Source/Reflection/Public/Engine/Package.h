#pragma once

#include "Engine/Compatibility.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/FontFace.h"

#include "Engine/StaticMesh.h"

#if ENGINE_UE5
#include "StaticMeshCompiler.h"

#if UE5_1_BEYOND
#include "SkinnedAssetCompiler.h"
#include "Engine/SkeletalMesh.h"
#endif
#endif

#if UE4_25_BELOW
#include "AssetRegistryModule.h"
#else
#include "AssetRegistry/AssetRegistryModule.h"
#endif
#include "VectorField/VectorFieldStatic.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/CoreRedirects.h"

#include "Utilities/ContentBrowser.h"
#include "Utilities/Dialog.h"

#if ENGINE_UE5
#include "UObject/SavePackage.h"
#endif

inline UObject* ResolveRedirector(UObject* Object) {
	for (int32 Depth = 0; Depth < 16; ++Depth) {
		const UObjectRedirector* Redirector = Cast<UObjectRedirector>(Object);

		if (Redirector == nullptr) {
			return Object;
		}

		Object = Redirector->DestinationObject;
	}

	return nullptr;
}

template <typename T>
T* LoadObjectByPath(const FString& InPath) {
	FString Path = InPath;

	if (!Path.Contains(TEXT("."))) {
		FString Named = Path;

		if (Named.Split(TEXT("/"), nullptr, &Named, ESearchCase::CaseSensitive, ESearchDir::FromEnd) && !Named.IsEmpty()) {
			Path += TEXT(".") + Named;
		}
	}

	if (UObject* Found = FindObject<UObject>(nullptr, *Path)) {
		return Cast<T>(ResolveRedirector(Found));
	}

	if (Path.StartsWith(TEXT("/Script/"))) {
		const FCoreRedirectObjectName Named(Path);

		for (const ECoreRedirectFlags Kind : { ECoreRedirectFlags::Type_Class, ECoreRedirectFlags::Type_Struct, ECoreRedirectFlags::Type_Enum }) {
			const FCoreRedirectObjectName Moved = FCoreRedirects::GetRedirectedName(Kind, Named);

			if (Moved == Named) {
				continue;
			}

			if (UObject* Found = FindObject<UObject>(nullptr, *Moved.ToString())) {
				return Cast<T>(Found);
			}
		}

		return nullptr;
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(Path);

	if (!FPackageName::IsValidLongPackageName(PackageName,  false)) {
		return nullptr;
	}

	if (!FPackageName::DoesPackageExist(PackageName)) {
		return nullptr;
	}

	return Cast<T>(ResolveRedirector(StaticLoadObject(T::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)));
}

inline void SavePackage(UPackage* Package) {
	const FString PackageName = Package->GetName();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName,
		Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());

#if ENGINE_UE5
	Package->ClearPackageFlags(PKG_ContainsNoAsset);

	FSavePackageArgs SaveArgs; {
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		SaveArgs.SaveFlags = SAVE_NoError;
	}

	UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
#else
	UPackage::SavePackage(Package, nullptr, RF_Standalone, *PackageFileName);
#endif
}

inline bool HandleAssetCreation(UObject* Asset, UPackage* Package, const bool bBrowse = true) {
	{
		const UPackage* AssetOutermostPackage = Asset->GetOutermost();
		const FString PackageName = AssetOutermostPackage->GetName();

		const FString Path = FPackageName::GetLongPackagePath(PackageName);
		if (!Path.StartsWith(TEXT("/")) || Path.Len() < 2) {
			SpawnPrompt("Failsafe", "Here's some reasons why:\n\n- You didn't export it from FModel\n- Reflected it from a random path, not in Exports/.../\n\nPlease reflect it again next time at the proper location.");

			return false;
		}
	}

	FAssetRegistryModule::AssetCreated(Asset);
	if (!Asset->MarkPackageDirty()) return false;

	Package->SetDirtyFlag(true);

	if (UVectorFieldStatic* VectorFieldStatic = Cast<UVectorFieldStatic>(Asset)) {
		VectorFieldStatic->InitResource();
	}

	if (!Asset->IsA<UFontFace>() || FSlateApplication::IsInitialized()) {
		Asset->PostEditChange();
	}

	Asset->AddToRoot();

	Package->FullyLoad();

	if (bBrowse) BrowseToAsset(Asset);

	if (UVectorFieldStatic* VectorFieldStatic = Cast<UVectorFieldStatic>(Asset)) {
		VectorFieldStatic->Resource = nullptr;
	}

	if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset)) {
#if ENGINE_UE5
		if (StaticMesh->IsCompiling()) {
			FStaticMeshCompilingManager::Get().FinishCompilation({ StaticMesh });
		}
#endif

		return true;
	}

#if UE5_1_BEYOND
	if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset)) {
		if (SkeletalMesh->IsCompiling()) {
			FSkinnedAssetCompilingManager::Get().FinishCompilation({ SkeletalMesh });
		}

		return true;
	}
#endif

	Asset->PostLoad();

	return true;
}

inline FString GetAssetPath(const UObject* Object) {
	if (!Object) {
		return FString();
	}

	if (const UPackage* Package = Object->GetOutermost()) {
		return Package->GetName();
	}

	return FString();
}

inline void MoveToTransientPackageAndRename(UObject* Object) {
	if (Object) {
		Object->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		Object->SetFlags(RF_Transient);
	}
}

inline void MoveToTransientPackagesAndRename(TArray<UObject*> Objects) {
	for (UObject* Object : Objects) {
		MoveToTransientPackageAndRename(Object);
	}
}
