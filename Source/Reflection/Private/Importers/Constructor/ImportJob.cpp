#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/ImportJob.h"

#include "Editor.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

#include "Importers/Constructor/ImportIssues.h"
#include "Importers/Constructor/ImportReader.h"
#include "Engine/Log.h"
#include "Modules/UI/StyleModule.h"
#include "Engine/EngineUtilities.h"
#include "Utilities/JsonHelpers.h"
#include "Modules/Cloud/Remote.h"

namespace {
	constexpr double SliceBudgetSeconds = 0.008;

	constexpr float StepRate = 0.001f;

	struct FJobState {
		TArray<FString> Files;
		int32 FileIndex = 0;

		FString CurrentFile;
		FUObjectExportContainer* Container = nullptr;
		int32 ExportIndex = 0;

		bool bBlueprintOnly = false;

		int32 ImportedFiles = 0;

		TArray<TStrongObjectPtr<UObject>> Keep;
		TSet<UObject*> Kept;

		FTimerHandle Timer;
		TWeakPtr<SNotificationItem> Notification;
	};

	TUniquePtr<FJobState> GJob;

	bool IsPlayingInEditor() {
		if (GEditor == nullptr) {
			return false;
		}

		for (const FWorldContext& WorldContext : GEditor->GetWorldContexts()) {
			if (WorldContext.World() && WorldContext.World()->WorldType == EWorldType::PIE) {
				return true;
			}
		}

		return false;
	}

	void UpdateNotification() {
		const TSharedPtr<SNotificationItem> Item = GJob->Notification.Pin();
		if (!Item.IsValid()) {
			return;
		}

		Item->SetText(FText::FromString(FString::Printf(
			TEXT("Reflecting %d/%d: %s"),
			GJob->ImportedFiles + 1,
			GJob->Files.Num(),
			*FPaths::GetCleanFilename(GJob->CurrentFile)
		)));
	}

	void KeepContainerObjects() {
		if (GJob->Container == nullptr) {
			return;
		}

		for (const FUObjectExport* Export : GJob->Container->Exports) {
			if (Export == nullptr || Export->Object == nullptr) {
				continue;
			}

			if (GJob->Kept.Contains(Export->Object)) {
				continue;
			}

			GJob->Kept.Add(Export->Object);
			GJob->Keep.Add(TStrongObjectPtr<UObject>(Export->Object));
		}
	}

	bool OpenPendingFile() {
		if (GJob->Container != nullptr && GJob->Container->Exports.IsValidIndex(GJob->ExportIndex)) {
			return true;
		}

		if (GJob->Container != nullptr) {
			GJob->Container = nullptr;
			GJob->ImportedFiles++;
		}

		while (GJob->Files.IsValidIndex(GJob->FileIndex)) {
			FString FilePath = GJob->Files[GJob->FileIndex++];

			if (FilePath.Contains("\\")) {
				FilePath = FilePath.Replace(TEXT("\\"), TEXT("/"));
			}

			TArray<TSharedPtr<FJsonValue>> DataObjects; {
				DeserializeJSON(FilePath, DataObjects);
			}

			if (DataObjects.Num() == 0) {
				UE_LOG(LogReflection, Warning, TEXT("Nothing to reflect from \"%s\"."), *FilePath);
				GJob->ImportedFiles++;

				continue;
			}

			FUObjectExportContainer* Container = new FUObjectExportContainer(DataObjects);

			if (Container->Exports.Num() == 0) {
				UE_LOG(LogReflection, Warning, TEXT("No exports found in \"%s\"."), *FilePath);
				GJob->ImportedFiles++;

				continue;
			}

			GJob->Container = Container;
			GJob->CurrentFile = FilePath;
			GJob->ExportIndex = 0;
			GJob->bBlueprintOnly = Container->FindByType(FString("BlueprintGeneratedClass"))->IsJsonValid();

			UpdateNotification();

			return true;
		}

		return false;
	}

	bool StepJob() {
		const double SliceStart = FPlatformTime::Seconds();

		const FString Reached = FString::Printf(
			TEXT("Reflecting %d of %d"),
			FMath::Min(GJob->ImportedFiles + 1, GJob->Files.Num()),
			GJob->Files.Num()
		);

		const FString Wait = GJob->CurrentFile.IsEmpty()
			? Reached
			: Reached + TEXT(": ") + FPaths::GetCleanFilename(GJob->CurrentFile);

		const FBlockingRequestScope BlockingScope(
			FText::FromString(Wait),
			static_cast<float>(GJob->Files.Num()),
			static_cast<float>(GJob->ImportedFiles)
		);

		do {
			if (!OpenPendingFile()) {
				return false;
			}

			FUObjectExport* Export = GJob->Container->Exports[GJob->ExportIndex++];

			if (Export == nullptr) continue;
			if (GJob->bBlueprintOnly && Export->GetType() != "BlueprintGeneratedClass") continue;

			IImportReader::ReadExportAndImport(GJob->Container, Export, GJob->CurrentFile);

			KeepContainerObjects();
		} while (FPlatformTime::Seconds() - SliceStart < SliceBudgetSeconds);

		return true;
	}

	void FinishJob() {
		if (GEditor != nullptr) {
			GEditor->GetTimerManager()->ClearTimer(GJob->Timer);
		}

		RemoveNotification(GJob->Notification);

		const int32 Count = GJob->ImportedFiles;

		GJob.Reset();

		UE_LOG(LogReflection, Log, TEXT("Reflection finished: %d file(s)."), Count);

		AppendNotification(
			FText::FromString(FString::Printf(TEXT("Reflected %d file(s)"), Count)),
			FText::FromString(TEXT("")),
			4.0f,
			FReflectionStyle::Get().GetBrush("Toolbar.Icon"),
			SNotificationItem::CS_Success,
			false,
			310.0f
		);

		FImportIssues::Finish();
	}

	void TickJob() {
		if (!GJob.IsValid()) {
			return;
		}

		if (IsPlayingInEditor()) {
			if (const TSharedPtr<SNotificationItem> Item = GJob->Notification.Pin()) {
				Item->SetText(FText::FromString(TEXT("Reflection paused while in Play In Editor")));
			}

			return;
		}

		if (!StepJob()) {
			FinishJob();

			return;
		}

		UpdateNotification();
	}
}

void FImportJob::Enqueue(const TArray<FString>& Files) {
	if (Files.Num() == 0) {
		return;
	}

	if (GEditor == nullptr) {
		for (const FString& File : Files) {
			IImportReader::ImportReference(File);
		}

		return;
	}

	if (GJob.IsValid()) {
		GJob->Files.Append(Files);
		UpdateNotification();

		return;
	}

	GJob = MakeUnique<FJobState>();
	GJob->Files = Files;

	FImportIssues::Begin();

	GJob->Notification = AppendNotificationWithHandler(
		FText::FromString(TEXT("Reflecting")),
		FText::FromString(TEXT("")),
		999.0f,
		FReflectionStyle::Get().GetBrush("Toolbar.Icon"),
		SNotificationItem::CS_Pending,
		false,
		340.0f
	);

	GEditor->GetTimerManager()->SetTimer(
		GJob->Timer,
		FTimerDelegate::CreateStatic(&TickJob),
		StepRate,
		true
	);
}

bool FImportJob::IsRunning() {
	return GJob.IsValid();
}

void FImportJob::Cancel() {
	if (!GJob.IsValid()) {
		return;
	}

	GJob->Files.SetNum(GJob->FileIndex);

	if (GJob->Container != nullptr) {
		GJob->ExportIndex = GJob->Container->Exports.Num();
	}
}
