#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Cloud/Tools/CurveLinearColorData.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/EngineUtilities.h"
#include "Utilities/JsonHelpers.h"

void TCurveLinearColorData::Process(UObject* Object, const TArray<TSharedPtr<FJsonValue>>& Exports) {
	UCurveLinearColor* CurveLinearColor = Cast<UCurveLinearColor>(Object);
	if (!CurveLinearColor) return;

	FUObjectExportContainer* Container = new FUObjectExportContainer(Exports);
	FUObjectExport* Export = Container->FindByType(FString("CurveLinearColor"));

	if (!Export->IsJsonValid()) return;

	Initialize(Export, Container);

	TArray<TSharedPtr<FJsonValue>> FloatCurves = GetAssetData()->GetArrayField(TEXT("FloatCurves"));

	for (int i = 0; i < FloatCurves.Num(); i++) {
		TArray<TSharedPtr<FJsonValue>> Keys = FloatCurves[i]->AsObject()->GetArrayField(TEXT("Keys"));
		CurveLinearColor->FloatCurves[i].Keys.Empty();

		for (int j = 0; j < Keys.Num(); j++) {
			CurveLinearColor->FloatCurves[i].Keys.Add(ObjectToRichCurveKey(Keys[j]->AsObject()));
		}
	}

#if UE4_25_BELOW
	HandleAssetCreation(CurveLinearColor, CurveLinearColor->GetOutermost());
#else
	HandleAssetCreation(CurveLinearColor, CurveLinearColor->GetPackage());
#endif
}
