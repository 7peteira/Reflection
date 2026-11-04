#pragma once

#include "Settings/ReflectionSettings.h"
#include "Settings/Redirector.h"
#include "Settings/Runtime.h"
#include "Engine/EngineUtilities.h"
#include "Containers/ExportContainer.h"
#include "Importers/Constructor/Importer.h"
#include "Modules/Toolbar/Tools/ImportFromPath.h"
#include "UObject/CoreRedirects.h"

#if UE4_25_BELOW
#include "Kismet/BlueprintFunctionLibrary.h"
#endif

inline void ReadPropertyReference(const FUObjectJsonValueExport& Property, FString& Owner, FString& Member) {
	if (!Property.JsonObject.IsValid()) return;

	if (Member.IsEmpty()) {
		Property.JsonObject->TryGetStringField(TEXT("Name"), Member);
	}

	if (Member.IsEmpty()) {
		if (const TArray<TSharedPtr<FJsonValue>>* Path = nullptr; Property.JsonObject->TryGetArrayField(TEXT("Path"), Path) && Path->Num() > 0) {
			Member = (*Path)[Path->Num() - 1]->AsString();
		}
	}

	if (const TSharedPtr<FJsonObject>* Inner = nullptr; Property.JsonObject->TryGetObjectField(TEXT("Property"), Inner)) {
		if (Member.IsEmpty()) {
			(*Inner)->TryGetStringField(TEXT("Name"), Member);
		}

		const TSharedPtr<FJsonObject>* Holder = nullptr;
		FString Spelled;

		if (Owner.IsEmpty() && Property.JsonObject->TryGetObjectField(TEXT("Owner"), Holder) && (*Holder)->TryGetStringField(TEXT("ObjectName"), Spelled)) {
			if (FString Held; Spelled.Split(TEXT("'"), nullptr, &Held)) {
				Held.RemoveFromEnd(TEXT("'"));

				Owner = Held;
			}
		}
	}

	if (!Owner.IsEmpty() && !Member.IsEmpty()) return;

	FString Spelled;

	if (!Property.JsonObject->TryGetStringField(TEXT("ObjectName"), Spelled)) return;

	FString Held;

	if (!Spelled.Split(TEXT("'"), nullptr, &Held)) return;

	Held.RemoveFromEnd(TEXT("'"));

	FString Before, After;

	if (!Held.Split(TEXT(":"), &Before, &After)) return;

	if (Owner.IsEmpty()) Owner = Before;
	if (Member.IsEmpty()) Member = After;
}

inline void ReadStructMemberContext(const FUObjectJsonValueExport& Variable, FString& Owner, FString& Member) {
	Owner.Empty();
	Member.Empty();

	const FUObjectJsonValueExport Inside = Variable.GetObject(TEXT("StructExpression")).GetObject(TEXT("Variable"));

	if (Inside.JsonObject.IsValid() && !Inside.JsonObject->TryGetStringField(TEXT("Name"), Owner)) {
		if (const TSharedPtr<FJsonObject> Held = Inside.GetObject(TEXT("Property")).JsonObject; Held.IsValid()) {
			Held->TryGetStringField(TEXT("Name"), Owner);
		}
	}

	const TSharedPtr<FJsonObject> Named = Variable.GetObject(TEXT("Property")).JsonObject;

	if (!Named.IsValid()) return;

	if (const TArray<TSharedPtr<FJsonValue>>* Path = nullptr; Named->TryGetArrayField(TEXT("Path"), Path) && Path->Num() == 1) {
		Member = (*Path)[0]->AsString();

		return;
	}

	if (FString Spelled; Named->TryGetStringField(TEXT("ObjectName"), Spelled)) {
		Spelled.Split(TEXT(":"), nullptr, &Spelled);

		Member = Spelled.Replace(TEXT("'"), TEXT(""));
	}
}

inline TSubclassOf<UObject> LoadClassFromPath(const FString& ObjectName, const FString& ObjectPath) {
	const FString FullPath = ObjectPath + TEXT(".") + ObjectName;

	if (UObject* LoadedObject = LoadObjectByPath<UObject>(FullPath)) {
		if (UClass* LoadedClass = Cast<UClass>(LoadedObject)) {
			return LoadedClass;
		}
	}

	const FCoreRedirectObjectName Named(*FullPath);
	const FCoreRedirectObjectName Now = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, Named);

	if (Now != Named) {
		if (UClass* Renamed = LoadObjectByPath<UClass>(Now.ToString())) {
			return Renamed;
		}
	}

	return nullptr;
}

