#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Toolbar/Toolbar.h"

#include "Engine/Compatibility.h"
#include "Engine/EngineUtilities.h"
#include "Utilities/Process.h"

#include "Modules/UI/StyleModule.h"
#include "ContentBrowserModule.h"
#include "Engine/AssetCompatibility.h"
#include "Importers/Constructor/ImportIssues.h"
#include "Importers/Constructor/ImportJob.h"
#include "Importers/Constructor/ImportReader.h"
#include "Modules/Metadata.h"
#include "Modules/Cloud/Cloud.h"
#include "Modules/Toolbar/Tools/ImportFolder.h"
#include "Modules/Toolbar/Tools/ImportFromPath.h"
#if ENGINE_UE5
#include "Modules/Toolbar/Dropdowns/ValidationDropdownBuilder.h"
#endif
#include "Modules/Toolbar/Dropdowns/CloudToolsDropdownBuilder.h"
#include "Settings/Runtime.h"
#include "Modules/Toolbar/Dropdowns/GeneralDropdownBuilder.h"
#include "Modules/Toolbar/Dropdowns/DonateDropdownBuilder.h"
#include "Modules/Toolbar/Dropdowns/ParentDropdownBuilder.h"
#include "Modules/Toolbar/Dropdowns/ToolsDropdownBuilder.h"
#include "Modules/Toolbar/Dropdowns/VersioningDropdownBuilder.h"
#include "Settings/Static.h"
#include "Utilities/ContentBrowser.h"
#include "Utilities/Dialog.h"
#include "Widgets/Layout/SBox.h"

#if !UE4_23_BELOW
#include "ToolMenus.h"
#endif

#if UE4_25_BELOW
#include "Framework/MultiBox/MultiBoxBuilder.h"
#endif

static void CloudButtonPressed() {
	if (!Cloud::Status::IsOpened()) {
		LaunchURL(GitHub::README::Cloud);
	}
}

static FText GetCloudButtonLabel() {
	return Cloud::Status::IsOpened() ? FText::FromString("On") : FText::FromString("Off");
}

static FText GetCloudButtonTooltip() {
	return Cloud::Status::IsOpened()
		? FText::FromString("Cloud is running.")
		: FText::FromString("Cloud isn't running. Click to read how to set one up.");
}

static bool CanOpenCloudMenu() {
	return Cloud::Status::IsOpened();
}

#if ENGINE_UE5
static FSlateIcon GetValidationIcon() {
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Validate");
}

static FSlateIcon GetAssetToolsIcon() {
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon");
}
#endif

void UReflectionToolbar::Register() {
#if ENGINE_UE5
	static bool UseToolbar = false;

	UToolMenu* Menu;

	if (UseToolbar) {
		Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	} else {
		Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.Toolbar");
	}

	if (UseToolbar) {
		FToolMenuSection& Section = Menu->FindOrAddSection(GReflectionName);

		AddReflectionButtons(Section);
		AddCloudButtons(Section);
	} else {
		static const FName EmbeddedToolbarName("Reflection.EmbeddedToolbar");

		FReflectionStyle::EnsureEmbeddedToolbarStyleRegistered();

		UToolMenu* EmbeddedToolbar = UToolMenus::Get()->RegisterMenu(EmbeddedToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);

#if UE5_4_BEYOND
		EmbeddedToolbar->SetStyleSet(&FReflectionStyle::Get());
#endif
		EmbeddedToolbar->StyleName = FReflectionStyle::GetEmbeddedToolbarStyleName();

		FToolMenuSection& EmbeddedSection = EmbeddedToolbar->FindOrAddSection("ReflectionEmbeddedSection");

		AddReflectionButtons(EmbeddedSection);
		AddCloudButtons(EmbeddedSection);

		FToolMenuSection& Section = Menu->FindOrAddSection("New");

		Section.AddDynamicEntry("ReflectionEmbeddedToolbar", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection) {
			InSection.AddEntry(FToolMenuEntry::InitWidget(
				"ReflectionEmbeddedToolbar",
				SNew(SBox)
				.Visibility_Static([]() {
					return UReflectionToolbar::IsToolBarVisible() ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					UToolMenus::Get()->GenerateWidget(EmbeddedToolbarName, FToolMenuContext())
				],
				FText::GetEmpty(),
				true,
				false
			));
		}));
	}

	RegisterMainMenu();
#endif

#if !UE4_25_BELOW
	RegisterAssetContextMenu();
#endif
}

