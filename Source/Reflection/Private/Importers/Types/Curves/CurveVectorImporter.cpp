#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Curves/CurveVectorImporter.h"
#include "Factories/CurveFactory.h"
#include "Curves/CurveVector.h"
#include "Utilities/JsonHelpers.h"

UObject* ICurveVectorImporter::CreateAsset(UObject* CreatedAsset) {
	UCurveVectorFactory* CurveVectorFactory = NewObject<UCurveVectorFactory>();
	UCurveVector* CurveVector = Cast<UCurveVector>(CurveVectorFactory->FactoryCreateNew(UCurveVector::StaticClass(), GetPackage(), *GetAssetName(), RF_Standalone | RF_Public, nullptr, GWarn));

	return IImporter::CreateAsset(CurveVector);
}

bool ICurveVectorImporter::Import() {
	TArray<TSharedPtr<FJsonValue>> FloatCurves = GetAssetData()->GetArrayField(TEXT("FloatCurves"));

	UCurveVector* CurveVectorAsset = Create<UCurveVector>();

	for (int i = 0; i < FloatCurves.Num(); i++) {
		TArray<TSharedPtr<FJsonValue>> Keys = FloatCurves[i]->AsObject()->GetArrayField(TEXT("Keys"));
		CurveVectorAsset->FloatCurves[i].Keys.Empty();

		for (int j = 0; j < Keys.Num(); j++) {
			CurveVectorAsset->FloatCurves[i].Keys.Add(ObjectToRichCurveKey(Keys[j]->AsObject()));
		}
	}

	return OnAssetCreation(CurveVectorAsset);
}
