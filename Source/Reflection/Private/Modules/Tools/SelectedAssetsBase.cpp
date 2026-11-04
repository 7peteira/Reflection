#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Tools/SelectedAssetsBase.h"

#include "Modules/Cloud/Cloud.h"
#include "Modules/UI/StyleModule.h"
#include "Engine/EngineUtilities.h"

void TSelectedAssetsBase::Execute() {
	if (bRunning) {
		AppendNotification(
			FText::FromString("Already Running"),
			FText::FromString("Wait for the current Cloud run to finish."),
			3.0f,
			FReflectionStyle::Get().GetBrush("Toolbar.Icon"),
			SNotificationItem::CS_None,
			false,
			310.0f
		);

		return;
	}

	Cloud::EnsureMetadataBlocking();

	TArray<FAssetData> AssetDataList = GetAssetsInSelectedFolder();

	if (const UObject* SelectedAsset = GetSelectedAsset<UObject>(true)) {
		AssetDataList.Empty();
		AssetDataList.Add(FAssetData(SelectedAsset));
	}

	const FName SupportedClass = GetSupportedClass();

	Queue.Reset();

	for (const FAssetData& AssetData : AssetDataList) {
		if (!AssetData.IsValid()) continue;
		if (!SupportedClass.IsNone() && GetAssetDataClass(AssetData) != SupportedClass) continue;

		const FString ObjectPath = GetAssetObjectPath(AssetData);
		if (ObjectPath.IsEmpty()) continue;

		Queue.Add(ObjectPath);
	}

	if (Queue.Num() == 0) {
		return;
	}

	QueueIndex = 0;
	bRunning = true;

	RequestNext();
}

void TSelectedAssetsBase::RequestNext() {
	if (!Queue.IsValidIndex(QueueIndex)) {
		Finish();

		return;
	}

	Cloud::Export::GetRawExportsAsync(Queue[QueueIndex], [this](const TArray<TSharedPtr<FJsonValue>>& Exports) {
		const TArray<TSharedPtr<FJsonValue>> OwnedExports = Exports;

		Cloud::RunWhenSafe([this, OwnedExports]() {
			HandleExports(OwnedExports);
		});
	});
}

void TSelectedAssetsBase::HandleExports(const TArray<TSharedPtr<FJsonValue>>& Exports) {
	const FString ObjectPath = Queue.IsValidIndex(QueueIndex) ? Queue[QueueIndex] : FString();

	QueueIndex++;

	if (Exports.Num() > 0 && !ObjectPath.IsEmpty()) {
		if (UObject* Object = LoadObjectByPath<UObject>(ObjectPath)) {
			CreateSerializer();

			Process(Object, Exports);
		}
	}

	RequestNext();
}

void TSelectedAssetsBase::BrowseToWhenFinished(const UObject* Object) {
	if (Object == nullptr) return;

	PendingBrowseTo = Object->GetPathName();
}

void TSelectedAssetsBase::Finish() {
	Queue.Reset();

	QueueIndex = 0;
	bRunning = false;

	if (!PendingBrowseTo.IsEmpty()) {
		if (UObject* Object = LoadObjectByPath<UObject>(PendingBrowseTo)) {
			BrowseToAsset(Object);
		}

		PendingBrowseTo.Empty();
	}
}
