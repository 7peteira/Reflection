#include "Engine/SharedPtrCompatibility.h"
#include "Modules/UI/SupportedAssets/SupportedAssetsTab.h"

#include "Modules/UI/SupportedAssets/SSupportedAssetsPanel.h"
#include "Modules/UI/StyleModule.h"
#include "Engine/Compatibility.h"

#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "Reflection.SupportedAssets"

const FName FSupportedAssetsTab::TabId(TEXT("ReflectionSupportedAssets"));

TWeakPtr<SSupportedAssetsPanel> FSupportedAssetsTab::ActivePanel;

void FSupportedAssetsTab::Register() {
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId, FOnSpawnTab::CreateStatic(&FSupportedAssetsTab::Spawn))
		.SetDisplayName(LOCTEXT("TabTitle", "Supported Assets"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Every asset type Reflection can build."))
		.SetIcon(FSlateIcon(FReflectionStyle::GetStyleSetName(), "Toolbar.Icon"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FSupportedAssetsTab::Unregister() {
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);

	ActivePanel.Reset();
}

void FSupportedAssetsTab::Open() {
#if UE4_25_BELOW
	FGlobalTabmanager::Get()->InvokeTab(TabId);
#else
	FGlobalTabmanager::Get()->TryInvokeTab(TabId);
#endif

	if (const TSharedPtr<SSupportedAssetsPanel> Panel = ActivePanel.Pin()) {
		Panel->Refresh();
	}
}

TSharedRef<SDockTab> FSupportedAssetsTab::Spawn(const FSpawnTabArgs& Args) {
	const TSharedRef<SSupportedAssetsPanel> Panel = SNew(SSupportedAssetsPanel);
	ActivePanel = Panel;

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			Panel
		];
}

#undef LOCTEXT_NAMESPACE
