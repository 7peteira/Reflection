#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/GraphTidy.h"
#include "Importers/Types/Blueprint/MacroPattern.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetSystemLibrary.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionTidyValid, All, All);

namespace {
	UEdGraphPin* Named(UEdGraphNode* Node, const TCHAR* Wanted, const EEdGraphPinDirection Direction) {
		if (Node == nullptr) return nullptr;

		const FString Asked = FString(Wanted).Replace(TEXT(" "), TEXT(""));

		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin == nullptr || Pin->Direction != Direction) continue;

			if (Pin->PinName.ToString().Replace(TEXT(" "), TEXT("")).Equals(Asked, ESearchCase::IgnoreCase)) return Pin;
		}

		return nullptr;
	}

	UEdGraphPin* WayInto(UEdGraphNode* Node) {
		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin != nullptr && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) return Pin;
		}

		return nullptr;
	}
}

struct FIsValidMacroTidy final : FGraphTidy {
	virtual const TCHAR* GetName() const override { return TEXT("IsValid"); }

	virtual int32 Apply(UEdGraph* Graph) const override {
		if (Graph == nullptr) return 0;

		int32 PutBack = 0;

		TArray<UEdGraphNode*> Nodes = Graph->Nodes;

		for (UEdGraphNode* Node : Nodes) {
			UK2Node_CallFunction* Asking = Cast<UK2Node_CallFunction>(Node);

			if (Asking == nullptr || Asking->FunctionReference.GetMemberName() != TEXT("IsValid")) continue;

			if (Asking->FunctionReference.GetMemberParentClass() != UKismetSystemLibrary::StaticClass()) continue;

			UEdGraphPin* Object = Asking->FindPin(TEXT("Object"), EGPD_Input);
			UEdGraphPin* Answer = Asking->GetReturnValuePin();

			if (Object == nullptr || Answer == nullptr) continue;

			if (Object->LinkedTo.Num() != 1 || Answer->LinkedTo.Num() != 1) continue;

			UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Answer->LinkedTo[0]->GetOwningNode());

			if (Branch == nullptr || Answer->LinkedTo[0] != Branch->GetConditionPin()) continue;

			UEdGraphPin* Source = Object->LinkedTo[0];

			if (const UK2Node_VariableGet* Reading = Cast<UK2Node_VariableGet>(Source->GetOwningNode())) {
				if (Reading->IsNodePure() && Source->LinkedTo.Num() == 1) continue;
			}

			UEdGraph* Definition = MacroReading::StandardMacro(TEXT("IsValid"));

			if (Definition == nullptr) {
				UE_LOG(LogReflectionTidyValid, Warning, TEXT("%s"), TEXT("no IsValid macro to put back into"));

				continue;
			}

			UEdGraphPin* Entered = WayInto(Branch);

			const TArray<UEdGraphPin*> Enters = Entered != nullptr ? Entered->LinkedTo : TArray<UEdGraphPin*>();
			const TArray<UEdGraphPin*> WhenValid = Branch->GetThenPin() != nullptr ? Branch->GetThenPin()->LinkedTo : TArray<UEdGraphPin*>();
			const TArray<UEdGraphPin*> WhenNot = Branch->GetElsePin() != nullptr ? Branch->GetElsePin()->LinkedTo : TArray<UEdGraphPin*>();

			UK2Node_MacroInstance* Asks = NewObject<UK2Node_MacroInstance>(Graph, UK2Node_MacroInstance::StaticClass(), NAME_None, RF_Transactional);

			Graph->AddNode(Asks, false, false);

			Asks->CreateNewGuid();
			Asks->PostPlacedNewNode();

			Asks->SetMacroGraph(Definition);
			Asks->AllocateDefaultPins();

			Asks->NodePosX = Branch->NodePosX;
			Asks->NodePosY = Branch->NodePosY;

			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

			if (UEdGraphPin* Given = Named(Asks, TEXT("InputObject"), EGPD_Input)) {
				Schema->TryCreateConnection(Source, Given);
			}

			if (UEdGraphPin* In = WayInto(Asks)) {
				for (UEdGraphPin* Enter : Enters) Schema->TryCreateConnection(Enter, In);
			}

			if (UEdGraphPin* Valid = Named(Asks, TEXT("Is Valid"), EGPD_Output)) {
				for (UEdGraphPin* Next : WhenValid) Schema->TryCreateConnection(Valid, Next);
			}

			if (UEdGraphPin* NotValid = Named(Asks, TEXT("Is Not Valid"), EGPD_Output)) {
				for (UEdGraphPin* Next : WhenNot) Schema->TryCreateConnection(NotValid, Next);
			}

			Graph->RemoveNode(Branch);
			Graph->RemoveNode(Asking);

			PutBack++;
		}

		if (PutBack > 0) {
			UE_LOG(LogReflectionTidyValid, Display, TEXT("\"%s\" had %d check(s) asked and branched on at once"), *Graph->GetName(), PutBack);
		}

		return PutBack;
	}
};

REGISTER_TIDY(FIsValidMacroTidy)
