#include "Engine/SharedPtrCompatibility.h"
#include "SchemaDumpCommandlet.h"

#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"

DECLARE_LOG_CATEGORY_CLASS(LogSchemaDump, All, All);

namespace {
	TSharedPtr<FJsonObject> Describe(const FProperty* Property) {
		TSharedPtr<FJsonObject> Described = MakeShared<FJsonObject>();

		if (Property == nullptr) return Described;

		Described->SetStringField(TEXT("Type"), Property->GetClass()->GetName());

		if (const FStructProperty* AsStruct = CastField<FStructProperty>(Property)) {
			if (AsStruct->Struct != nullptr) {
				Described->SetStringField(TEXT("StructType"), AsStruct->Struct->GetName());
			}
		} else if (const FByteProperty* AsByte = CastField<FByteProperty>(Property)) {
			if (AsByte->Enum != nullptr) {
				Described->SetStringField(TEXT("EnumName"), AsByte->Enum->GetName());
				Described->SetBoolField(TEXT("IsEnumAsByte"), true);
			}
		} else if (const FEnumProperty* AsEnum = CastField<FEnumProperty>(Property)) {
			if (AsEnum->GetEnum() != nullptr) {
				Described->SetStringField(TEXT("EnumName"), AsEnum->GetEnum()->GetName());
			}

			Described->SetObjectField(TEXT("InnerType"), Describe(AsEnum->GetUnderlyingProperty()));
		} else if (const FArrayProperty* AsArray = CastField<FArrayProperty>(Property)) {
			Described->SetObjectField(TEXT("InnerType"), Describe(AsArray->Inner));
		} else if (const FSetProperty* AsSet = CastField<FSetProperty>(Property)) {
			Described->SetObjectField(TEXT("InnerType"), Describe(AsSet->ElementProp));
		} else if (const FMapProperty* AsMap = CastField<FMapProperty>(Property)) {
			Described->SetObjectField(TEXT("InnerType"), Describe(AsMap->KeyProp));
			Described->SetObjectField(TEXT("ValueType"), Describe(AsMap->ValueProp));
		}

		return Described;
	}

	TArray<TSharedPtr<FJsonValue>> OwnProperties(const UStruct* Struct, int32& OutSlots) {
		TArray<TSharedPtr<FJsonValue>> Listed;

		OutSlots = 0;

		for (const FProperty* Property = Struct->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext) {
			if (Property->GetOwnerStruct() != Struct) break;

			TSharedPtr<FJsonObject> Entry = Describe(Property);

			Entry->SetStringField(TEXT("Name"), Property->GetName());

			if (Property->ArrayDim > 1) {
				Entry->SetNumberField(TEXT("ArraySize"), Property->ArrayDim);
			}

			OutSlots += Property->ArrayDim;

			Listed.Add(MakeShared<FJsonValueObject>(Entry));
		}

		return Listed;
	}
}

int32 USchemaDumpCommandlet::Main(const FString& Params) {
	FString Out;

	if (!FParse::Value(*Params, TEXT("out="), Out) || Out.IsEmpty()) {
		UE_LOG(LogSchemaDump, Error, TEXT("nowhere to write it: give it -out=<file>"));

		return 1;
	}

	const TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	int32 Classes = 0;
	int32 Structs = 0;

	for (TObjectIterator<UStruct> It; It; ++It) {
		UStruct* Struct = *It;

		if (Struct == nullptr || (!Struct->IsA<UClass>() && !Struct->IsA<UScriptStruct>())) continue;

		int32 Slots = 0;

		TArray<TSharedPtr<FJsonValue>> Listed = OwnProperties(Struct, Slots);

		if (Listed.Num() == 0) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		if (const UStruct* Super = Struct->GetSuperStruct()) {
			Entry->SetStringField(TEXT("Super"), Super->GetName());
		}

		Entry->SetNumberField(TEXT("Slots"), Slots);
		Entry->SetArrayField(TEXT("Properties"), Listed);

		Root->SetObjectField(Struct->GetName(), Entry);

		Struct->IsA<UClass>() ? Classes++ : Structs++;
	}

	FString Written;

	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Written);

	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer) || !FFileHelper::SaveStringToFile(Written, *Out)) {
		UE_LOG(LogSchemaDump, Error, TEXT("could not write \"%s\""), *Out);

		return 1;
	}

	UE_LOG(LogSchemaDump, Display, TEXT("wrote %d class(es) and %d struct(s) to \"%s\" (%d KB)"),
		Classes, Structs, *Out, Written.Len() / 1024);

	return 0;
}
