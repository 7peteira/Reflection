#pragma once

struct ImportTypes {
	static TMap<FString, TArray<FString>> Templated;

	struct Cloud {
		static inline TArray<FString> Blacklisted = { };

		static bool Allowed(const FString& Type);
	};

	static inline TArray<FString> Experimental = {
		"AnimBlueprintGeneratedClass",
		"World",
		"BlueprintGeneratedClass",
		"WidgetBlueprintGeneratedClass"
	};

	static bool Allowed(const FString& ImporterType);
};
