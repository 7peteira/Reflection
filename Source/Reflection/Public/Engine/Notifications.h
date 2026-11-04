#pragma once

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "Editor.h"
#include "TimerManager.h"
#include "Modules/Cloud/Remote.h"

namespace NotificationBudget {
	constexpr int32 MaxLive = 6;

	inline TArray<TWeakPtr<SNotificationItem>> Live;

	inline int32 Pending = 0;

	inline int32 Suppressed = 0;

	inline bool AtCap() {
		Live.RemoveAll([](const TWeakPtr<SNotificationItem>& Item) {
			return !Item.IsValid();
		});

		return Live.Num() + Pending >= MaxLive;
	}

	inline void Track(const TSharedPtr<SNotificationItem>& Item) {
		if (Item.IsValid()) {
			Live.Add(Item);
		}
	}
}

inline void SetNotificationSubText(FNotificationInfo& Notification, const FText& SubText) {
#if ENGINE_UE5
	Notification.SubText = SubText;
#endif
}

inline TSharedPtr<SNotificationItem> AddNotificationWhenSafe(const FNotificationInfo& Info, const SNotificationItem::ECompletionState CompletionState) {
	if (FBlockingRequestScope::IsActive() && GEditor != nullptr) {
		NotificationBudget::Pending++;

		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda([Info, CompletionState] {
			NotificationBudget::Pending--;

			if (const TSharedPtr<SNotificationItem> DeferredItem = FSlateNotificationManager::Get().AddNotification(Info)) {
				DeferredItem->SetCompletionState(CompletionState);

				NotificationBudget::Track(DeferredItem);
			}
		}));

		return nullptr;
	}

	const TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);

	if (NotificationPtr.IsValid()) {
		NotificationPtr->SetCompletionState(CompletionState);

		NotificationBudget::Track(NotificationPtr);
	}

	return NotificationPtr;
}

inline void SetNotificationSubTextWithSuppressed(FNotificationInfo& Info, const FText& SubText) {
	if (NotificationBudget::Suppressed == 0) {
		SetNotificationSubText(Info, SubText);

		return;
	}

	const FText Note = FText::Format(
		NSLOCTEXT("Reflection", "NotificationsHidden", "{0} ({1} more hidden)"),
		SubText,
		FText::AsNumber(NotificationBudget::Suppressed)
	);

	NotificationBudget::Suppressed = 0;

	SetNotificationSubText(Info, Note);
}

inline auto AppendNotification(const FText& Text, const FText& SubText, const float ExpireDuration,
                               const SNotificationItem::ECompletionState CompletionState, const bool UseSuccessFailIcons,
                               const float WidthOverride) -> void
{
	if (NotificationBudget::AtCap()) {
		NotificationBudget::Suppressed++;

		return;
	}

	FNotificationInfo Info = FNotificationInfo(Text);
	Info.ExpireDuration = ExpireDuration;
	Info.bUseLargeFont = true;
	Info.bUseSuccessFailIcons = UseSuccessFailIcons;
	Info.WidthOverride = FOptionalSize(WidthOverride);

	SetNotificationSubTextWithSuppressed(Info, SubText);

	AddNotificationWhenSafe(Info, CompletionState);
}

inline auto AppendNotification(const FText& Text, const FText& SubText, float ExpireDuration,
                               const FSlateBrush* SlateBrush, SNotificationItem::ECompletionState CompletionState,
                               const bool UseSuccessFailIcons, const float WidthOverride) -> void
{
	if (NotificationBudget::AtCap()) {
		NotificationBudget::Suppressed++;

		return;
	}

	FNotificationInfo Info = FNotificationInfo(Text);
	Info.ExpireDuration = ExpireDuration;
	Info.bUseLargeFont = true;
	Info.bUseSuccessFailIcons = UseSuccessFailIcons;
	Info.WidthOverride = FOptionalSize(WidthOverride);
	Info.Image = SlateBrush;

	SetNotificationSubTextWithSuppressed(Info, SubText);

	AddNotificationWhenSafe(Info, CompletionState);
}

inline TSharedPtr<SNotificationItem> AppendNotificationWithHandler(const FText& Text, const FText& SubText, const float ExpireDuration,
	const FSlateBrush* SlateBrush, const SNotificationItem::ECompletionState CompletionState, const bool UseSuccessFailIcons,
	const float WidthOverride, const TFunction<void(FNotificationInfo&)>& PreAddHandler = nullptr)
{
	FNotificationInfo Info(Text);
	Info.ExpireDuration = ExpireDuration;
	Info.bUseLargeFont = true;
	Info.bUseSuccessFailIcons = UseSuccessFailIcons;

	if (WidthOverride != 0.0f) {
		Info.WidthOverride = FOptionalSize(WidthOverride);
	}

	Info.Image = SlateBrush;

	SetNotificationSubText(Info, SubText);

	if (PreAddHandler) {
		PreAddHandler(Info);
	}

	return AddNotificationWhenSafe(Info, CompletionState);
}

inline void RemoveNotification(TWeakPtr<SNotificationItem>& Notification) {
	const TSharedPtr<SNotificationItem> Item = Notification.Pin();

	if (Item.IsValid()) {
		Item->SetFadeOutDuration(0.001f);
		Item->Fadeout();
	}

	Notification.Reset();
}
