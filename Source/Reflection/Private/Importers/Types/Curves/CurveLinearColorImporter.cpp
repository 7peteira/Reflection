#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Curves/CurveLinearColorImporter.h"
#include "Curves/CurveLinearColor.h"
#include "Factories/CurveFactory.h"
#include "Utilities/JsonHelpers.h"

UObject* ICurveLinearColorImporter::CreateAsset(UObject* CreatedAsset) {
	UCurveLinearColorFactory* CurveFactory = NewObject<UCurveLinearColorFactory>();
	UCurveLinearColor* CurveLinearColor = Cast<UCurveLinearColor>(CurveFactory->FactoryCreateNew(UCurveLinearColor::StaticClass(), GetPackage(), *GetAssetName(), RF_Standalone | RF_Public, nullptr, GWarn));

	return IImporter::CreateAsset(CurveLinearColor);
}

bool ICurveLinearColorImporter::Import() {
	TArray<TSharedPtr<FJsonValue>> FloatCurves = GetAssetData()->GetArrayField(TEXT("FloatCurves"));

	UCurveLinearColor* CurveLinearColor = Create<UCurveLinearColor>();

	for (int i = 0; i < FloatCurves.Num(); i++) {
		TArray<TSharedPtr<FJsonValue>> Keys = FloatCurves[i]->AsObject()->GetArrayField(TEXT("Keys"));
		CurveLinearColor->FloatCurves[i].Keys.Empty();

		for (int j = 0; j < Keys.Num(); j++) {
			CurveLinearColor->FloatCurves[i].Keys.Add(ObjectToRichCurveKey(Keys[j]->AsObject()));
		}
	}

	return OnAssetCreation(CurveLinearColor);
}
