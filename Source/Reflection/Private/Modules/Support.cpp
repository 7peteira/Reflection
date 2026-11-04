#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Support.h"

#include "Engine/EngineUtilities.h"
#include "Utilities/Process.h"
#include "Engine/Notifications.h"
#include "Modules/UI/StyleModule.h"
#include "Settings/Static.h"
#include "Settings/SettingsAccess.h"
#include "Modules/Cloud/Remote.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"

static constexpr float TurnSeconds = 20.0f * 60.0f;

#if ENGINE_UE5
FTSTicker::FDelegateHandle FReflectionSupport::Handle;
#else
FDelegateHandle FReflectionSupport::Handle;
#endif

int32 FReflectionSupport::Turn = 0;

void FReflectionSupport::Register() {
	if (IsRunningCommandlet() || IsRunningDedicatedServer() || !FSlateApplication::IsInitialized()) {
		return;
	}

	if (Handle.IsValid()) return;

	if (const UReflectionSettings* Settings = GetSettings(); Settings != nullptr && !Settings->ShowSupportPrompt) {
		return;
	}

	const FTickerDelegate Delegate = FTickerDelegate::CreateStatic(&FReflectionSupport::Tick);

#if ENGINE_UE5
	Handle = FTSTicker::GetCoreTicker().AddTicker(Delegate, TurnSeconds);
#else
	Handle = FTicker::GetCoreTicker().AddTicker(Delegate, TurnSeconds);
#endif
}

void FReflectionSupport::Unregister() {
	if (!Handle.IsValid()) return;

#if ENGINE_UE5
	FTSTicker::GetCoreTicker().RemoveTicker(Handle);
#else
	FTicker::GetCoreTicker().RemoveTicker(Handle);
#endif

	Handle.Reset();
}

bool FReflectionSupport::Tick(float DeltaTime) {
	if (const UReflectionSettings* Settings = GetSettings(); Settings != nullptr && !Settings->ShowSupportPrompt) {
		Handle.Reset();

		return false;
	}

	if (Turn % 2 != 0) {
		Turn++;

		return true;
	}

	if (!IsQuiet()) return true;

	Turn++;

	Show();

	return true;
}

bool FReflectionSupport::IsQuiet() {
	if (FBlockingRequestScope::IsActive()) return false;

	#if !UE4_26
	if (NotificationBudget::AtCap() || NotificationBudget::Live.Num() > 0) return false;
	#else
	if (NotificationBudget::AtCap() || NotificationBudget::Live.Max() != 0) return false;
	#endif

	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsActive()) return false;

	return true;
}

void FReflectionSupport::Show() {
	FNotificationInfo Info(FText::FromString("Enjoying Reflection?"));

	SetNotificationSubText(Info, FText::FromString(
		"Starring the repository helps other people find it."
	));

	Info.HyperlinkText = FText::FromString("Star on GitHub");
	Info.Hyperlink = FSimpleDelegate::CreateLambda([]() {
		LaunchURL(GitHub::URL);
	});

	Info.bFireAndForget = true;
	Info.FadeOutDuration = 0.1f;
	Info.ExpireDuration = 13.5f;
	Info.bUseLargeFont = false;
	Info.bUseThrobber = false;
	Info.Image = FReflectionStyle::Get().GetBrush("Toolbar.Icon");

	AddNotificationWhenSafe(Info, SNotificationItem::CS_None);
}
