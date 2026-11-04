#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/BlueprintCookedMetaData.h"

#include "Containers/ExportContainer.h"
#include "Importers/Types/Blueprint/BlueprintGraphs.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace {
	TMap<FString, FString> Pairs(const FUObjectJsonValueExport& Holding, const TCHAR* Named) {
		TMap<FString, FString> Read;

		if (!Holding.Has(Named)) return Read;

		for (const FUObjectJsonValueExport& Entry : Holding.GetArray(Named)) {
			if (!Entry.Has(TEXT("Key"))) continue;

			Read.Add(Entry.GetString(TEXT("Key")), Entry.Has(TEXT("Value")) ? Entry.GetString(TEXT("Value")) : FString());
		}

		return Read;
	}

	UK2Node_Event* EventOf(const UBlueprint* Blueprint, const FString& Named) {
		for (UEdGraph* Page : Blueprint->UbergraphPages) {
			if (Page == nullptr) continue;

			for (UEdGraphNode* Node : Page->Nodes) {
				UK2Node_Event* Event = Cast<UK2Node_Event>(Node);

				if (Event == nullptr) continue;

				const FName Answers = Event->IsA<UK2Node_CustomEvent>()
					? static_cast<UK2Node_CustomEvent*>(Event)->CustomFunctionName
					: Event->EventReference.GetMemberName();

				if (Answers == FName(*Named)) return Event;
			}
		}

		return nullptr;
	}

}

namespace {
	void ApplyToEvent(UBlueprint* Blueprint, const FString& Named, const FUObjectJsonValueExport& Value, int32& Said) {
		UK2Node_Event* Event = EventOf(Blueprint, Named);

		if (Event == nullptr) return;

		const TMap<FString, FString> Told = Value.Has(TEXT("ObjectMetaData"))
			? Pairs(Value.GetObject(TEXT("ObjectMetaData")), TEXT("ObjectMetaData"))
			: TMap<FString, FString>();

		for (const TPair<FString, FString>& One : Told) {
			if (One.Value.IsEmpty()) continue;

			if (UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(Event)) {
#if UE5_1_BEYOND
				Custom->GetUserDefinedMetaData().SetMetaData(FName(*One.Key), One.Value);
#endif

				if (One.Key == FBlueprintMetadata::MD_CallInEditor.ToString()) {
					Custom->bCallInEditor = One.Value == TEXT("true");
				}
			}

			Said++;
		}
	}
}

TMap<FString, FString> FBlueprintCookedMetaData::WorldContexts(FUObjectExportContainer* Container) {
	TMap<FString, FString> Said;

	if (Container == nullptr) return Said;

	const FUObjectExport* Kept = Container->FindByType(FString(TEXT("ClassCookedMetaData")));

	if (Kept == nullptr || !Kept->IsJsonValid()) return Said;

	const FUObjectJsonValueExport Metadata(Kept->GetProperties());

	if (!Metadata.Has(TEXT("FunctionsMetaData"))) return Said;

	for (const FUObjectJsonValueExport& Entry : Metadata.GetArray(TEXT("FunctionsMetaData"))) {
		if (!Entry.Has(TEXT("Key")) || !Entry.Has(TEXT("Value"))) continue;

		const FUObjectJsonValueExport Value = Entry.GetObject(TEXT("Value"));

		if (!Value.Has(TEXT("ObjectMetaData"))) continue;

		for (const TPair<FString, FString>& One : Pairs(Value.GetObject(TEXT("ObjectMetaData")), TEXT("ObjectMetaData"))) {
			if (One.Key != FBlueprintMetadata::MD_WorldContext.ToString() || One.Value.IsEmpty()) continue;

			Said.Add(Entry.GetString(TEXT("Key")), One.Value);
		}
	}

	return Said;
}

