#pragma once

#include "CoreMinimal.h"

enum class EImportIssue : uint8 {
	Failed,

	MissingAsset,

	MissingClass,

	Setting,

	Data
};

REFLECTION_API FText GetImportIssueText(EImportIssue Kind);
REFLECTION_API FLinearColor GetImportIssueColor(EImportIssue Kind);

struct FImportIssue {
	EImportIssue Kind = EImportIssue::Failed;

	FString Summary;
	FString Detail;

	int32 Count = 1;
};

struct FImportIssueAsset {
	FString Name;
	FString Path;
	FString Type;

	TArray<TSharedPtr<FImportIssue>> Issues;
};

class REFLECTION_API FImportIssues {
public:
	static void Begin();

	static void Finish();

	static void Push(const FString& Name, const FString& Path, const FString& Type);
	static void Pop();

	static void Report(EImportIssue Kind, const FString& Summary, const FString& Detail = FString());
	static void ReportFor(const FString& Name, const FString& Path, const FString& Type, EImportIssue Kind, const FString& Summary, const FString& Detail = FString());

	static void ReportUnresolvedReference(const FString& Type, const FString& Name, const FString& Path, const FString& Within = FString());

	template <typename TContainer, typename TPredicate>
	static int32 ReportIncomplete(const TContainer& Items, TPredicate&& IsMissing, const FString& Summary, const FString& Detail, const EImportIssue Kind = EImportIssue::Data) {
		int32 Missing = 0;
		int32 Total = 0;

		for (const auto& Item : Items) {
			Total++;

			if (IsMissing(Item)) Missing++;
		}

		if (Missing > 0) {
			Report(Kind, FString::Printf(TEXT("%d of %d %s"), Missing, Total, *Summary), Detail);
		}

		return Missing;
	}

	static const TArray<TSharedPtr<FImportIssueAsset>>& GetAssets();

	static int32 NumIssues();

	static void Clear();
};

struct REFLECTION_API FImportIssueScope {
	FImportIssueScope(const FString& Name, const FString& Path, const FString& Type) {
		FImportIssues::Push(Name, Path, Type);
	}

	~FImportIssueScope() {
		FImportIssues::Pop();
	}
};
