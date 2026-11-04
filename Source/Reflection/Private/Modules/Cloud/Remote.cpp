#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Cloud/Remote.h"

#include "HttpManager.h"
#include "HttpModule.h"
#include "Misc/ScopedSlowTask.h"
#include "Engine/Log.h"

namespace {
	constexpr float BlockingPollInterval = 0.01f;

	constexpr float BlockingProgressDelay = 0.4f;

	int32 GBlockingScopeDepth = 0;

	TUniquePtr<FScopedSlowTask> GBlockingProgress;

	TArray<FText> GDescriptionStack;

	bool GPumping = false;
}

FBlockingRequestScope::FBlockingRequestScope(const FText& Description, const float Total, const float Done) {
	bTracked = IsInGameThread();

	if (!bTracked) {
		return;
	}

	GDescriptionStack.Push(Description);

	bOwnsProgress = GBlockingScopeDepth++ == 0;

	if (bOwnsProgress) {
		GBlockingProgress = MakeUnique<FScopedSlowTask>(Total, Description);
		GBlockingProgress->MakeDialogDelayed(BlockingProgressDelay, true);

		if (Total > 0.0f && Done > 0.0f) {
			GBlockingProgress->EnterProgressFrame(FMath::Min(Done, Total), Description);
		}
	}
}

FBlockingRequestScope::~FBlockingRequestScope() {
	if (!bTracked) {
		return;
	}

	GBlockingScopeDepth--;
	GDescriptionStack.Pop();

	if (bOwnsProgress) {
		GBlockingProgress.Reset();
	}
}

bool FBlockingRequestScope::IsActive() {
	return GBlockingScopeDepth > 0;
}

bool FBlockingRequestScope::Pump() {
	if (!GBlockingProgress.IsValid() || !IsInGameThread() || GPumping) {
		return false;
	}

	const FText CurrentDescription = GDescriptionStack.Num() > 0 ? GDescriptionStack.Top() : FText::GetEmpty();

	GPumping = true;

	GBlockingProgress->EnterProgressFrame(0.0f, CurrentDescription);

	GPumping = false;

	return GBlockingProgress.IsValid() && GBlockingProgress->ShouldCancel();
}

void FRemoteUtilities::ExecuteRequestAsync(FReflectionHttpRequest HttpRequest, TFunction<void(FReflectionHttpResponse)> OnComplete) {
	const TSharedRef<bool, ESPMode::ThreadSafe> Reported = MakeShared<bool, ESPMode::ThreadSafe>(false);

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[OnComplete, Reported](FHttpRequestPtr Request, FHttpResponsePtr Response, const bool bSuccess) {
			if (*Reported) {
				return;
			}

			*Reported = true;

			if (!bSuccess || !Response.IsValid()) {
				const FString RequestURL = Request.IsValid() ? Request->GetURL() : TEXT("<unknown>");

				UE_LOG(LogReflection, Warning, TEXT("HTTP request failed: \"%s\""), *RequestURL);
				OnComplete(nullptr);

				return;
			}

			OnComplete(Response);
		}
	);

	if (!HttpRequest->ProcessRequest() && !*Reported) {
		*Reported = true;

		UE_LOG(LogReflection, Error, TEXT("Failed to start HTTP request: \"%s\""), *HttpRequest->GetURL());
		OnComplete(nullptr);
	}
}

FReflectionHttpResponse FRemoteUtilities::ExecuteRequestBlocking(FReflectionHttpRequest HttpRequest, const float TimeoutSeconds) {
	const FString RequestURL = HttpRequest->GetURL();

	if (!HttpRequest->ProcessRequest()) {
		UE_LOG(LogReflection, Error, TEXT("Failed to start HTTP request: \"%s\""), *RequestURL);

		return nullptr;
	}

	double LastTime = FPlatformTime::Seconds();
	const double Deadline = LastTime + TimeoutSeconds;

	while (EHttpRequestStatus::Processing == HttpRequest->GetStatus()) {
		const double CurrentTime = FPlatformTime::Seconds();

		FHttpModule::Get().GetHttpManager().Tick(static_cast<float>(CurrentTime - LastTime));
		LastTime = CurrentTime;

		if (FBlockingRequestScope::Pump()) {
			UE_LOG(LogReflection, Log, TEXT("HTTP request cancelled: \"%s\""), *RequestURL);
			HttpRequest->CancelRequest();

			return nullptr;
		}

		if (CurrentTime >= Deadline) {
			UE_LOG(LogReflection, Error, TEXT("HTTP request timed out after %.0f seconds: \"%s\""), TimeoutSeconds, *RequestURL);
			HttpRequest->CancelRequest();

			return nullptr;
		}

		FPlatformProcess::Sleep(BlockingPollInterval);
	}

	return HttpRequest->GetResponse();
}
