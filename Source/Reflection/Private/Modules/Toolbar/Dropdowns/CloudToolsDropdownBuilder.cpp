#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Toolbar/Dropdowns/CloudToolsDropdownBuilder.h"

#include "Algo/Sort.h"

#include "Modules/Cloud/Cloud.h"
#include "Modules/Tools/ToolRegistry.h"

#include "Modules/Cloud/Tools/AnimationData.h"
#include "Modules/Cloud/Tools/ClothingData.h"
#include "Modules/Cloud/Tools/ConvexCollision.h"
#include "Modules/Cloud/Tools/CurveLinearColorData.h"
#include "Modules/Cloud/Tools/FontData.h"
#include "Modules/Cloud/Tools/MeshGeometry.h"
#include "Modules/Cloud/Tools/SkeletalMeshData.h"
#include "Modules/Cloud/Tools/WidgetAnimations.h"

void ICloudToolsDropdownBuilder::Build(FMenuBuilder& MenuBuilder) const {
	MenuBuilder.BeginSection("ReflectionCloudSection", FText::FromString("Selected Assets"));

	TArray<TSelectedAssetsBase*> Tools;

	for (const TPair<FName, FToolInstanceDelegate>& Registration : GetToolRegistry()) {
		TSelectedAssetsBase* Tool = Registration.Value();

		if (Tool == nullptr || Tool->GetDisplayName().IsEmpty()) continue;

		Tools.Add(Tool);
	}

	Algo::Sort(Tools, [](const TSelectedAssetsBase* A, const TSelectedAssetsBase* B) {
		return A->GetDisplayName().CompareTo(B->GetDisplayName()) < 0;
	});

	for (TSelectedAssetsBase* Tool : Tools) {
		MenuBuilder.AddMenuEntry(
			Tool->GetDisplayName(),
			Tool->GetTooltip(),
			Tool->GetIcon(),

			FUIAction(
				FExecuteAction::CreateLambda([Tool] {
					Tool->Execute();
				}),

				FCanExecuteAction::CreateLambda([Tool] {
					return Cloud::Status::IsOpened() && !Tool->IsRunning();
				})
			),
			NAME_None
		);
	}

	MenuBuilder.EndSection();
}