#if !UE4_25_BELOW
void UReflectionToolbar::RegisterAssetContextMenu() {
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus == nullptr) return;

	const auto AddReflectSection = [](UToolMenu* Menu, const FName After, const FText& Tooltip, TFunction<void()> OnReflect) {
		if (Menu == nullptr) return;

		FToolMenuSection& Section = Menu->AddSection(
			GReflectionName,
			FText::FromString(GReflectionName.ToString()),
			FToolMenuInsert(After, EToolMenuInsertType::After)
		);

		Section.AddMenuEntry(
			GReflectionName,
			FText::FromString("Reflect"),
			Tooltip,
			FSlateIcon(FReflectionStyle::Get().GetStyleSetName(), "Toolbar.Icon"),

			FUIAction(
				FExecuteAction::CreateLambda(OnReflect),

				FCanExecuteAction::CreateLambda([] {
					return Cloud::Status::IsOpened();
				})
			)
		);
	};

	AddReflectSection(
		ToolMenus->ExtendMenu("ContentBrowser.AssetContextMenu"),
		TEXT("GetAssetActions"),
		FText::FromString("Reflects the selected assets from Cloud, replacing what is in the project."),
		[] {
			const FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

			TArray<FAssetData> SelectedAssets;
			ContentBrowser.Get().GetSelectedAssets(SelectedAssets);

			if (!Cloud::EnsureMetadataBlocking()) {
				return;
			}

			FImportIssues::Begin();

			for (const FAssetData& AssetData : SelectedAssets) {
				const FString ObjectPath = GetAssetObjectPath(AssetData);
				if (ObjectPath.IsEmpty()) continue;

				TToolImportFromPath::Import(ObjectPath);
			}

			FImportIssues::Finish();
		}
	);

	AddReflectSection(
		ToolMenus->ExtendMenu("ContentBrowser.FolderContextMenu"),
		TEXT("PathViewFolderOptions"),
		FText::FromString("Opens Reflect Folder listing what the Cloud has under this folder."),
		[] {
			TToolImportFolder().Execute(GetSelectedContentBrowserFolder());
		}
	);
}
#endif

#if ENGINE_UE5
void UReflectionToolbar::RegisterMainMenu() {
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus == nullptr) {
		return;
	}

	UToolMenu* MenuBar = ToolMenus->ExtendMenu("LevelEditor.MainMenu");
	if (MenuBar == nullptr) {
		return;
	}

	FToolMenuOwnerScoped OwnerScoped(this);

	FToolMenuSection& Section = MenuBar->AddSection(
		"ReflectionValidationSection",
		FText::GetEmpty(),
		FToolMenuInsert("WindowLayout", EToolMenuInsertType::After)
	);

	Section.AddSubMenu(
		"Validation",
		FText::FromString("Validation"),
		FText::FromString("Validate this project's content against the real game files"),
		FNewToolMenuChoice(FNewMenuDelegate::CreateStatic(&UReflectionToolbar::PopulateValidationMenu)),
		false,
		GetValidationIcon()
	);

	Section.AddSubMenu(
		"AssetTools",
		FText::FromString("Asset Tools"),
		FText::FromString("Tools that work over the assets already in this project"),
		FNewToolMenuChoice(FNewMenuDelegate::CreateStatic(&UReflectionToolbar::PopulateAssetToolsMenu)),
		false,
		GetAssetToolsIcon()
	);
}

void UReflectionToolbar::PopulateValidationMenu(FMenuBuilder& MenuBuilder) {
	IValidationDropdownBuilder().Build(MenuBuilder);
}

void UReflectionToolbar::PopulateAssetToolsMenu(FMenuBuilder& MenuBuilder) {
	IToolsDropdownBuilder().Build(MenuBuilder);
}
#endif

#if !UE4_23_BELOW
void UReflectionToolbar::AddReflectionButtons(FToolMenuSection& Section) {
#if ENGINE_UE5
	FToolMenuEntry& ActionButton = Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		GReflectionName,

		FToolUIActionChoice(
			FUIAction(
				FExecuteAction::CreateUObject(this, &UReflectionToolbar::ImportAction),
				FCanExecuteAction(),
				FGetActionCheckState(),
				FIsActionButtonVisible::CreateStatic(&IsToolBarVisible)
			)
		),

		FText::FromString(""),

		FText::FromString(""),

		FSlateIcon(FReflectionStyle::Get().GetStyleSetName(), FName("Toolbar.Icon")),

		EUserInterfaceActionType::Button
	));

	ActionButton.StyleNameOverride = FReflectionStyle::GetActionButtonStyleName();

	const FToolMenuEntry MenuButton = Section.AddEntry(FToolMenuEntry::InitComboButton(
		"ReflectionMenu",
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction(),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateStatic(IsToolBarVisible)
		),
		FOnGetContent::CreateStatic(&CreateMenuDropdown),
		FText::FromString(GReflectionName.ToString()),
		FText::FromString(""),
		FSlateIcon(),
		true
	));
#endif
}

void UReflectionToolbar::AddCloudButtons(FToolMenuSection& Section) {
#if 0
	Section.AddSeparator(NAME_None);

	FToolMenuEntry& ActionButton = Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"ReflectionCloud",
		FToolUIActionChoice(
			FUIAction(
				FExecuteAction::CreateStatic(&CloudButtonPressed),
				FCanExecuteAction(),
				FGetActionCheckState(),
				FIsActionButtonVisible::CreateStatic(&IsToolBarVisible)
			)
		),
		TAttribute<FText>::Create(&GetCloudButtonLabel),
		TAttribute<FText>::Create(&GetCloudButtonTooltip),
		FSlateIcon(FReflectionStyle::Get().GetStyleSetName(), FName("Toolbar.Cloud")),
		EUserInterfaceActionType::Button
	));

	ActionButton.StyleNameOverride = "CalloutToolbar";

	const FToolMenuEntry MenuButton = Section.AddEntry(FToolMenuEntry::InitComboButton(
		"ReflectionCloudMenu",
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction::CreateStatic(&CanOpenCloudMenu),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateStatic(IsToolBarVisible)
		),
		FOnGetContent::CreateStatic(&CreateCloudMenuDropdown),
		FText::FromString(""),
		FText::FromString(""),
		FSlateIcon(),
		true
	));
