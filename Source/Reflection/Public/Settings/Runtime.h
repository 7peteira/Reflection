#pragma once

#include "CoreMinimal.h"

struct FRCloudProfile {
	FString Name;

	FString ProjectName;
};

struct FRRuntime {
	int MinorVersion = -1;

	int MajorVersion = -1;

	FRCloudProfile Profile;
	FDirectoryPath ExportDirectory;

	bool bEnableToolbarToggling;

	bool IsOlderUE4Target() const;
	bool IsUE5() const;
	bool IsUE4() const;
	bool IsUE5OrNewer() const;

	void Update();
};

extern FRRuntime GReflectionRuntime;
