#pragma once

#include "CoreMinimal.h"
#include "Serializers/SerializerContainer.h"

class AActor;
struct FUObjectExport;

struct FRLevelReadResult {
	FString Level;

	int32 Placed = 0;

	int32 MissingBlueprints = 0;

	int32 UnusableClasses = 0;

	int32 LeftOut = 0;

	int32 SubLevels = 0;

	int32 MissingSubLevels = 0;

	int32 HLODsTied = 0;
	int32 HLODsUntied = 0;

	int32 Failed = 0;

	int32 Missing() const {
		return MissingBlueprints + UnusableClasses + Failed;
	}
};

class REFLECTION_API FLevelRead : public USerializerContainer {
public:
	static bool Handles(const TArray<TSharedPtr<FJsonValue>>& Exports);

	static bool FromCloud(const TArray<TSharedPtr<FJsonValue>>& Exports, const FString& Path, FRLevelReadResult* OutResult = nullptr);

private:
	FRLevelReadResult Read(const TArray<TSharedPtr<FJsonValue>>& Exports, const FString& Path);

	bool Allows(const FUObjectExport* Export, bool& bOutMayFetch) const;

	UClass* ClassOf(FUObjectExport* Export, bool bMayFetch, TMap<FString, UClass*>& Known, FRLevelReadResult& Result) const;

	UWorld* LevelFor(UPackage* Package) const;

	void ReadWorldSettings(UWorld* World, FUObjectExport* Export) const;

	static void SubLevelsNamed(const TArray<TSharedPtr<FJsonValue>>& Exports, FUObjectExport* World, TArray<FString>& OutNamed);

	void ReadSubLevels(const TArray<FString>& Named, FRLevelReadResult& Result) const;

	void ReadHLODs(UWorld* World, const TArray<class ALODActor*>& Actors, FRLevelReadResult& Result) const;

	void ReadHLODSetup(UWorld* World, const TArray<class ALODActor*>& Actors) const;
};
