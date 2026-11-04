#include "Engine/SharedPtrCompatibility.h"
#include "Settings/Redirector.h"

#include "Settings/ReflectionSettings.h"
#include "Settings/Runtime.h"
#include "Engine/EngineUtilities.h"

TMap<FString, TArray<FRRedirectorPoint>> FRRedirects::History;

bool FRRedirector::IsEnabled() const {
	bool bIsEnabled = Enable;

	if (!GReflectionRuntime.Profile.Name.IsEmpty()) {
		if (Profiles.Num() > 0 && !Profiles.Contains(GReflectionRuntime.Profile.Name)) {
			bIsEnabled = false;
		}
	}

	return bIsEnabled;
}

void FRRedirects::Clear() {
	History.Empty();
}

static bool ReplaceFirst(FString& Path, const FString& From, const FString& To) {
	if (From.IsEmpty()) return false;

	const int32 At = Path.Find(From, ESearchCase::IgnoreCase, ESearchDir::FromStart);

	if (At == INDEX_NONE) return false;

	Path = Path.Left(At) + To + Path.RightChop(At + From.Len());

	return true;
}

void FRRedirects::Redirect(FString& Path) {
	TArray<FRRedirectorPoint> Points;

	const UReflectionSettings* Settings = GetSettings();

	for (const FRRedirector& Redirect : Settings->Redirectors) {
		if (!Redirect.IsEnabled()) continue;

		for (const FRRedirectorPoint& Point : Redirect.Points) {
			if (ReplaceFirst(Path, Point.From, Point.To)) {
				Points.Add(Point);
			}
		}
	}

	TArray<FRRedirectorPoint>& Pointers = History.FindOrAdd(Path);
	Pointers.Append(Points);
}

void FRRedirects::Reverse(FString& Path) {
	TArray<FRRedirectorPoint>* Points = History.Find(Path);
	if (!Points) {
		return;
	}

	for (int32 At = Points->Num() - 1; At >= 0; --At) {
		const FRRedirectorPoint& Point = (*Points)[At];

		ReplaceFirst(Path, Point.To, Point.From);
	}
}
