
/* Copyright Reflection Contributors 2024-2026 */

#pragma once

struct ImportTypes {
	/* AssetType/Category ~ Defined in CPP */
	static TMap<FString, TArray<FString>> Templated;

	struct Cloud {
		static TArray<FString> Blacklisted;

		static bool Allowed(const FString& Type);
	};

	static TArray<FString> Experimental;

	static bool Allowed(const FString& ImporterType);
};
