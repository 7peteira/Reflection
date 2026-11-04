#include "Engine/SharedPtrCompatibility.h"
#include "Modules/UI/Reflect/SReflectPathsDialog.h"

#include "Engine/Compatibility.h"

#include "Containers/Export.h"
#include "Utilities/Dialog.h"

#include "Interfaces/IMainFrameModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Settings/SettingsAccess.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "Reflection.ReflectPaths"

#if ENGINE_UE5
static const FName RemoveButtonStyleName("SimpleButton");
static const FName RemoveIconName("Icons.X");
#else
static const FName RemoveButtonStyleName("HoverHintOnly");
static const FName RemoveIconName("Symbols.X");
#endif

static FSlateColor GetRemoveIconColor() {
#if ENGINE_UE5
	return FSlateColor::UseForeground();
#else
	return FAppStyle::Get().GetSlateColor("DefaultForeground");
#endif
}

static bool IsPath(const FString& Line) {
	return IsAssetPathLike(Line);
}

static bool IsFolderPath(const FString& Line) {
	FString Path = StripObjectOuter(Line.TrimStartAndEnd());
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));

	return Path.EndsWith(TEXT("/"));
}

static bool IsPathList(const FString& Text) {
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);

	int32 Paths = 0;

	for (const FString& Line : Lines) {
		if (Line.TrimStartAndEnd().IsEmpty()) continue;

		if (!IsPath(Line)) {
			return false;
		}

		Paths++;
	}

	return Paths > 0;
}

