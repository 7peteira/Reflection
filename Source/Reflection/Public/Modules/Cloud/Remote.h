#pragma once

#include "Engine/Compatibility.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#if ENGINE_UE5
using FReflectionHttpRequest = TSharedRef<IHttpRequest>;
using FReflectionHttpResponse = TSharedPtr<IHttpResponse>;
#elif UE4_25_BELOW
using FReflectionHttpRequest = TSharedRef<IHttpRequest>;
using FReflectionHttpResponse = TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>;
#else
using FReflectionHttpRequest = TSharedRef<IHttpRequest, ESPMode::ThreadSafe>;
using FReflectionHttpResponse = TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>;
#endif

class REFLECTION_API FBlockingRequestScope {
public:
	explicit FBlockingRequestScope(const FText& Description, float Total = 0.0f, float Done = 0.0f);
	~FBlockingRequestScope();

	FBlockingRequestScope(const FBlockingRequestScope&) = delete;
	FBlockingRequestScope& operator=(const FBlockingRequestScope&) = delete;

	static bool IsActive();

	static bool Pump();

private:
	bool bTracked = false;
	bool bOwnsProgress = false;
};

class REFLECTION_API FRemoteUtilities {
public:
	static void ExecuteRequestAsync(FReflectionHttpRequest HttpRequest, TFunction<void(FReflectionHttpResponse)> OnComplete);

	static FReflectionHttpResponse ExecuteRequestBlocking(FReflectionHttpRequest HttpRequest, float TimeoutSeconds = 30.0f);
};
