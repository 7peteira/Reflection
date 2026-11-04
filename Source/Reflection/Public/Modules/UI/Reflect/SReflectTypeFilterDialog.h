#pragma once

#include "CoreMinimal.h"
#include "Modules/UI/SupportedAssets/SSupportedAssetsPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SSearchBox;
class SWindow;

struct FReflectTypeFilterEntry {
	TSharedPtr<FSupportedAssetEntry> Asset;

	bool Allowed = true;

	bool Available = true;

	FString Unavailable;
};

class REFLECTION_API SReflectTypeFilterDialog : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SReflectTypeFilterDialog) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)

		SLATE_ARGUMENT(TSet<FString>, AllowedTypes)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static bool Open(TSet<FString>& InOutAllowedTypes);

private:
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FReflectTypeFilterEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable);

	void OnSearchChanged(const FText& Text);
	void ApplyFilter();

	FReply OnAllClicked();
	FReply OnNoneClicked();

	FReply OnAcceptClicked();

	FText GetSummaryText() const;

	int32 CountAllowed() const;
	int32 CountAvailable() const;

	TArray<TSharedPtr<FReflectTypeFilterEntry>> AllEntries;
	TArray<TSharedPtr<FReflectTypeFilterEntry>> VisibleEntries;

	TSharedPtr<SListView<TSharedPtr<FReflectTypeFilterEntry>>> ListView;
	TSharedPtr<SWindow> ParentWindow;

	FString SearchText;

	bool Accepted = false;
};
