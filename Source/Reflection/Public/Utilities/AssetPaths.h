#pragma once

#include "CoreMinimal.h"
#include "Settings/ReflectionSettings.h"
#include "Settings/Runtime.h"

inline FString ToEditorPackagePath(const FString& InPath) {
	FString Path = InPath;

	const FString& ProjectName = GReflectionRuntime.Profile.ProjectName;

	if (!ProjectName.IsEmpty()) {
		Path = Path.Replace(*(ProjectName + "/Content/"), TEXT("/Game/"));
		Path = Path.Replace(*(ProjectName + "/Plugins"), TEXT(""));
		Path = Path.Replace(TEXT("/Content/"), TEXT("/"));
	}

	Path = Path.Replace(TEXT("Engine/Content"), TEXT("/Engine"));

	if (!Path.StartsWith(TEXT("/"))) {
		Path = "/" + Path;
	}

	FRRedirects::Redirect(Path);

	return Path;
}

inline FString ToCloudPackagePath(const FString& InPath) {
	FString Path = InPath;

	FRRedirects::Reverse(Path);

	const FString& ProjectName = GReflectionRuntime.Profile.ProjectName;

	if (!ProjectName.IsEmpty() && Path.StartsWith(TEXT("/Game/"))) {
		Path = ProjectName + TEXT("/Content/") + Path.RightChop(6);
	}

	return Path;
}
