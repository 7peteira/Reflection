#pragma once

#include "Importers/Constructor/Types.h"
#include "Engine/EngineUtilities.h"
#include "Settings/ReflectionSettings.h"

class IImporter;

FORCEINLINE uint32 GetTypeHash(const TArray<FString>& Array) {
	uint32 Hash = 0;

	for (const FString& Str : Array) {
		Hash = HashCombine(Hash, GetTypeHash(Str));
	}

	return Hash;
}

using FImporterFactoryDelegate = TFunction<IImporter*()>;

struct FImporterRegistrationInfo {
	FString Category;
	FImporterFactoryDelegate Factory;

	FImporterRegistrationInfo(const FString& InCategory, const FImporterFactoryDelegate& InFactory)
		: Category(InCategory)
		, Factory(InFactory)
	{
	}

	FImporterRegistrationInfo() = default;
};

inline TMap<TArray<FString>, FImporterRegistrationInfo>& GetFactoryRegistry() {
	static TMap<TArray<FString>, FImporterRegistrationInfo> Registry;

	return Registry;
}

inline TMap<TArray<FString>, FImporterRegistrationInfo>& GetFactoryOverrides() {
	static TMap<TArray<FString>, FImporterRegistrationInfo> Overrides;

	return Overrides;
}

inline FImporterFactoryDelegate* FindFactoryForAssetType(const FString& AssetType) {
	const UReflectionSettings* Settings = GetSettings();

	for (auto& Pair : GetFactoryOverrides()) {
		if (Pair.Key.Contains(AssetType)) {
			return &Pair.Value.Factory;
		}
	}

	for (auto& Pair : GetFactoryRegistry()) {
		if (!Settings->EnableExperiments) {
			if (ImportTypes::Experimental.Contains(AssetType)) return nullptr;
		}

		if (Pair.Key.Contains(AssetType)) {
			return &Pair.Value.Factory;
		}
	}

	return nullptr;
}

template <typename T>
IImporter* CreateImporter() {
	return new T();
}
