#pragma once

#include "CoreMinimal.h"
#include "Engine/Compatibility.h"

#if ENGINE_UE5

enum class EValidationStatus : uint8 {
	Pending,

	Valid,

	Misplaced,

	Missing
};

REFLECTION_API FText GetValidationStatusText(EValidationStatus Status);
REFLECTION_API FLinearColor GetValidationStatusColor(EValidationStatus Status);

struct REFLECTION_API FValidationEntry {
	FString PackagePath;

	FString AssetName;

	FString ClassName;

	FString ResolvedPath;

	TArray<FString> Suggestions;

	EValidationStatus Status = EValidationStatus::Pending;

	const FString& GetSuggestedPath() const;

	FString GetSuggestedFolder() const;
};

class REFLECTION_API FValidation : public TSharedFromThis<FValidation> {
public:
	static constexpr int32 BatchSize = 400;

	static constexpr int32 MaxRequestsInFlight = 4;

	void Start(const FString& RootPath);

	void Cancel();

	bool IsRunning() const { return bRunning; }

	float GetProgress() const;

	int32 GetCheckedCount() const { return CheckedCount; }
	int32 GetCount(EValidationStatus Status) const;

	const TArray<TSharedPtr<FValidationEntry>>& GetEntries() const { return Entries; }

	const FString& GetFailureReason() const { return FailureReason; }

	FSimpleMulticastDelegate OnUpdated;

private:
	void GatherAssets(const FString& RootPath);
	void DispatchBatches();
	void HandleBatchResponse(int32 BatchStart, int32 BatchEnd, const TSharedPtr<FJsonObject>& Response, int32 ResponseCode);
	void Finish(const FString& Reason);

	TArray<TSharedPtr<FValidationEntry>> Entries;

	TMap<FString, TSharedPtr<FValidationEntry>> EntryLookup;

	int32 NextBatchStart = 0;

	int32 CheckedCount = 0;
	int32 RequestsInFlight = 0;

	uint32 RunId = 0;

	bool bRunning = false;
	FString FailureReason;
};

#endif
