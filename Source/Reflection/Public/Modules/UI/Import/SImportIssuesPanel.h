#pragma once

#include "CoreMinimal.h"
#include "Importers/Constructor/ImportIssues.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

struct FImportIssueRow {
	TSharedPtr<FImportIssueAsset> Asset;
	TSharedPtr<FImportIssue> Issue;

	TArray<TSharedPtr<FImportIssueRow>> Children;

	bool IsAsset() const {
		return !Issue.IsValid();
	}
};

class REFLECTION_API SImportIssuesPanel : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SImportIssuesPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static void Open();

	void Refresh();

private:
	TSharedRef<SWidget> BuildHeader();

	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FImportIssueRow> Row, const TSharedRef<STableViewBase>& OwnerTable);
	void GetRowChildren(TSharedPtr<FImportIssueRow> Row, TArray<TSharedPtr<FImportIssueRow>>& OutChildren);
	void OnRowDoubleClicked(TSharedPtr<FImportIssueRow> Row);

	FReply OnExpandAllClicked();
	FReply OnCollapseAllClicked();
	FReply OnCopyReportClicked();

	FText GetSummaryText() const;

	FString BuildReport() const;

	TSharedPtr<STreeView<TSharedPtr<FImportIssueRow>>> TreeView;

	TArray<TSharedPtr<FImportIssueRow>> Rows;
};
