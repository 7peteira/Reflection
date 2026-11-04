#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Tables/CurveTableImporter.h"
#include "Dom/JsonObject.h"

class UCurveTableAccessor final : public UCurveTable {
public:
	void ChangeTableMode(const ECurveTableMode Mode) {
		CurveTableMode = Mode;
	}
};

UObject* ICurveTableImporter::CreateAsset(UObject* CreatedAsset) {
	return IImporter::CreateAsset(NewObject<UCurveTable>(GetPackage(), UCurveTable::StaticClass(), *GetAssetName(), RF_Public | RF_Standalone));
}

bool ICurveTableImporter::Import() {
	const TSharedPtr<FJsonObject> RowData = GetAssetData()->GetObjectField(TEXT("Rows"));
	UCurveTable* CurveTable = Create<UCurveTable>();

	ECurveTableMode CurveTableMode = ECurveTableMode::RichCurves; {
		if (FString CurveMode; GetAssetData()->TryGetStringField(TEXT("CurveTableMode"), CurveMode))
			CurveTableMode = static_cast<ECurveTableMode>(StaticEnum<ECurveTableMode>()->GetValueByNameString(CurveMode));

		UCurveTableAccessor* CurveTableAccessor = Cast<UCurveTableAccessor>(CurveTable);
		CurveTableAccessor->ChangeTableMode(CurveTableMode);
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RowData->Values) {
		const TSharedPtr<FJsonObject> CurveData = Pair.Value->AsObject();

		if (CurveTableMode == ECurveTableMode::RichCurves) {
			FRichCurve& Curve = CurveTable->AddRichCurve(FName(*Pair.Key));

			GetPropertySerializer()->DeserializeStruct(Curve.StaticStruct(), CurveData.ToSharedRef(), &Curve);
		} else {
			FSimpleCurve& Curve = CurveTable->AddSimpleCurve(FName(*Pair.Key));

			GetPropertySerializer()->DeserializeStruct(Curve.StaticStruct(), CurveData.ToSharedRef(), &Curve);
		}

		CurveTable->OnCurveTableChanged().Broadcast();
		CurveTable->Modify(true);
	}

	return OnAssetCreation(CurveTable);
}
