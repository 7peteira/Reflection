#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Animation/PreviewMesh.h"

#include "Engine/Log.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Interfaces/Interface_PreviewMeshProvider.h"

#if UE4_25_BELOW
#include "AssetRegistryModule.h"
#else
#include "AssetRegistry/AssetRegistryModule.h"
#endif

bool FPreviewMesh::FromFolder(UObject* Asset, const USkeleton* Skeleton) {
	IInterface_PreviewMeshProvider* Previews = Cast<IInterface_PreviewMeshProvider>(Asset);

	if (Asset == nullptr || Previews == nullptr) return false;

	if (Previews->GetPreviewMesh() != nullptr) return false;

	const FString Folder = FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Beside;
	Registry.GetAssetsByPath(*Folder, Beside,   false);

	const FString Rigged = Skeleton != nullptr ? Skeleton->GetPathName() : FString();

	FAssetData Picked;

	for (const FAssetData& One : Beside) {
#if UE5_1_BEYOND
		if (One.AssetClassPath != USkeletalMesh::StaticClass()->GetClassPathName()) continue;
#else
		if (One.AssetClass != USkeletalMesh::StaticClass()->GetFName()) continue;
#endif

		if (!Rigged.IsEmpty()) {
			FString Built;

			if (!One.GetTagValue(TEXT("Skeleton"), Built)) continue;
			if (FPackageName::ExportTextPathToObjectPath(Built) != Rigged) continue;
		}

		if (!Picked.IsValid() || One.AssetName.Compare(Picked.AssetName) < 0) {
			Picked = One;
		}
	}

	if (!Picked.IsValid()) return false;

	USkeletalMesh* Mesh = Cast<USkeletalMesh>(Picked.GetAsset());

	if (Mesh == nullptr) return false;

	Previews->SetPreviewMesh(Mesh);

	UE_LOG(LogReflection, Display, TEXT("\"%s\" previews on \"%s\" from its own folder"), *Asset->GetName(), *Mesh->GetName());

	return true;
}
