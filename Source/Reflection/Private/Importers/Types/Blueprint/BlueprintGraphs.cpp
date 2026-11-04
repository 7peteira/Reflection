#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/BlueprintGraphs.h"
#include "AnimationGraphSchema.h"
#include "AnimationGraph.h"
#if UE4_23_BELOW
#include "AnimGraphNode_SubInput.h"
#else
#include "AnimGraphNode_LinkedInputPose.h"
#endif
#include "Importers/Types/Blueprint/BlueprintVariables.h"
#include "Importers/Types/Blueprint/MacroPattern.h"

#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "Importers/Types/Blueprint/BytecodeGraph.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace {
	bool Says(const FUObjectJsonValueExport& Function, const TCHAR* Flag) {
		return Function.Has(TEXT("FunctionFlags")) && Function.GetString(TEXT("FunctionFlags")).Contains(Flag);
	}

	void ReadSignature(const TArray<TSharedPtr<FJsonValue>>& Declared, TArray<TSharedPtr<FJsonObject>>& Takes, TArray<TSharedPtr<FJsonObject>>& GivesBack) {
		for (const TSharedPtr<FJsonValue>& Value : Declared) {
			const TSharedPtr<FJsonObject> Property = Value.IsValid() ? Value->AsObject() : nullptr;

			bool bGivenBack = false;

			if (!FBlueprintGraphs::IsParameter(Property, bGivenBack)) continue;

			(bGivenBack ? GivesBack : Takes).Add(Property);
		}
	}

	void PassedAs(const TSharedPtr<FJsonObject>& Parameter, FEdGraphPinType& Type) {
		const FString Spelled = Parameter->GetStringField(TEXT("PropertyFlags"));

		Type.bIsReference = Spelled.Contains(TEXT("ReferenceParm"));
		Type.bIsConst = Spelled.Contains(TEXT("ConstParm"));
	}

	template <typename T>
	T* Place(UEdGraph* Graph) {
		T* Node = NewObject<T>(Graph, T::StaticClass(), NAME_None, RF_Transactional);

		Graph->AddNode(Node, false, false);

		Node->CreateNewGuid();
		Node->PostPlacedNewNode();

		return Node;
	}

	FName CalledBy(const UK2Node_Event* Event) {
		if (!Event->bOverrideFunction && !Event->CustomFunctionName.IsNone()) {
			return Event->CustomFunctionName;
		}

		return Event->EventReference.GetMemberName();
	}
}

bool FBlueprintGraphs::IsParameter(const TSharedPtr<FJsonObject>& Property, bool& bOutGivenBack) {
	bOutGivenBack = false;

	FString Spelled;

	if (!Property.IsValid() || !Property->TryGetStringField(TEXT("PropertyFlags"), Spelled)) return false;

	TArray<FString> Named;
	Spelled.ParseIntoArray(Named, TEXT("|"), true);

	bool bParameter = false;
	bool bOut = false;
	bool bByReference = false;
	bool bReturned = false;

	for (FString& Flag : Named) {
		Flag.TrimStartAndEndInline();

		if (Flag == TEXT("Parm")) bParameter = true;
		if (Flag == TEXT("OutParm")) bOut = true;
		if (Flag == TEXT("ReferenceParm")) bByReference = true;
		if (Flag == TEXT("ReturnParm")) bReturned = true;
	}

	bOutGivenBack = bReturned || (bOut && !bByReference);

	return bParameter;
}

UK2Node_FunctionEntry* FBlueprintGraphs::EntryOf(const UEdGraph* Graph) {
	for (UEdGraphNode* Node : Graph->Nodes) {
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node)) return Entry;
	}

	return nullptr;
}

