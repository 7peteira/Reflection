#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/ImportIssues.h"

#include "Engine/Log.h"
#include "Engine/Package.h"
#include "Modules/UI/Import/SImportIssuesPanel.h"
#include "Settings/Redirector.h"

#define LOCTEXT_NAMESPACE "Reflection.ImportIssues"

namespace {
	TArray<TSharedPtr<FImportIssueAsset>> GAssets;

	TArray<TSharedPtr<FImportIssueAsset>> GStack;

	struct FPendingReference {
		FString AssetName;
		FString AssetPath;
		FString AssetType;

		FString Type;
		FString Name;
		FString Path;

		FString Within;
	};

	TArray<FPendingReference> GPending;

	const FString UnattributedName = TEXT("Import");

	bool ResolvesAt(const FString& Path) {
		if (LoadObjectByPath<UObject>(Path) != nullptr) {
			return true;
		}

		FString Redirected = Path;
		FRRedirects::Redirect(Redirected);

		return Redirected != Path && LoadObjectByPath<UObject>(Redirected) != nullptr;
	}

	bool ReferenceResolves(const FPendingReference& Pending) {
		if (!Pending.Within.IsEmpty() && ResolvesAt(Pending.Path + TEXT(".") + Pending.Within)) {
			return true;
		}

		if (ResolvesAt(Pending.Path + TEXT(".") + Pending.Name)) {
			return true;
		}

		if (Pending.Name.Contains(TEXT(":"))) {
			return false;
		}

		FString Folder;

		if (!Pending.Path.Split(TEXT("/"), &Folder, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd)) {
			return false;
		}

		return ResolvesAt(Folder + TEXT("/") + Pending.Name + TEXT(".") + Pending.Name);
	}

	TSharedPtr<FImportIssueAsset> FindOrAddAsset(const FString& Name, const FString& Path, const FString& Type) {
		for (const TSharedPtr<FImportIssueAsset>& Asset : GAssets) {
			if (Asset->Name == Name && Asset->Path == Path) {
				return Asset;
			}
		}

		const TSharedPtr<FImportIssueAsset> Asset = MakeShared<FImportIssueAsset>();

		Asset->Name = Name;
		Asset->Path = Path;
		Asset->Type = Type;

		GAssets.Add(Asset);

		return Asset;
	}
}

FText GetImportIssueText(const EImportIssue Kind) {
	switch (Kind) {
		case EImportIssue::MissingAsset: return LOCTEXT("MissingAsset", "Missing Asset");
		case EImportIssue::MissingClass: return LOCTEXT("MissingClass", "Missing Class");
		case EImportIssue::Setting: return LOCTEXT("Setting", "Setting");
		case EImportIssue::Data: return LOCTEXT("Data", "Data");
		default: return LOCTEXT("Failed", "Failed");
	}
}

FLinearColor GetImportIssueColor(const EImportIssue Kind) {
	switch (Kind) {
		case EImportIssue::MissingAsset: return FLinearColor(1.0f, 0.55f, 0.15f);
		case EImportIssue::MissingClass: return FLinearColor(1.0f, 0.75f, 0.2f);
		case EImportIssue::Setting: return FLinearColor(0.45f, 0.7f, 1.0f);
		case EImportIssue::Data: return FLinearColor(0.8f, 0.8f, 0.8f);
		default: return FLinearColor(1.0f, 0.35f, 0.35f);
	}
}

void FImportIssues::Begin() {
	Clear();
}

void FImportIssues::Finish() {
	GStack.Reset();

	for (const FPendingReference& Pending : GPending) {
		if (ReferenceResolves(Pending)) {
			continue;
		}

		const bool bNative = Pending.Path.StartsWith(TEXT("/Script/"));

		ReportFor(
			Pending.AssetName,
			Pending.AssetPath,
			Pending.AssetType,
			bNative ? EImportIssue::MissingClass : EImportIssue::MissingAsset,
			bNative
				? FString::Printf(TEXT("This engine has no %s"), *Pending.Name)
				: TEXT("Couldn't resolve ") + Pending.Name,
			bNative
				? FString::Printf(TEXT("%s is compiled into %s rather than stored as an asset, so nothing can fetch it."), *Pending.Name, *Pending.Path)
				: Pending.Type + TEXT(" at ") + Pending.Path
		);
	}

	GPending.Reset();

	if (GAssets.Num() == 0) {
		return;
	}

	UE_LOG(LogReflection, Warning, TEXT("Reflection finished with %d issue(s) across %d asset(s)."), NumIssues(), GAssets.Num());

	SImportIssuesPanel::Open();
}

