#pragma once

#if  !PLATFORM_LINUX
#include "Windows/WindowsPlatformApplicationMisc.h"
#endif

#include "Interfaces/IMainFrameModule.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#include "Engine/Compatibility.h"

#if UE4_25_BELOW
#include "Misc/MessageDialog.h"
#endif

inline void SpawnPrompt(const FString& Title, const FString& Text) {
	FText DialogTitle = FText::FromString(Title);
	const FText DialogMessage = FText::FromString(Text);

	FMessageDialog::Open(EAppMsgType::Ok, DialogMessage);
}

inline auto SpawnYesNoPrompt = [](const FString& Title, const FString& Text, const TFunction<void(bool)>& OnResponse) {
	const FText DialogTitle = FText::FromString(Title);
	const FText DialogMessage = FText::FromString(Text);

#if UE5_3_BEYOND
	const EAppReturnType::Type Response = FMessageDialog::Open(EAppMsgType::YesNo, DialogMessage, DialogTitle);
#else
	const EAppReturnType::Type Response = FMessageDialog::Open(EAppMsgType::YesNo, DialogMessage, &DialogTitle);
#endif

	OnResponse(Response == EAppReturnType::Yes);
};

inline bool SpawnTextEntryPrompt(const FString& Title, const FString& Hint, FString& InOutText) {
	bool Accepted = false;

	TSharedPtr<SMultiLineEditableTextBox> TextBox;

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(Title))
		.ClientSize(FVector2D(640, 280))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	Window->SetContent(
		SNew(SBorder)
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Hint))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(TextBox, SMultiLineEditableTextBox)
				.Text(FText::FromString(InOutText))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString("Reflect"))
					.OnClicked_Lambda([&Accepted, &InOutText, &TextBox, Window]() {
						Accepted = true;
						InOutText = TextBox->GetText().ToString();

						Window->RequestDestroyWindow();

						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString("Cancel"))
					.OnClicked_Lambda([Window]() {
						Window->RequestDestroyWindow();

						return FReply::Handled();
					})
				]
			]
		]
	);

	const IMainFrameModule& MainFrameModule = IMainFrameModule::Get();
	FSlateApplication::Get().AddModalWindow(Window, MainFrameModule.GetParentWindow());

	return Accepted && !InOutText.IsEmpty();
}

inline FString GetClipboard() {
	FString ClipboardContent;

#if  !PLATFORM_LINUX
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
#endif

	return ClipboardContent;
}

inline TArray<FString> OpenFileDialog(const FString& Title, const FString& Type) {
	TArray<FString> ReturnValue;

	const void* ParentWindowHandle = nullptr;
	const IMainFrameModule& MainFrameModule = IMainFrameModule::Get();
	const TSharedPtr<SWindow> MainWindow = MainFrameModule.GetParentWindow();

	if (MainWindow.IsValid() && MainWindow->GetNativeWindow().IsValid()) {
		ParentWindowHandle = MainWindow->GetNativeWindow()->GetOSWindowHandle();
	}

	FString ClipboardContent = GetClipboard();
	FString DefaultPath = FString("");

	if (!ClipboardContent.IsEmpty()) {
		if (FPaths::FileExists(ClipboardContent)) {
			DefaultPath = FPaths::GetPath(ClipboardContent);
		}
		else if (FPaths::DirectoryExists(ClipboardContent)) {
			DefaultPath = ClipboardContent;
		}
	}

	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get()) {
		constexpr uint32 SelectionFlag = 1;

		DesktopPlatform->OpenFileDialog(ParentWindowHandle, Title, DefaultPath, FString(""), Type, SelectionFlag, ReturnValue);
	}

	return ReturnValue;
}

inline FString OpenFolderDialog(const FString& Title, const FString& DefaultPath) {
	FString OutFolder;
	const void* ParentWindowHandle = nullptr;

	const IMainFrameModule& MainFrameModule = IMainFrameModule::Get();
	const TSharedPtr<SWindow> MainWindow = MainFrameModule.GetParentWindow();
	if (MainWindow.IsValid() && MainWindow->GetNativeWindow().IsValid()) {
		ParentWindowHandle = MainWindow->GetNativeWindow()->GetOSWindowHandle();
	}

	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get()) {
		DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, Title, DefaultPath, OutFolder);
	}

	return OutFolder;
}

inline TArray<FString> OpenFolderDialog(const FString& Title) {
	TArray<FString> ReturnValue;

	const void* ParentWindowHandle = nullptr;

	const IMainFrameModule& MainFrameModule = IMainFrameModule::Get();
	const TSharedPtr<SWindow> MainWindow = MainFrameModule.GetParentWindow();

	if (MainWindow.IsValid() && MainWindow->GetNativeWindow().IsValid()) {
		ParentWindowHandle = MainWindow->GetNativeWindow()->GetOSWindowHandle();
	}

	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get()) {
		FString SelectedFolder;

		if (DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, Title, FString(""), SelectedFolder)) {
			ReturnValue.Add(SelectedFolder);
		}
	}

	return ReturnValue;
}