int32 FBlueprintCookedMetaData::Apply(UBlueprint* Blueprint, FUObjectExportContainer* Container) {
	if (Blueprint == nullptr || Container == nullptr) return 0;

	const FUObjectExport* Kept = Container->FindByType(FString(TEXT("ClassCookedMetaData")));

	if (Kept == nullptr || !Kept->IsJsonValid()) return 0;

	const FUObjectJsonValueExport Metadata(Kept->GetProperties());

	int32 Said = 0;

	if (Metadata.Has(TEXT("ClassMetaData"))) {
		const FUObjectJsonValueExport Class = Metadata.GetObject(TEXT("ClassMetaData"));

		if (Class.Has(TEXT("PropertiesMetaData"))) {
			for (const FUObjectJsonValueExport& Entry : Class.GetArray(TEXT("PropertiesMetaData"))) {
				if (!Entry.Has(TEXT("Key")) || !Entry.Has(TEXT("Value"))) continue;

				const FName Variable(*Entry.GetString(TEXT("Key")));

				if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Variable) == INDEX_NONE) continue;

				for (const TPair<FString, FString>& Told : Pairs(Entry.GetObject(TEXT("Value")), TEXT("FieldMetaData"))) {
					FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Variable, nullptr, FName(*Told.Key), Told.Value);

					Said++;
				}
			}
		}
	}

	if (!Metadata.Has(TEXT("FunctionsMetaData"))) return Said;

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);

	for (const FUObjectJsonValueExport& Entry : Metadata.GetArray(TEXT("FunctionsMetaData"))) {
		if (!Entry.Has(TEXT("Key")) || !Entry.Has(TEXT("Value"))) continue;

		const FString Named = Entry.GetString(TEXT("Key"));

		UEdGraph* Graph = nullptr;

		for (UEdGraph* Candidate : Graphs) {
			if (Candidate != nullptr && Candidate->GetName() == Named) {
				Graph = Candidate;

				break;
			}
		}

		const FUObjectJsonValueExport Value = Entry.GetObject(TEXT("Value"));

		if (Graph == nullptr) {
			ApplyToEvent(Blueprint, Named, Value, Said);

			continue;
		}

		UK2Node_FunctionEntry* Entered = FBlueprintGraphs::EntryOf(Graph);

		if (Entered == nullptr) continue;
		const TMap<FString, FString> Told = Value.Has(TEXT("ObjectMetaData"))
			? Pairs(Value.GetObject(TEXT("ObjectMetaData")), TEXT("ObjectMetaData"))
			: TMap<FString, FString>();

		for (const TPair<FString, FString>& One : Told) {
			if (One.Value.IsEmpty()) continue;

#if UE5_1_BEYOND
			Entered->MetaData.SetMetaData(FName(*One.Key), One.Value);
#endif

			Said++;
		}

		const auto Given = [&Told](const FName Key, FString& Out) {
			if (const FString* Against = Told.Find(Key.ToString())) {
				Out = *Against;

				return true;
			}

			return false;
		};

		FString Held;

		if (Given(FBlueprintMetadata::MD_FunctionCategory, Held)) Entered->MetaData.Category = FText::FromString(Held);
		if (Given(FBlueprintMetadata::MD_Tooltip, Held)) Entered->MetaData.ToolTip = FText::FromString(Held);
		if (Given(FBlueprintMetadata::MD_FunctionKeywords, Held)) Entered->MetaData.Keywords = FText::FromString(Held);
		if (Given(FBlueprintMetadata::MD_CompactNodeTitle, Held)) Entered->MetaData.CompactNodeTitle = FText::FromString(Held);
#if !UE4_23_BELOW
		if (Given(FBlueprintMetadata::MD_DeprecationMessage, Held)) Entered->MetaData.DeprecationMessage = Held;
#endif
		if (Given(FBlueprintMetadata::MD_CallInEditor, Held)) Entered->MetaData.bCallInEditor = Held == TEXT("true");
#if ENGINE_UE5
		if (Given(FBlueprintMetadata::MD_ThreadSafe, Held)) Entered->MetaData.bThreadSafe = Held == TEXT("true");
#endif
	}

	return Said;
}
