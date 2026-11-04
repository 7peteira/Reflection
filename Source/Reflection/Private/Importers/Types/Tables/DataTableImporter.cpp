#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Tables/DataTableImporter.h"

#if UE4_25_BELOW
#include "Engine/DataTable.h"
#endif

UObject* IDataTableImporter::CreateAsset(UObject* CreatedAsset) {
	return IImporter::CreateAsset(NewObject<UDataTable>(GetPackage(), UDataTable::StaticClass(), *GetAssetName(), RF_Public | RF_Standalone));
}

bool IDataTableImporter::Import() {
	UDataTable* DataTable = Create<UDataTable>();

	FString TableStruct; {
		GetAssetData()->GetObjectField(TEXT("RowStruct"))
			->GetStringField(TEXT("ObjectName")).
			Split("'", nullptr, &TableStruct);
		TableStruct.Split("'", &TableStruct, nullptr);
	}

#if UE5_1_BEYOND
	UScriptStruct* TableRowStruct = FindFirstObject<UScriptStruct>(*TableStruct); {
#else
	UScriptStruct* TableRowStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *TableStruct); {
#endif

		if (TableRowStruct == nullptr) {
			FImportIssues::Report(
				EImportIssue::MissingClass,
				TEXT("Missing row struct ") + TableStruct,
				TEXT("The table's struct definition could not be found. Ensure the correct struct is defined and referenced by this table.")
			);

			return false;
		}

		DataTable->RowStruct = TableRowStruct;
	}

	const UPropertySerializer* ObjectPropertySerializer = GetObjectSerializer()->GetPropertySerializer();
	const TSharedPtr<FJsonObject> RowData = GetAssetData()->GetObjectField(TEXT("Rows"));

	for (const auto& Pair : RowData->Values) {
		const TSharedPtr<FStructOnScope> ScopedStruct = MakeShareable(new FStructOnScope(TableRowStruct));
		TSharedPtr<FJsonObject> StructData = Pair.Value->AsObject();

		ObjectPropertySerializer->DeserializeStruct(TableRowStruct, StructData.ToSharedRef(), ScopedStruct->GetStructMemory());
		DataTable->AddRow(*Pair.Key, *reinterpret_cast<const FTableRowBase*>(ScopedStruct->GetStructMemory()));
	}

	GetObjectSerializer()->SpawnExport(AssetExport, true);

	return OnAssetCreation(DataTable);
}