void FImportIssues::Push(const FString& Name, const FString& Path, const FString& Type) {
	const TSharedPtr<FImportIssueAsset> Asset = MakeShared<FImportIssueAsset>();

	Asset->Name = Name;
	Asset->Path = Path;
	Asset->Type = Type;

	GStack.Push(Asset);
}

void FImportIssues::Pop() {
	if (GStack.Num() > 0) {
		GStack.Pop();
	}
}

void FImportIssues::Report(const EImportIssue Kind, const FString& Summary, const FString& Detail) {
	const TSharedPtr<FImportIssueAsset> Current = GStack.Num() > 0 ? GStack.Last() : nullptr;

	if (Current.IsValid()) {
		ReportFor(Current->Name, Current->Path, Current->Type, Kind, Summary, Detail);

		return;
	}

	ReportFor(UnattributedName, FString(), FString(), Kind, Summary, Detail);
}

void FImportIssues::ReportFor(const FString& Name, const FString& Path, const FString& Type, const EImportIssue Kind, const FString& Summary, const FString& Detail) {
	const TSharedPtr<FImportIssueAsset> Asset = FindOrAddAsset(Name, Path, Type);

	for (const TSharedPtr<FImportIssue>& Existing : Asset->Issues) {
		if (Existing->Kind == Kind && Existing->Summary == Summary && Existing->Detail == Detail) {
			Existing->Count++;

			return;
		}
	}

	const TSharedPtr<FImportIssue> Issue = MakeShared<FImportIssue>();

	Issue->Kind = Kind;
	Issue->Summary = Summary;
	Issue->Detail = Detail;

	Asset->Issues.Add(Issue);
}

void FImportIssues::ReportUnresolvedReference(const FString& Type, const FString& Name, const FString& Path, const FString& Within) {
	if (Name.IsEmpty()) {
		return;
	}

	if (Name.Contains(TEXT(":"))) {
		return;
	}

	if (Path.StartsWith(TEXT("/Engine/")) || Path.StartsWith(TEXT("Engine/"))) {
		return;
	}

	const TSharedPtr<FImportIssueAsset> Current = GStack.Num() > 0 ? GStack.Last() : nullptr;

	if (Current.IsValid() && !Path.IsEmpty()) {
		if (Path == Current->Path) {
			return;
		}

		if (!Current->Name.IsEmpty() && Path.EndsWith(TEXT("/") + Current->Name)) {
			return;
		}
	}

	FPendingReference Pending;

	Pending.AssetName = Current.IsValid() ? Current->Name : UnattributedName;
	Pending.AssetPath = Current.IsValid() ? Current->Path : FString();
	Pending.AssetType = Current.IsValid() ? Current->Type : FString();

	Pending.Type = Type;
	Pending.Name = Name;
	Pending.Path = Path;
	Pending.Within = Within;

	GPending.Add(Pending);
}

const TArray<TSharedPtr<FImportIssueAsset>>& FImportIssues::GetAssets() {
	return GAssets;
}

int32 FImportIssues::NumIssues() {
	int32 Count = 0;

	for (const TSharedPtr<FImportIssueAsset>& Asset : GAssets) {
		for (const TSharedPtr<FImportIssue>& Issue : Asset->Issues) {
			Count += Issue->Count;
		}
	}

	return Count;
}

void FImportIssues::Clear() {
	GAssets.Reset();
	GStack.Reset();
	GPending.Reset();
}

#undef LOCTEXT_NAMESPACE
