#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SEditableTextBox;
class SWindow;

enum class EReflectFolderChoice : uint8 {
	Cancelled,

	Reflect,

	Back
};

class REFLECTION_API SReflectFolderDialog : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SReflectFolderDialog)
		: _UseClipboard(false)
		, _CanGoBack(false)
		{}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ARGUMENT(FString, InitialFolder)

		SLATE_ARGUMENT(bool, UseClipboard)

		SLATE_ARGUMENT(bool, CanGoBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static EReflectFolderChoice Open(TArray<FString>& OutPaths, TSet<FString>& OutAllowedTypes, const FString& InitialFolder = FString(), bool bUseClipboard = false, bool bCanGoBack = false);

private:
	FReply OnBackClicked();
	FReply OnFindClicked();
	FReply OnTypesClicked();
	FReply OnUseSelectedFolderClicked();
	FReply OnReflectClicked();

	void OnFolderCommitted(const FText& NewText, ETextCommit::Type CommitType);

	void Find();

	bool CanFind() const;
	bool CanReflect() const;

	EVisibility GetBackVisibility() const;

	bool HasSelectedFolder() const;
	FText GetSelectedFolderTooltip() const;

	FText GetStatusText() const;
	FText GetReflectText() const;
	FText GetTypesText() const;

	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FString> Path, const TSharedRef<STableViewBase>& OwnerTable) const;

	TSharedPtr<SWindow> ParentWindow;
	TSharedPtr<SEditableTextBox> FolderBox;
	TSharedPtr<SListView<TSharedPtr<FString>>> ListView;

	TArray<TSharedPtr<FString>> Rows;
	TArray<FString> Paths;

	TSet<FString> AllowedTypes;

	bool Searched = false;

	bool CanGoBack = false;

	bool Accepted = false;
	bool BackRequested = false;
};
