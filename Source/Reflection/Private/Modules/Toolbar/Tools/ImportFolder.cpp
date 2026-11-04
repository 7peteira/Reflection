#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Toolbar/Tools/ImportFolder.h"

#include "Importers/Constructor/ImportIssues.h"
#include "Modules/Cloud/Cloud.h"
#include "Modules/Cloud/Remote.h"
#include "Modules/Toolbar/Tools/ImportFromPath.h"
#include "Modules/UI/Reflect/SReflectFolderDialog.h"
#include "Engine/EngineUtilities.h"
#include "Utilities/Dialog.h"

bool TToolImportFolder::Execute(const FString& InitialFolder, const bool bUseClipboard, const bool bCanGoBack) {
	if (!Cloud::Status::IsOpened()) {
		SpawnPrompt("Reflect Folder", "Cloud isn't running, so there is nowhere to fetch from.");

		return false;
	}

	if (!Cloud::EnsureMetadataBlocking()) {
		return false;
	}

	TArray<FString> Paths;
	TSet<FString> AllowedTypes;

	const EReflectFolderChoice Choice = SReflectFolderDialog::Open(Paths, AllowedTypes, InitialFolder, bUseClipboard, bCanGoBack);

	if (Choice == EReflectFolderChoice::Back) {
		return true;
	}

	if (Choice != EReflectFolderChoice::Reflect) {
		return false;
	}

	int32 Reflected = 0;
	int32 Attempted = 0;
	int32 Filtered = 0;
	bool Cancelled = false;

	FImportIssues::Begin(); {
		const FBlockingRequestScope BlockingScope(NSLOCTEXT("Reflection", "ReflectingFolder", "Reflecting folder"));

		for (const FString& Path : Paths) {
			bool WasFiltered = false;

			if (TToolImportFromPath::Import(Path, AllowedTypes, &WasFiltered)) {
				Reflected++;
			}

			if (WasFiltered) {
				Filtered++;
			} else {
				Attempted++;
			}

			if (FBlockingRequestScope::Pump()) {
				Cancelled = true;

				break;
			}
		}
	}

	const bool Successful = !Cancelled && Reflected == Attempted;

	AppendNotification(
		FText::FromString(Cancelled ? "Reflect Folder Cancelled" : (Successful ? "Reflected Folder" : "Reflected With Failures")),
		FText::FromString(Filtered > 0
			? FString::Printf(TEXT("%d of %d, %d filtered out"), Reflected, Attempted, Filtered)
			: FString::Printf(TEXT("%d of %d"), Reflected, Paths.Num())),
		Successful ? 2.0f : 5.0f,
		Successful ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail,
		true,
		310.0f
	);

	FImportIssues::Finish();

	return false;
}
