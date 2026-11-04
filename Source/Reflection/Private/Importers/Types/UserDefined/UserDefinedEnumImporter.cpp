#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/UserDefined/UserDefinedEnumImporter.h"

#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"

UObject* IUserDefinedEnumImporter::CreateAsset(UObject* CreatedAsset) {
	return IImporter::CreateAsset(NewObject<UUserDefinedEnum>(GetPackage(), *GetAssetName(), RF_Public | RF_Standalone));
}

bool IUserDefinedEnumImporter::Import() {
	UUserDefinedEnum* UserDefinedEnum = Create<UUserDefinedEnum>();

	UserDefinedEnum->SetMetaData(TEXT("BlueprintType"), TEXT("true"));
	UserDefinedEnum->Modify();

	UEnum::ECppForm CppForm = UEnum::ECppForm::Regular;

	if (GetAssetData()->HasField(TEXT("CppForm"))) {
		const FString CppForm_String = GetAssetData()->GetStringField(TEXT("CppForm"));

		CppForm = CppForm_String == "Regular" ? UEnum::ECppForm::Regular : CppForm_String == "Namespaced" ? UEnum::ECppForm::Namespaced : UEnum::ECppForm::EnumClass;
	}

	if (GetAssetData()->HasTypedField<EJson::Object>(TEXT("Names"))) {
		const TSharedPtr<FJsonObject> Names = GetAssetData()->GetObjectField(TEXT("Names"));

		TArray<TPair<FName, int64>> EnumNames;

		int32 EntryCount = Names->Values.Num();

		for (const auto& Pair : Names->Values) {
			if (--EntryCount == 0) break;

			EnumNames.Emplace(FName(*Pair.Key), static_cast<int64>(Pair.Value->AsNumber()));
		}

		UserDefinedEnum->SetEnums(EnumNames, CppForm
	#if UE5_8_BEYOND
			, UEnum::EUnderlyingType::int32, EEnumFlags::None, UEnum::EAddMaxKeyIfMissing::Yes
	#elif ENGINE_UE5 || ((ENGINE_UE4 && ENGINE_MINOR_VERSION >= 26) && !(ENGINE_MINOR_VERSION == 26 && ENGINE_PATCH_VERSION == 0))
			, EEnumFlags::None, true
	#endif
		);
	}

	TArray<TSharedPtr<FJsonValue>> DisplayNameMap = GetAssetData()->GetArrayField(TEXT("DisplayNameMap"));
	TMap<FName, FText> DisplayNames;

	for (const TSharedPtr<FJsonValue>& DisplayEntry : DisplayNameMap) {
		if (!DisplayEntry.IsValid()) continue;

		TSharedPtr<FJsonObject> EntryObject = DisplayEntry->AsObject(); {
			if (!EntryObject.IsValid()) continue;
		}

		TSharedPtr<FJsonObject> ValueObject = EntryObject->GetObjectField(TEXT("Value")); {
			if (!ValueObject.IsValid()) continue;
		}

		FName EnumKey = *EntryObject->GetStringField(TEXT("Key"));
		FString TextNamespace = ValueObject->GetStringField(TEXT("Namespace"));
		FString UniqueKey = ValueObject->GetStringField(TEXT("Key"));
		FString SourceString = ValueObject->GetStringField(TEXT("SourceString"));

		if (ValueObject->HasField(TEXT("CultureInvariantString"))) {
			DisplayNames.Add(EnumKey, FText::FromString(*ValueObject->GetStringField(TEXT("CultureInvariantString"))));
		} else {
	#if UE5_8_BEYOND
			DisplayNames.Add(EnumKey, FText::FromString(SourceString));
	#else
			DisplayNames.Add(EnumKey, FInternationalization::ForUseOnlyByLocMacroAndGraphNodeTextLiterals_CreateText(*SourceString, *TextNamespace, *UniqueKey));
	#endif
		}
	}

	for (const auto& Pair : DisplayNames) {
		UserDefinedEnum->DisplayNameMap.Add(Pair.Key, Pair.Value);
	}

	UserDefinedEnum->Modify();
	UserDefinedEnum->PostEditChange();

	FEnumEditorUtils::EnsureAllDisplayNamesExist(UserDefinedEnum);

	return OnAssetCreation(UserDefinedEnum);
}
