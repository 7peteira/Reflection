#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SSearchBox;

struct FSupportedAssetEntry {
	FString Type;

	FString DisplayType;

	FString Category;

	FString Importer;

	bool Experimental = false;

	bool CloudBlocked = false;
};

namespace SupportedAssetColumns {
	const FName Type(TEXT("Type"));
	const FName Category(TEXT("Category"));
	const FName Importer(TEXT("Importer"));
	const FName Notes(TEXT("Notes"));
}

class REFLECTION_API SSupportedAssetsPanel : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SSupportedAssetsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void Refresh();

	static TArray<TSharedPtr<FSupportedAssetEntry>> Collect();

private:
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FSupportedAssetEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable);

	void OnSearchChanged(const FText& Text);
	void ApplyFilter();

	FText GetSummaryText() const;

	TArray<TSharedPtr<FSupportedAssetEntry>> AllEntries;
	TArray<TSharedPtr<FSupportedAssetEntry>> VisibleEntries;

	TSharedPtr<SListView<TSharedPtr<FSupportedAssetEntry>>> ListView;
	FString SearchText;
};
