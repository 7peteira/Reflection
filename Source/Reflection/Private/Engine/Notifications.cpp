
/* Copyright Reflection Contributors 2024-2026 */

#include "Engine/Notifications.h"

TArray<TWeakPtr<SNotificationItem>> NotificationBudget::Live;

int32 NotificationBudget::Pending = 0;

int32 NotificationBudget::Suppressed = 0;
