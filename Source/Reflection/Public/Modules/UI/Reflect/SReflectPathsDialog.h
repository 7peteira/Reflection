#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SEditableTextBox;
class SWindow;

enum class EReflectPathsChoice : uint8 {
	Cancelled,

	Reflect,

	Folder
};

class REFLECTION_API SReflectPathsDialog : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SReflectPathsDialog) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static EReflectPathsChoice Open(TArray<FString>& OutPaths, FString& OutFolder);

private:
	FReply OnAddClicked();
	FReply OnFolderClicked();
	FReply OnReflectClicked();

	void OnPathCommitted(const FText& NewText, ETextCommit::Type CommitType);

	void AddFromBox();

	void AddPaths(const FString& Text);

	FReply RemovePath(TSharedPtr<FString> Path);

	bool CanAdd() const;
	bool CanReflect() const;

	FText GetStatusText() const;
	FText GetReflectText() const;

	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FString> Path, const TSharedRef<STableViewBase>& OwnerTable);

	TSharedPtr<SWindow> ParentWindow;
	TSharedPtr<SEditableTextBox> PathBox;
	TSharedPtr<SListView<TSharedPtr<FString>>> ListView;

	TArray<TSharedPtr<FString>> Rows;

	bool Accepted = false;
	bool FolderRequested = false;
};
