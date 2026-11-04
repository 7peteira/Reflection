#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/GraphTidy.h"
#include "Engine/Compatibility.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_VariableGet.h"

namespace {
	TArray<TSharedRef<FGraphTidy>>& Tidies() {
		static TArray<TSharedRef<FGraphTidy>> Held;

		return Held;
	}
}

void AddGraphTidy(const TSharedRef<FGraphTidy>& Tidy) {
	Tidies().Add(Tidy);
}

const TArray<TSharedRef<FGraphTidy>>& GetGraphTidies() {
	return Tidies();
}

bool SetVariableGetVariation(UK2Node_VariableGet* Get, const FName Variation) {
	if (Get == nullptr) return false;

	FProperty* Wearing = UK2Node_VariableGet::StaticClass()->FindPropertyByName(TEXT("CurrentVariation"));

	if (Wearing == nullptr) return false;

	const UEnum* Shapes = nullptr;

	if (const FEnumProperty* AsEnum = CastField<FEnumProperty>(Wearing)) Shapes = AsEnum->GetEnum();
	else if (const FByteProperty* AsByte = CastField<FByteProperty>(Wearing)) Shapes = AsByte->Enum;

	if (Shapes == nullptr) return false;

	const int64 Wanted = Shapes->GetValueByNameString(Variation.ToString());

	if (Wanted == INDEX_NONE) return false;

	void* Value = Wearing->ContainerPtrToValuePtr<void>(Get);

	if (const FEnumProperty* AsEnum = CastField<FEnumProperty>(Wearing)) {
		AsEnum->GetUnderlyingProperty()->SetIntPropertyValue(Value, Wanted);
	} else if (const FByteProperty* AsByte = CastField<FByteProperty>(Wearing)) {
		AsByte->SetIntPropertyValue(Value, Wanted);
	} else {
		return false;
	}

	Get->ReconstructNode();

	return true;
}

void GiveReadersTheirOwn(UEdGraph* Graph, UK2Node_VariableGet* Converted, const TArray<UEdGraphPin*>& Readers) {
	if (Graph == nullptr || Converted == nullptr) return;

	for (UEdGraphPin* Reader : Readers) {
		if (Reader == nullptr) continue;

		UK2Node_VariableGet* Own = NewObject<UK2Node_VariableGet>(Graph, UK2Node_VariableGet::StaticClass(), NAME_None, RF_Transactional);

		Graph->AddNode(Own, false, false);

		Own->CreateNewGuid();
		Own->PostPlacedNewNode();

		Own->VariableReference = Converted->VariableReference;
		Own->AllocateDefaultPins();

		if (const UEdGraphNode* Wanting = Reader->GetOwningNode()) {
			Own->NodePosX = Wanting->NodePosX - 320;
			Own->NodePosY = Wanting->NodePosY + 40;
		}

		if (UEdGraphPin* Value = Own->FindPin(Own->GetVarName(), EGPD_Output)) {
			GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Value, Reader);
		}
	}
}
