#pragma once

#include "CoreMinimal.h"

struct FReflectionVersioning {
	FReflectionVersioning() = default;

	FReflectionVersioning(const int Version, const int LatestVersion, const FString& InHTMLUrl, const FString& VersionName, const FString& CurrentVersionName)
		: Version(Version)
		, VersionName(VersionName)
		, CurrentVersionName(CurrentVersionName)
		, HTMLUrl(InHTMLUrl)
		, LatestVersion(LatestVersion)
	{
	}

	void SetValid(const bool Valid);
	void Reset(const int InVersion, const int InLatestVersion, const FString& InHTMLUrl, const FString& InVersionName, const FString& InCurrentVersionName);
	void Update();

	bool IsNewVersionAvailable() const;
	bool IsFutureVersion() const;
	bool IsLatestVersion() const;

	int Version = 0;

	FString VersionName = "";
	FString CurrentVersionName = "";
	FString HTMLUrl = "";

	bool IsValid = false;

	int LatestVersion = 0;
};

extern FReflectionVersioning GReflectionVersioning;