inline TSubclassOf<UObject> LoadBlueprintClass(FString& ObjectPath) {
	const FString& ProjectName = GReflectionRuntime.Profile.ProjectName;

	if (!ProjectName.IsEmpty()) {
		ObjectPath = ObjectPath.Replace(*(ProjectName + "/Content"), TEXT("/Game"));
	}

	FString FullPath = ObjectPath; 
	if (FullPath.EndsWith(TEXT(".1"))) {
		FullPath = FullPath.LeftChop(2);
	}

	if (UObject* LoadedObject = LoadObjectByPath<UObject>(FullPath)) {
		const UBlueprint* LoadedBlueprint = Cast<UBlueprint>(LoadedObject);

		if (LoadedBlueprint && LoadedBlueprint->GeneratedClass) {
			return LoadedBlueprint->GeneratedClass;
		}

		if (FString Leaf = FullPath; Leaf.Split(TEXT("/"), nullptr, &Leaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) {
			if (UClass* Generated = LoadObjectByPath<UClass>(FullPath + TEXT(".") + Leaf + TEXT("_C"))) {
				return Generated;
			}
		}
	}

	return nullptr;
}

inline UClass* LoadClass(const TSharedPtr<FJsonObject>& SuperStruct) {
	FString ObjectName = SuperStruct->GetStringField(TEXT("ObjectName")).Replace(TEXT("Class'"), TEXT("")).Replace(TEXT("'"), TEXT(""));
	FString ObjectPath = SuperStruct->GetStringField(TEXT("ObjectPath"));

	{
		FString Named = ObjectPath.Contains(TEXT(".")) ? ObjectPath : ObjectPath + TEXT(".") + ObjectName;

		FRRedirects::Redirect(Named);

		if (int32 Dot; Named.FindLastChar(TEXT('.'), Dot)) {
			ObjectPath = Named.Left(Dot);
			ObjectName = Named.RightChop(Dot + 1);

			if (!ObjectPath.Contains(TEXT("/Script/")) && !ObjectName.EndsWith(TEXT("_C"))) {
				ObjectPath = Named;
			}
		}
	}

	if (ObjectPath.Contains("/Script/")) {
		return LoadClassFromPath(ObjectName, ObjectPath);
	}

	ObjectPath.Split(".", &ObjectPath, nullptr);

	if (UClass* Made = LoadBlueprintClass(ObjectPath)) {
		return Made;
	}

	if (!PackageHoldsAsset(ObjectPath) && TToolImportFromPath::Import(ObjectPath)) {
		return LoadBlueprintClass(ObjectPath);
	}

	return nullptr;
}

inline TSharedPtr<FJsonObject> GetSuperStructJsonObject(const TSharedPtr<FJsonObject>& JsonObject) {
	if (!JsonObject.IsValid()) return nullptr;

	for (const TCHAR* Named : { TEXT("Next"), TEXT("SuperStruct"), TEXT("Super") }) {
		if (const TSharedPtr<FJsonObject>* Found = nullptr; JsonObject->TryGetObjectField(Named, Found)) {
			return *Found;
		}
	}

	return nullptr;
}

inline EBlueprintType GetBlueprintTypeSaid(const TSharedPtr<FJsonObject>& AssetData, const UClass* Class);

inline EBlueprintType GetBlueprintType(const UClass* Class) {
	EBlueprintType BlueprintType = BPTYPE_Normal;

	if (Class->HasAnyClassFlags(CLASS_Const)) {
		BlueprintType = BPTYPE_Const;
	}

	if (Class == UBlueprintFunctionLibrary::StaticClass()) {
		BlueprintType = BPTYPE_FunctionLibrary;
	}

	if (Class == UInterface::StaticClass()) {
		BlueprintType = BPTYPE_Interface;
	}

	return BlueprintType;
}

inline EBlueprintType GetBlueprintTypeSaid(const TSharedPtr<FJsonObject>& AssetData, const UClass* Class) {
	const TSharedPtr<FJsonObject>* Tags = nullptr;

	if (AssetData.IsValid() && AssetData->TryGetObjectField(TEXT("EditorTags"), Tags)) {
		FString Says;

		if ((*Tags)->TryGetStringField(TEXT("BlueprintType"), Says) && !Says.IsEmpty()) {
			if (const UEnum* Kinds = StaticEnum<EBlueprintType>()) {
				const int64 Which = Kinds->GetValueByNameString(Says);

				if (Which != INDEX_NONE) return static_cast<EBlueprintType>(Which);
			}
		}
	}

	return GetBlueprintType(Class);
}

inline FUObjectExport* GetClassDefaultObject(FUObjectExportContainer* AssetContainer, const FUObjectJsonValueExport& JsonObject) {
	FUObjectExport* Export = AssetContainer->GetExportByObjectPath(JsonObject.GetObject("ClassDefaultObject"));
	if (!Export->IsJsonValid()) {
		Export = AssetContainer->GetExportStartingWith("Name", "Default__");
	}

	return Export;
}
