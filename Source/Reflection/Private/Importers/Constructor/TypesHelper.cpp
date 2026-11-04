#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/TypesHelper.h"

#include "Importers/Constructor/Types.h"
#include "Importers/Constructor/Registry/RegistrationInfo.h"
#include "Engine/Compatibility.h"

bool ClassIsFormed(const UClass* Class) {
	if (Class == nullptr) return false;

	if (Class->HasAnyClassFlags(CLASS_NewerVersionExists)) return false;

	if (Class->ClassConstructor == nullptr || Class->ClassWithin == nullptr) return false;

	return Class->GetStructureSize() >= sizeof(UObject);
}

bool CanImport(const FString& Type, const bool IsCloud, const UClass* Class) {
	if (IsCloud) {
		if (!ImportTypes::Cloud::Allowed(Type)) {
			return false;
		}

		if (Type == "SoundWave") {
			return true;
		}
	}

	if (FindFactoryForAssetType(Type)) {
		return true;
	}

	for (const TPair<FString, TArray<FString>>& Pair : ImportTypes::Templated) {
		if (Pair.Value.Contains(Type)) {
			return true;
		}
	}

	if (!Class) {
		Class = FindClassByType(Type);
	}

	if (Class == nullptr) {
		return false;
	}

	if (!ImportTypes::Allowed(Type)) return false;

	if (Class->IsChildOf(UDataAsset::StaticClass())) {
		return true;
	}

	if (Class->IsChildOf(UTexture::StaticClass())) {
		return false;
	}

#if 0
	const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	const IAssetTools& AssetTools = AssetToolsModule.Get();

	static TArray<TWeakPtr<IAssetTypeActions>> OutAssetTypeActionsList = {};
	AssetTools.GetAssetTypeActionsList(OutAssetTypeActionsList);

	for (TWeakPtr AssetTypeAction : OutAssetTypeActionsList) {
		if (const TSharedPtr<IAssetTypeActions> Action = AssetTypeAction.Pin()) {
			const UClass* SupportedClass = Action->GetSupportedClass();

			if (SupportedClass == Class) return true;
		}
	}
#endif

	return false;
}