FBlueprintGraphs::FWritten FBlueprintGraphs::Reads(const FUObjectJsonValueExport& Function) {
	FWritten Written;

	FString Named;

	if (Function.JsonObject.IsValid()) {
		Function.JsonObject->TryGetStringField(TEXT("Name"), Named);
	}

	if (Says(Function, TEXT("FUNC_UbergraphFunction")) || Named.StartsWith(UEdGraphSchema_K2::FN_ExecuteUbergraphBase.ToString())) {
		Written.Kind = EWritten::Ubergraph;

		return Written;
	}

	const TArray<FUObjectJsonValueExport> Statements = Function.Has(TEXT("ScriptBytecode")) ? Function.GetArray(TEXT("ScriptBytecode")) : TArray<FUObjectJsonValueExport>();

	int32 Entered = INDEX_NONE;

	for (const FUObjectJsonValueExport& Statement : Statements) {
		const FString Token = MacroReading::TokenOf(Statement);

		if (Token == TEXT("EX_LetValueOnPersistentFrame")) {
			const FString Kept = MacroReading::NamedProperty(Statement.GetObject(TEXT("DestinationProperty")));
			const FString Given = MacroReading::ReadFrom(Statement.GetObject(TEXT("AssignmentExpression")));

			if (!Kept.IsEmpty() && !Given.IsEmpty()) {
				Written.Frame.Add(Kept, Given);
			}

			continue;
		}

		if (Token == TEXT("EX_Return") || Token == TEXT("EX_Nothing") || Token == TEXT("EX_EndOfScript")) continue;

		if (Entered != INDEX_NONE) return Written;

		if (!MacroReading::CallsTo(Statement).StartsWith(TEXT("ExecuteUbergraph"))) return Written;

		const TArray<FUObjectJsonValueExport> Arguments = Statement.Has(TEXT("Parameters")) ? Statement.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

		if (Arguments.Num() != 1 || MacroReading::TokenOf(Arguments[0]) != TEXT("EX_IntConst")) return Written;

		Entered = Arguments[0].GetInteger(TEXT("Value"), INDEX_NONE);
	}

	if (Entered == INDEX_NONE) return Written;

	Written.Kind = EWritten::Event;
	Written.EntryPoint = Entered;

	return Written;
}

UEdGraph* FBlueprintGraphs::Events(UBlueprint* Blueprint) {
	if (Blueprint == nullptr) return nullptr;

	for (UEdGraph* Page : Blueprint->UbergraphPages) {
		if (Page != nullptr) return Page;
	}

	return nullptr;
}

UEdGraph* FBlueprintGraphs::MakeLayer(UBlueprint* Blueprint, const FString& Name, const TArray<TSharedPtr<FJsonValue>>& Declared) {
	if (Blueprint == nullptr || Name.IsEmpty()) return nullptr;

	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UAnimationGraph::StaticClass(), UAnimationGraphSchema::StaticClass());

	if (Graph == nullptr) return nullptr;

	FBlueprintEditorUtils::AddDomainSpecificGraph(Blueprint, Graph);

	TArray<TSharedPtr<FJsonObject>> Takes;
	TArray<TSharedPtr<FJsonObject>> GivesBack;

	ReadSignature(Declared, Takes, GivesBack);

	UAnimGraphNode_LinkedInputPose* Handing = nullptr;

	int32 Which = 0;

	for (const TSharedPtr<FJsonObject>& Parameter : Takes) {
		FEdGraphPinType Type;

		if (!FBlueprintVariables::GetPinType(Parameter, Type)) continue;

		const FName Named(*Parameter->GetStringField(TEXT("Name")));

		if (UAnimationGraphSchema::IsPosePin(Type)) {
			Handing = NewObject<UAnimGraphNode_LinkedInputPose>(Graph);

			Graph->AddNode(Handing, false, false);

			Handing->CreateNewGuid();
			Handing->PostPlacedNewNode();

#if !UE4_23_BELOW
			Handing->Node.Name = Named;
			Handing->InputPoseIndex = Which;
#endif
			Which++;

			Handing->AllocateDefaultPins();

			continue;
		}

		if (Handing == nullptr) continue;

#if !UE4_23_BELOW
		Handing->Inputs.Add(FAnimBlueprintFunctionPinInfo(Named, Type));
#endif
	}

#if !UE4_23_BELOW
	for (UEdGraphNode* Node : Graph->Nodes) {
		if (UAnimGraphNode_LinkedInputPose* Takes_Pose = Cast<UAnimGraphNode_LinkedInputPose>(Node); Takes_Pose != nullptr && Takes_Pose->Inputs.Num() > 0) {
			Takes_Pose->ReconstructNode();
		}
	}
#endif

	return Graph;
}