EReflectPathsChoice SReflectPathsDialog::Open(TArray<FString>& OutPaths, FString& OutFolder) {
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("Title", "Reflection"))
		.ClientSize(FVector2D(720.0f, 480.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	TSharedPtr<SReflectPathsDialog> Dialog;

	Window->SetContent(
		SAssignNew(Dialog, SReflectPathsDialog)
		.ParentWindow(Window)
	);

	const IMainFrameModule& MainFrameModule = IMainFrameModule::Get();
	FSlateApplication::Get().AddModalWindow(Window, MainFrameModule.GetParentWindow());

	if (Dialog->FolderRequested) {
		OutFolder = Dialog->PathBox->GetText().ToString().TrimStartAndEnd();

		return EReflectPathsChoice::Folder;
	}

	if (!Dialog->Accepted) {
		return EReflectPathsChoice::Cancelled;
	}

	OutPaths.Reset();

	for (const TSharedPtr<FString>& Row : Dialog->Rows) {
		if (Row.IsValid()) {
			OutPaths.Add(*Row);
		}
	}

	return OutPaths.Num() > 0 ? EReflectPathsChoice::Reflect : EReflectPathsChoice::Cancelled;
}

void SReflectPathsDialog::Construct(const FArguments& InArgs) {
	ParentWindow = InArgs._ParentWindow;

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(8.0f, 6.0f))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(PathBox, SEditableTextBox)
					.HintText(LOCTEXT("PathHint", "/Game/Asset"))
					.ToolTipText(LOCTEXT("PathTooltip", "An asset path, in either form. A copied reference or several paths at once are fine."))
					.OnTextCommitted(this, &SReflectPathsDialog::OnPathCommitted)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(SButton)
					.Text(LOCTEXT("Add", "Add"))
					.ToolTipText(LOCTEXT("AddTooltip", "Queue this path. Several pasted at once are queued one per line."))
					.IsEnabled(this, &SReflectPathsDialog::CanAdd)
					.OnClicked(this, &SReflectPathsDialog::OnAddClicked)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(8.0f, 6.0f, 8.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(this, &SReflectPathsDialog::GetStatusText)
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(FMargin(8.0f, 6.0f))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(2.0f))
			[
				SAssignNew(ListView, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&Rows)
				.SelectionMode(ESelectionMode::None)
				.OnGenerateRow(this, &SReflectPathsDialog::GenerateRow)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(8.0f, 0.0f, 8.0f, 8.0f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("Folder", "Folder"))
				.ToolTipText(LOCTEXT("FolderTooltip", "Reflect a folder of the game files instead. This closes, and the folder window takes over."))
				.OnClicked(this, &SReflectPathsDialog::OnFolderClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(12.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("ReferencesTooltip", "Fetch what an asset names as well as the asset. Off, only the paths queued here are built, and everything they refer to is left to whatever the project already has."))
				.IsChecked_Lambda([] {
					return GetSettings()->AssetSettings.ImportReferences ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([](const ECheckBoxState NewState) {
					GetSettings()->AssetSettings.ImportReferences = NewState == ECheckBoxState::Checked;
					GetSettings()->SaveConfig();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("References", "References"))
					.Margin(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SButton)
				.Text(this, &SReflectPathsDialog::GetReflectText)
				.IsEnabled(this, &SReflectPathsDialog::CanReflect)
				.OnClicked(this, &SReflectPathsDialog::OnReflectClicked)
			]
		]
	];

	const FString Clipboard = GetClipboard();

	if (IsAssetPathLike(Clipboard) && IsFolderPath(Clipboard)) {
		PathBox->SetText(FText::FromString(Clipboard.TrimStartAndEnd()));
	} else if (IsPathList(Clipboard)) {
		AddPaths(Clipboard);
	}

	FSlateApplication::Get().SetKeyboardFocus(PathBox);
}

void SReflectPathsDialog::OnPathCommitted(const FText& NewText, const ETextCommit::Type CommitType) {
	if (CommitType == ETextCommit::OnEnter) {
		AddFromBox();
	}
}

FReply SReflectPathsDialog::OnAddClicked() {
	AddFromBox();

	return FReply::Handled();
}

FReply SReflectPathsDialog::OnFolderClicked() {
	FolderRequested = true;

	if (ParentWindow.IsValid()) {
		ParentWindow->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SReflectPathsDialog::OnReflectClicked() {
	Accepted = true;

	if (ParentWindow.IsValid()) {
		ParentWindow->RequestDestroyWindow();
	}

	return FReply::Handled();
}

void SReflectPathsDialog::AddFromBox() {
	AddPaths(PathBox->GetText().ToString());

	PathBox->SetText(FText::GetEmpty());

	FSlateApplication::Get().SetKeyboardFocus(PathBox);
}

void SReflectPathsDialog::AddPaths(const FString& Text) {
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);

	for (const FString& Line : Lines) {
		const FString Path = Line.TrimStartAndEnd();
		if (Path.IsEmpty()) continue;

		const bool Queued = Rows.ContainsByPredicate([&Path](const TSharedPtr<FString>& Row) {
			return Row.IsValid() && Row->Equals(Path, ESearchCase::IgnoreCase);
		});

		if (Queued) continue;

		Rows.Add(MakeShared<FString>(Path));
	}

	if (ListView.IsValid()) {
		ListView->RequestListRefresh();
	}
}

FReply SReflectPathsDialog::RemovePath(TSharedPtr<FString> Path) {
	Rows.Remove(Path);

	if (ListView.IsValid()) {
		ListView->RequestListRefresh();
	}

	return FReply::Handled();
}

bool SReflectPathsDialog::CanAdd() const {
	return PathBox.IsValid() && !PathBox->GetText().ToString().TrimStartAndEnd().IsEmpty();
}

bool SReflectPathsDialog::CanReflect() const {
	return Rows.Num() > 0;
}

FText SReflectPathsDialog::GetStatusText() const {
	if (Rows.Num() == 0) {
		return LOCTEXT("StatusEmpty", "Nothing queued. Paths can be added one at a time, or pasted several at once.");
	}

	return FText::Format(LOCTEXT("StatusQueuedFmt", "{0} queued"), FText::AsNumber(Rows.Num()));
}

FText SReflectPathsDialog::GetReflectText() const {
	if (Rows.Num() == 0) {
		return LOCTEXT("Reflect", "Reflect");
	}

	return FText::Format(LOCTEXT("ReflectCountFmt", "Reflect {0}"), FText::AsNumber(Rows.Num()));
}

TSharedRef<ITableRow> SReflectPathsDialog::GenerateRow(TSharedPtr<FString> Path, const TSharedRef<STableViewBase>& OwnerTable) {
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.0f, 2.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Path.IsValid() ? *Path : FString()))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(4.0f, 2.0f, 4.0f, 2.0f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), RemoveButtonStyleName)
				.ContentPadding(FMargin(2.0f))
				.ToolTipText(LOCTEXT("RemoveTooltip", "Take this path back out of the queue"))
				.OnClicked(this, &SReflectPathsDialog::RemovePath, Path)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush(RemoveIconName))
					.ColorAndOpacity(GetRemoveIconColor())
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
