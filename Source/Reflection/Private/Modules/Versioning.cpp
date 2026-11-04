#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Versioning.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Engine/Log.h"
#include "Utilities/Process.h"
#include "Modules/Metadata.h"
#include "Modules/UI/StyleModule.h"
#include "Engine/EngineUtilities.h"
#include "Utilities/JsonHelpers.h"

FReflectionVersioning GReflectionVersioning;

void FReflectionVersioning::SetValid(const bool Valid) {
	IsValid = Valid;
}

void FReflectionVersioning::Reset(const int InVersion, const int InLatestVersion, const FString& InHTMLUrl, const FString& InVersionName, const FString& InCurrentVersionName) {
	Version = InVersion;
	LatestVersion = InLatestVersion;
	VersionName = InVersionName;
	CurrentVersionName = InCurrentVersionName;
	HTMLUrl = InHTMLUrl;

	SetValid(true);
}

inline int32 ConvertVersionStringToInt(const FString& VersionStr) {
	return FCString::Atoi(*VersionStr.Replace(TEXT("."), TEXT("")));
}

void FReflectionVersioning::Update() {
	if (IsRunningCommandlet() || IsRunningDedicatedServer()) {
		return;
	}

	SetValid(false);

	FHttpModule* HttpModule = &FHttpModule::Get();

	const auto Request = HttpModule->CreateRequest();

	Request->SetURL(TEXT("https://api.github.com/repos/Reflection/Reflection/releases/latest"));
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("User-Agent"), GReflectionName.ToString());

	Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr Req, const FHttpResponsePtr& Resp, const bool bSuccess) {
		if (!bSuccess || !Resp.IsValid()) {
			UE_LOG(LogReflection, Warning, TEXT("HTTP request failed or no internet connection."));

			return;
		}

		const FString ResponseString = Resp->GetContentAsString();

		TSharedPtr<FJsonObject> JsonObject;
		if (!DeserializeJSONObject(ResponseString, JsonObject)) return;

		if (!JsonObject->HasField(TEXT("name"))) {
			return;
		}

		Reset(ConvertVersionStringToInt(FRMetadata::Version), ConvertVersionStringToInt(JsonObject->GetStringField(TEXT("name"))), JsonObject->GetStringField(TEXT("html_url")), JsonObject->GetStringField(TEXT("name")), FRMetadata::Version);

		static bool IsNotificationShown = false;

		if (IsNewVersionAvailable() && !IsNotificationShown) {
			const FString CapturedUrl = HTMLUrl;

			FNotificationInfo Info(FText::FromString(VersionName + " is now available!"));

			SetNotificationSubText(Info, FText::FromString(
				"Get the latest features and improvements in the new version."
			));

			Info.HyperlinkText = FText::FromString("GitHub Release");
			Info.Hyperlink = FSimpleDelegate::CreateLambda([CapturedUrl]() {
				LaunchURL(CapturedUrl);
			});

			Info.bFireAndForget = true;
			Info.FadeOutDuration = 0.1f;
			Info.ExpireDuration = 13.5f;
			Info.bUseLargeFont = false;
			Info.bUseThrobber = false;
			Info.Image = FReflectionStyle::Get().GetBrush("Toolbar.Icon");

			FSlateNotificationManager::Get().AddNotification(Info);
			IsNotificationShown = true;
		}
	});

	Request->ProcessRequest();
}

bool FReflectionVersioning::IsNewVersionAvailable() const {
	return LatestVersion > Version;
}

bool FReflectionVersioning::IsFutureVersion() const {
	return Version > LatestVersion;
}

bool FReflectionVersioning::IsLatestVersion() const {
	return !(IsNewVersionAvailable() || IsFutureVersion());
}