UEdGraph* FBlueprintGraphs::Make(UBlueprint* Blueprint, const FString& Name, const FUObjectJsonValueExport& Function, const TArray<TSharedPtr<FJsonValue>>& Declared) {
	if (Blueprint == nullptr || Name.IsEmpty()) return nullptr;

	FString Making = Name;

	if (!Function.Has(TEXT("SuperStruct")) && Blueprint->ParentClass != nullptr) {
		if (const UFunction* Taken = Blueprint->ParentClass->FindFunctionByName(*Name); Taken != nullptr && !Taken->HasAnyFunctionFlags(FUNC_BlueprintEvent)) {
			Making = Name + FBlueprintGraphs::ClashSuffix;

			UE_LOG(LogTemp, Warning, TEXT("\"%s\" is a name %s already uses for a function of its own, so the blueprint's is made as \"%s\""),
				*Name, *Taken->GetOwnerClass()->GetName(), *Making);
		}
	}

	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Making), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

	if (Graph == nullptr) return nullptr;

	UFunction* Answered = nullptr;

	if (Function.Has(TEXT("SuperStruct"))) {
		Answered = FBytecodeGraph::ResolveFunction(Function.GetObject(TEXT("SuperStruct")));
	}

	if (Answered == nullptr && Blueprint->ParentClass != nullptr) {
		Answered = Blueprint->ParentClass->FindFunctionByName(*Name);

		if (Answered != nullptr && !Answered->HasAnyFunctionFlags(FUNC_BlueprintEvent)) {
			Answered = nullptr;
		}
	}

	if (Answered != nullptr) {
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, false, Answered->GetOwnerClass());

		return Graph;
	}

	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, static_cast<UClass*>(nullptr));

	UK2Node_FunctionEntry* Entry = EntryOf(Graph);

	if (Entry == nullptr) return Graph;

	int32 Extra = 0;

	if (Says(Function, TEXT("FUNC_BlueprintPure"))) Extra |= FUNC_BlueprintPure;
	if (Says(Function, TEXT("FUNC_Const"))) Extra |= FUNC_Const;

	if (Extra != 0) {
		Entry->AddExtraFlags(Extra);
	}

	int32 Access = FUNC_Public;

	if (Says(Function, TEXT("FUNC_Private"))) Access = FUNC_Private;
	else if (Says(Function, TEXT("FUNC_Protected"))) Access = FUNC_Protected;

	Entry->SetExtraFlags((Entry->GetExtraFlags() & ~(FUNC_Public | FUNC_Protected | FUNC_Private)) | Access);

	TArray<TSharedPtr<FJsonObject>> Takes;
	TArray<TSharedPtr<FJsonObject>> GivesBack;

	ReadSignature(Declared, Takes, GivesBack);

	for (const TSharedPtr<FJsonObject>& Parameter : Takes) {
		FEdGraphPinType Type;

		if (!FBlueprintVariables::GetPinType(Parameter, Type)) continue;

		PassedAs(Parameter, Type);

		Entry->CreateUserDefinedPin(FName(*Parameter->GetStringField(TEXT("Name"))), Type, EGPD_Output);
	}

	if (GivesBack.Num() == 0) return Graph;

	UK2Node_FunctionResult* Result = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);

	if (Result == nullptr) return Graph;

	for (const TSharedPtr<FJsonObject>& Parameter : GivesBack) {
		FEdGraphPinType Type;

		if (!FBlueprintVariables::GetPinType(Parameter, Type)) continue;

		PassedAs(Parameter, Type);

		Result->CreateUserDefinedPin(FName(*Parameter->GetStringField(TEXT("Name"))), Type, EGPD_Input);
	}

	return Graph;
}

int32 FBlueprintGraphs::RemoveUnwrittenEvents(UEdGraph* EventGraph, const TSet<FString>& Written) {
	if (EventGraph == nullptr) return 0;

	int32 Taken = 0;

	for (int32 Index = EventGraph->Nodes.Num() - 1; Index >= 0; --Index) {
		UK2Node_Event* Event = Cast<UK2Node_Event>(EventGraph->Nodes[Index]);

		if (Event == nullptr || !Event->bOverrideFunction) continue;

		if (Written.Contains(CalledBy(Event).ToString())) continue;

		EventGraph->RemoveNode(Event);

		Taken++;
	}

	return Taken;
}

