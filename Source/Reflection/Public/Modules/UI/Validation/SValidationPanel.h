#pragma once

#include "CoreMinimal.h"
#include "Modules/Validation/Validation.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Engine/Compatibility.h"

#if ENGINE_UE5

class SSearchBox;
class SEditableTextBox;

namespace ValidationColumns {
	const FName Status(TEXT("Status"));
	const FName Asset(TEXT("Asset"));
	const FName Type(TEXT("Type"));
	const FName PackagePath(TEXT("PackagePath"));
	const FName Location(TEXT("Location"));
}

class REFLECTION_API SValidationPanel : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SValidationPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SValidationPanel() override;

	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;

	void SetRootPath(const FString& RootPath);

	void StartValidation();

private:
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildFilterBar();
	TSharedRef<SWidget> BuildStatusFilter(EValidationStatus Status, const FText& Label);
	TSharedRef<SWidget> BuildFooter();

	FReply OnRunClicked();
	FReply OnCancelClicked();
	FReply OnCopyReportClicked();

	FReply OnUseCurrentFolderClicked();

	bool CanRun() const;
	bool IsRunning() const;

	bool HasSelectedFolder() const;

	FText GetUseCurrentFolderTooltip() const;

	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FValidationEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedPtr<SWidget> OpenRowContextMenu();
	void OnRowDoubleClicked(TSharedPtr<FValidationEntry> Entry);
	void OnSelectionChanged(TSharedPtr<FValidationEntry> Entry, ESelectInfo::Type SelectInfo);

	EColumnSortMode::Type GetSortModeForColumn(FName ColumnId) const;
	void OnColumnSorted(EColumnSortPriority::Type Priority, const FName& ColumnId, EColumnSortMode::Type NewSortMode);

	void RefreshList();

	void InvalidateList();

	void OnValidationUpdated();

	bool PassesFilters(const TSharedPtr<FValidationEntry>& Entry) const;

	TSharedRef<SWidget> BuildDetails() const;
	static FText GetResolvedPathText(const TSharedPtr<FValidationEntry>& Entry);

	static void BrowseToEntry(TSharedPtr<FValidationEntry> Entry);
	static void CopyToClipboard(FString Text);
	static void MoveEntryTo(TSharedPtr<FValidationEntry> Entry, FString Destination);

	FText GetSummaryText() const;
	FText GetProgressText() const;
	TOptional<float> GetProgress() const;
	EVisibility GetProgressVisibility() const;
	EVisibility GetFailureVisibility() const;
	FText GetFailureText() const;

	TSharedPtr<FValidation> Validation;
	FDelegateHandle ValidationUpdatedHandle;

	TArray<TSharedPtr<FValidationEntry>> VisibleEntries;

	TSharedPtr<SListView<TSharedPtr<FValidationEntry>>> ListView;
	TSharedPtr<SEditableTextBox> RootPathBox;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SBox> DetailsContainer;

	FString SearchText;

	TMap<EValidationStatus, bool> StatusFilters = {
		{ EValidationStatus::Misplaced, true },
		{ EValidationStatus::Missing, true }
	};

	FName SortColumn = ValidationColumns::PackagePath;
	EColumnSortMode::Type SortMode = EColumnSortMode::Ascending;

	static constexpr double RefreshInterval = 0.25;

	bool bListDirty = false;
	double LastRefreshTime = 0.0;
};

#endif