#endif
}
#endif

#if ENGINE_UE4
void UReflectionToolbar::UE4Register(FToolBarBuilder& Builder) {
	Builder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateUObject(this, &UReflectionToolbar::ImportAction),
			FCanExecuteAction(),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateStatic(IsToolBarVisible)
		),
		NAME_None,
		FText::FromString(""),
		FText::FromString(""),
		FSlateIcon(FReflectionStyle::Get().GetStyleSetName(), FName("Toolbar.Icon"))
	);

	Builder.AddComboButton(
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction(),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateStatic(IsToolBarVisible)
		),
		FOnGetContent::CreateStatic(&UReflectionToolbar::CreateMenuDropdown),
		FText::FromString(""),
		FText::FromString(""),
		FSlateIcon(FReflectionStyle::Get().GetStyleSetName(), FName("Toolbar.Icon")),
		true
	);

	UE4CloudRegister(Builder);
}

void UReflectionToolbar::UE4CloudRegister(FToolBarBuilder& Builder) {
	Builder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateStatic(&CloudButtonPressed),
			FCanExecuteAction(),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateStatic(&IsToolBarVisible)
		),
		NAME_None,
		TAttribute<FText>::Create(&GetCloudButtonLabel),
		TAttribute<FText>::Create(&GetCloudButtonTooltip),
		FSlateIcon(FReflectionStyle::Get().GetStyleSetName(), FName("Toolbar.Cloud"))
	);

	Builder.AddComboButton(
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction::CreateStatic(&CanOpenCloudMenu),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateStatic(IsToolBarVisible)
		),
		FOnGetContent::CreateStatic(&CreateCloudMenuDropdown),
		FText::FromString(FRMetadata::Version),
		FText::FromString(""),
		FSlateIcon(FReflectionStyle::Get().GetStyleSetName(), FName("Toolbar.Cloud")),
		true
	);

}
#endif

IConsoleVariable* GReflectionButtonVisibility = nullptr;

bool UReflectionToolbar::IsToolBarVisible() {
	if (!GReflectionRuntime.bEnableToolbarToggling) {
		return true;
	}

	bool Visible = true;

	static bool bHasCheckedConsoleVariable = false;

	if (!bHasCheckedConsoleVariable) {
		GReflectionButtonVisibility = IConsoleManager::Get().FindConsoleVariable(TEXT("Toolbar.Tools.FlippedVisibility"));

		bHasCheckedConsoleVariable = true;
	}

	if (GReflectionButtonVisibility) {
		if (GReflectionButtonVisibility->GetInt() == 1) {
			Visible = false;
		}
	}

	if (GEditor) {
		for (const FWorldContext& WorldContext : GEditor->GetWorldContexts()) {
			if (WorldContext.World() && WorldContext.World()->WorldType == EWorldType::PIE) {
				Visible = false;
			}
		}
	}

	return Visible;
}

void UReflectionToolbar::ImportAction() {
	Import();
}

void UReflectionToolbar::Import() {
	GReflectionRuntime.Update();

	if (!FImportJob::IsRunning()) {
		FRRedirects::Clear();
	}

	if (!GetSettings()->UseLocalJson) {
		TToolImportFromPath Tool;
		Tool.Execute();

		return;
	}

	const TArray<FString> OutFileNames = OpenFileDialog("Select a JSON File", "JSON Files|*.json");
	if (OutFileNames.Num() == 0) {
		return;
	}

	FImportJob::Enqueue(OutFileNames);
}

TSharedRef<SWidget> UReflectionToolbar::CreateMenuDropdown() {
	FMenuBuilder MenuBuilder(false, nullptr);

	TArray<TSharedRef<IParentDropdownBuilder>> Dropdowns = {
		MakeShared<IVersioningDropdownBuilder>(),

#if ENGINE_UE4
		MakeShared<IToolsDropdownBuilder>(),
#endif

		MakeShared<IGeneralDropdownBuilder>(),
		MakeShared<IDonateDropdownBuilder>()
	};

	for (const TSharedRef<IParentDropdownBuilder>& Dropdown : Dropdowns) {
		Dropdown->Build(MenuBuilder);
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> UReflectionToolbar::CreateCloudMenuDropdown() {
	FMenuBuilder MenuBuilder(false, nullptr);

	TArray<TSharedRef<IParentDropdownBuilder>> Dropdowns = {
		MakeShared<ICloudToolsDropdownBuilder>()
	};

	for (const TSharedRef<IParentDropdownBuilder>& Dropdown : Dropdowns) {
		Dropdown->Build(MenuBuilder);
	}

	return MenuBuilder.MakeWidget();
}