UK2Node* FBlueprintGraphs::MakeEvent(UBlueprint* Blueprint, UEdGraph* EventGraph, const FString& Name, const TArray<TSharedPtr<FJsonValue>>& Declared) {
	if (Blueprint == nullptr || EventGraph == nullptr || Name.IsEmpty()) return nullptr;

	for (UEdGraphNode* Node : EventGraph->Nodes) {
		UK2Node_Event* Event = Cast<UK2Node_Event>(Node);

		if (Event != nullptr && CalledBy(Event) == FName(*Name)) return Event;
	}

	if (const UFunction* Declares = Blueprint->ParentClass != nullptr ? Blueprint->ParentClass->FindFunctionByName(*Name) : nullptr) {
		UK2Node_Event* Event = Place<UK2Node_Event>(EventGraph);

		Event->EventReference.SetExternalMember(FName(*Name), Declares->GetOwnerClass());
		Event->bOverrideFunction = true;

		Event->AllocateDefaultPins();

		return Event;
	}

	for (const FBPInterfaceDescription& Implemented : Blueprint->ImplementedInterfaces) {
		const UClass* Interface = Implemented.Interface;
		const UFunction* Declares = Interface != nullptr ? Interface->FindFunctionByName(FName(*Name)) : nullptr;

		if (Declares == nullptr) continue;

		UK2Node_Event* Event = Place<UK2Node_Event>(EventGraph);

		Event->EventReference.SetExternalMember(FName(*Name), Declares->GetOwnerClass());
		Event->bOverrideFunction = true;

		Event->AllocateDefaultPins();

		return Event;
	}

	UK2Node_CustomEvent* Event = Place<UK2Node_CustomEvent>(EventGraph);

	Event->CustomFunctionName = FName(*Name);
	Event->bIsEditable = true;

	Event->AllocateDefaultPins();

	TArray<TSharedPtr<FJsonObject>> Takes;
	TArray<TSharedPtr<FJsonObject>> GivesBack;

	ReadSignature(Declared, Takes, GivesBack);

	for (const TSharedPtr<FJsonObject>& Parameter : Takes) {
		FEdGraphPinType Type;

		if (!FBlueprintVariables::GetPinType(Parameter, Type)) continue;

		PassedAs(Parameter, Type);

		Event->CreateUserDefinedPin(FName(*Parameter->GetStringField(TEXT("Name"))), Type, EGPD_Output);
	}

	return Event;
}

UEdGraph* FBlueprintGraphs::MakeDispatcher(UBlueprint* Blueprint, const FString& Name, const TArray<TSharedPtr<FJsonValue>>& Declared) {
	if (Blueprint == nullptr || Name.IsEmpty()) return nullptr;

	for (UEdGraph* Kept : Blueprint->DelegateSignatureGraphs) {
		if (Kept != nullptr && Kept->GetName() == Name) return Kept;
	}

	if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*Name)) == INDEX_NONE) {
		FEdGraphPinType Carried;
		Carried.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;

		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), Carried)) return nullptr;
	}

	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

	if (Graph == nullptr) {
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*Name));

		return nullptr;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	Graph->bEditable = false;

	Schema->CreateDefaultNodesForGraph(*Graph);
	Schema->CreateFunctionGraphTerminators(*Graph, static_cast<UClass*>(nullptr));
	Schema->AddExtraFunctionFlags(Graph, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
	Schema->MarkFunctionEntryAsEditable(Graph, true);

	Blueprint->DelegateSignatureGraphs.Add(Graph);

	if (UK2Node_FunctionEntry* Entry = EntryOf(Graph)) {
		TArray<TSharedPtr<FJsonObject>> Takes;
		TArray<TSharedPtr<FJsonObject>> GivesBack;

		ReadSignature(Declared, Takes, GivesBack);

		for (const TSharedPtr<FJsonObject>& Parameter : Takes) {
			FEdGraphPinType Type;

			if (!FBlueprintVariables::GetPinType(Parameter, Type)) continue;

			PassedAs(Parameter, Type);

			Entry->CreateUserDefinedPin(FName(*Parameter->GetStringField(TEXT("Name"))), Type, EGPD_Output);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	return Graph;
}
