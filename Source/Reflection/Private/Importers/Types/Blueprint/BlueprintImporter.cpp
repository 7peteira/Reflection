#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/BlueprintImporter.h"
#include "Importers/Types/Blueprint/BlueprintCompile.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_Root.h"
#include "EdGraphSchema_K2.h"

#include "KismetCompilerModule.h"
#include "MovieScene.h"
#include "WidgetBlueprint.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_WidgetAnimationEvent.h"
#include "Engine/ComponentDelegateBinding.h"
#include "Animation/WidgetAnimationDelegateBinding.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/MovieSceneWidgetMaterialTrack.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "Engine/SCS_Node.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/InheritableComponentHandler.h"
#include "Importers/Types/Blueprint/BlueprintUtilities.h"
#include "Importers/Types/Blueprint/BlueprintVariables.h"
#include "Importers/Types/Blueprint/BlueprintGraphs.h"
#include "K2Node_Timeline.h"
#include "Engine/TimelineTemplate.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "UObject/Script.h"
#if UE5_7_BEYOND
#include "Misc/StringOutputDevice.h"
#endif

#include "ScriptDisassembler.h"
#include "Importers/Types/Blueprint/BlueprintCookedMetaData.h"
#include "Importers/Types/Blueprint/BytecodeGraph.h"
#include "Importers/Constructor/TypesHelper.h"

UObject* IBlueprintImporter::CreateAsset(UObject* CreatedAsset) {
	UClass* Class = GetAssetClass();

	if (!Class) {
		FImportIssues::Report(
			EImportIssue::MissingClass,
			TEXT("Couldn't resolve the parent class"),
			TEXT("The blueprint's parent class could not be found or loaded. Verify that the class is defined and available when reflecting.")
		);

		return nullptr;
	}

	if (!ClassIsFormed(Class)) {
		FImportIssues::Report(
			EImportIssue::MissingClass,
			FString::Printf(TEXT("Nothing was made of the parent class %s"), *Class->GetName()),
			TEXT("Its class is in memory but was never finished, most often because the blueprint above this one did not come across. Building on it would stop the editor, so nothing was made for this one.")
		);

		return nullptr;
	}

	if (Class->IsChildOf(ALevelScriptActor::StaticClass())) {
		FImportIssues::Report(
			EImportIssue::Data,
			FString::Printf(TEXT("\"%s\" is a level's own script"), *GetAssetName()),
			TEXT("A level script is part of the map it was written in rather than an asset of its own, and the map already holds that name. Nothing was made for it, and whatever named it carried on.")
		);

		return nullptr;
	}

	if (const UObject* Standing = FindObject<UObject>(GetPackage(), *GetAssetName()); Standing != nullptr && !Standing->IsA<UBlueprint>()) {
		FImportIssues::Report(
			EImportIssue::Failed,
			FString::Printf(TEXT("\"%s\" is already taken"), *GetAssetName()),
			FString::Printf(TEXT("Something of that name is already in the package and it is a %s rather than a blueprint, so a blueprint cannot be made there. Rename or delete it before reflecting."), *Standing->GetClass()->GetName())
		);

		return nullptr;
	}

	UClass* BlueprintClass = nullptr, *GeneratedClass = nullptr;

	FModuleManager::LoadModuleChecked<IKismetCompilerInterface>
		("KismetCompiler")
			.GetBlueprintTypesForClass(
				Class,
				BlueprintClass,
				GeneratedClass
			);

	if (UBlueprint* ExistingBlueprint = LoadObject<UBlueprint>(nullptr, *GetPackage()->GetPathName())) {
		if (ExistingBlueprint->ParentClass != Class && Class->IsChildOf(ExistingBlueprint->ParentClass)) {
			UE_LOG(LogReflection, Display, TEXT("Reparenting %s from %s to %s, the parent its export names"), *ExistingBlueprint->GetName(), *GetNameSafe(ExistingBlueprint->ParentClass), *Class->GetName());

			ExistingBlueprint->ParentClass = Class;
			FBlueprintEditorUtils::RefreshAllNodes(ExistingBlueprint);
			FBlueprintEditorUtils::MarkBlueprintAsModified(ExistingBlueprint);
			CompileBlueprintGuarded(ExistingBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		}

		UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(ExistingBlueprint->GeneratedClass);
		FBlueprintEditorUtils::PropagateParentBlueprintDefaults(BlueprintGeneratedClass);

		return IImporter::CreateAsset(BlueprintGeneratedClass);
	}

	const UBlueprint* CreatedBlueprint = CreateBlueprintGuarded(
		Class,
		GetPackage(),
		FName(*GetAssetName()),
		GetBlueprintTypeSaid(GetAssetData(), Class),
		BlueprintClass,
		GeneratedClass
	);

	if (!CreatedBlueprint) return nullptr;

	return IImporter::CreateAsset(CreatedBlueprint->GeneratedClass);
}

bool IBlueprintImporter::Import() {
	const UBlueprintGeneratedClass* BlueprintGeneratedClass = Create<UBlueprintGeneratedClass>();
	if (!BlueprintGeneratedClass) return false;

	Blueprint = UBlueprint::GetBlueprintFromClass(BlueprintGeneratedClass);
	if (!Blueprint) return false;

	UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	FUObjectExport* ClassDefaultObjectExport = GetClassDefaultObject(GetContainer(), GetAssetDataAsValue());

	if (ClassDefaultObjectExport->IsJsonInvalid()) return false;

	ClassDefaultObjectExport->Object = GeneratedClass != nullptr ? GeneratedClass->GetDefaultObject() : nullptr;

	if (ConstructVariables() > 0) {
		if (!CompileBlueprintGuarded(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection)) return false;

		GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
		if (!GeneratedClass) return false;

		ClassDefaultObjectExport->Object = GeneratedClass->GetDefaultObject();
	}

	GetObjectSerializer()->DeserializeObjectProperties(ClassDefaultObjectExport->GetProperties(), GeneratedClass->GetDefaultObject());

	{
		UObject* Defaults = GeneratedClass->GetDefaultObject();
		const FName DefaultsName = ClassDefaultObjectExport->GetName();

		for (FUObjectExport* Export : GetContainer()->Exports) {
			if (Export == nullptr || Export == ClassDefaultObjectExport || !Export->IsJsonValid()) continue;
			if (Export->GetOuter() != DefaultsName) continue;

			FString Flags;

			if (!Export->JsonObject->TryGetStringField(TEXT("Flags"), Flags) || !Flags.Contains(TEXT("RF_DefaultSubObject"))) continue;

			UObject* Made = Defaults->GetDefaultSubobjectByName(Export->GetName());

			if (Made == nullptr) continue;

			Export->Object = Made;

			if (Export->JsonObject->HasTypedField<EJson::Object>(TEXT("Properties"))) {
				GetObjectSerializer()->DeserializeObjectProperties(Export->GetProperties(), Made);
			}
		}
	}

	GetObjectSerializer()->bUseExperimentalSpawning = true;

	ConstructScript();
	ConstructOverriddenComponents();
	ConstructWidgetTree();

	ConstructBody();

	if (ConstructAnimationEvents() + ConstructComponentEvents() > 0) {
		CompileBlueprintGuarded(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	}

	Blueprint->LastEditedDocuments.Empty();

	return OnAssetCreation(Blueprint);
}

int32 IBlueprintImporter::ConstructInterfaces() {
	if (Blueprint == nullptr) return 0;

	const FUObjectJsonValueExport Data = GetAssetDataAsValue();

	if (!Data.Has(TEXT("Interfaces"))) return 0;

	int32 Added = 0;

	for (const FUObjectJsonValueExport& Entry : Data.GetArray(TEXT("Interfaces"))) {
		if (!Entry.GetBool(TEXT("bImplementedByK2"), true)) continue;
		if (!Entry.Has(TEXT("Class"))) continue;

		const FUObjectJsonValueExport Named = Entry.GetObject(TEXT("Class"));

		FString Kind;
		FString Called;

		if (Named.Has(TEXT("ObjectName"))) {
			const FString Spelled = Named.GetString(TEXT("ObjectName"));

			if (!Spelled.Split(TEXT("'"), &Kind, &Called)) {
				Called = Spelled;
			}

			Called.RemoveFromEnd(TEXT("'"));

			FString Held;

			if (Called.Split(TEXT(":"), nullptr, &Held)) Called = Held;
		}

		if (Called.IsEmpty()) continue;

		UClass* Interface = const_cast<UClass*>(FindClassByType(Called));

		if (Interface == nullptr && Named.Has(TEXT("ObjectPath"))) {
			FString Where = ToEditorPackagePath(Named.GetString(TEXT("ObjectPath")));

			if (int32 Sits; Where.FindLastChar(TEXT('.'), Sits)) {
				const FString After = Where.RightChop(Sits + 1);

				if (!After.IsEmpty() && After.IsNumeric()) LeftInline(Where, Sits);
			}

			FString Leaf = Where;

			if (int32 Slash; Where.FindLastChar(TEXT('/'), Slash)) Leaf = Where.RightChop(Slash + 1);

			if (!Where.IsEmpty() && !Leaf.IsEmpty() && !Kind.IsEmpty()) {
				TObjectPtr<UObject> Brought = nullptr;
				bool bBrought = false;

				FAssetUtilities::ConstructAsset<UObject>(Where, Where + TEXT(".") + Leaf, Kind, Brought, bBrought);

				Interface = const_cast<UClass*>(FindClassByType(Called));
			}
		}

		if (Interface == nullptr) {
			FImportIssues::Report(
				EImportIssue::MissingClass,
				TEXT("An interface the blueprint answers for is missing"),
				FString::Printf(TEXT("'%s' implements '%s', which is neither in the project nor anything the Cloud would give."), *GetAssetName(), *Called)
			);

			continue;
		}

		bool bHeld = false;

		for (const FBPInterfaceDescription& Description : Blueprint->ImplementedInterfaces) {
			if (Description.Interface == Interface) bHeld = true;
		}

		if (bHeld || (Blueprint->ParentClass != nullptr && Blueprint->ParentClass->ImplementsInterface(Interface))) continue;

		if (FBlueprintEditorUtils::ImplementNewInterface(Blueprint, Interface->GetFName())) {
			UE_LOG(LogReflection, Display, TEXT("\"%s\" answers for \"%s\""), *GetAssetName(), *Interface->GetName());

			Added++;
		}
	}

	return Added;
}

int32 IBlueprintImporter::ConstructBody() {
	if (const int32 Answered = ConstructInterfaces(); Answered > 0) {
		UE_LOG(LogReflection, Display, TEXT("%d interface(s) answered for"), Answered);
	}

	if (const int32 Timelines = ConstructTimelines(); Timelines > 0) {
		UE_LOG(LogReflection, Display, TEXT("%d timeline(s) rebuilt"), Timelines);
	}

	const int32 Laid = GetSettings()->AssetSettings.ImportBlueprintCode ? ConstructGraphs() : 0;

	if (!GetSettings()->AssetSettings.ImportBlueprintCode) {
		FImportIssues::Report(EImportIssue::Setting,
			TEXT("The graphs were left empty"),
			TEXT("Import Blueprint Code is off, so what the blueprint does was not read back. Everything else about it came across."));
	}

	if (const UEdGraph* Events = FBlueprintGraphs::Events(Blueprint)) {
		int32 Standing = 0;

		for (UEdGraphNode* Node : Events->Nodes) {
			if (Node != nullptr && Node->IsA<UK2Node_Timeline>()) Standing++;
		}

		UE_LOG(LogReflection, Display, TEXT("%d timeline node(s) still in the event graph after laying it out"), Standing);
	}

	if (const int32 Said = FBlueprintCookedMetaData::Apply(Blueprint, GetContainer()); Said > 0) {
		UE_LOG(LogReflection, Display, TEXT("%d thing(s) the editor knew were kept through the cook and said again"), Said);
	}

	if (Laid > 0) {
 		CompileBlueprintGuarded(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

		constexpr bool bMatchEventLayout = false;

		if (bMatchEventLayout && AssetEntries.Num() > 0 && IsRunningCommandlet()) {
			const int32 Landed = MatchEventLayout();

			UE_LOG(LogReflection, Display, TEXT("%d of %d event(s) entered where the asset enters them"), Landed, AssetEntries.Num());
		}

		if (const UBlueprintGeneratedClass* Compiled = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass)) {
			for (TFieldIterator<UFunction> It(Compiled, EFieldIteratorFlags::ExcludeSuper); It; ++It) {
				UE_LOG(LogReflection, Display, TEXT("compiled \"%s\" to %d byte(s) of script"), *It->GetName(), It->Script.Num());
			}
		}
	}

	Blueprint->bIsNewlyCreated = false;

	if (Blueprint->GeneratedClass == nullptr) {
		CompileBlueprintGuarded(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

		if (Blueprint->GeneratedClass == nullptr) {
			FImportIssues::Report(
				EImportIssue::Failed,
				TEXT("Nothing was made of the blueprint"),
				FString::Printf(TEXT("'%s' compiled to no class at all. Opening it would take the editor down, so it is better deleted than kept."), *GetAssetName())
			);
		}
	}

	return Laid;
}

int32 IBlueprintImporter::MatchEventLayout() {
	UEdGraph* EventGraph = FBlueprintGraphs::Events(Blueprint);

	if (EventGraph == nullptr || AssetEntries.Num() == 0) return 0;

	const auto Measure = [this](int32& OutLanded, int64& OutOff) {
		OutLanded = 0;
		OutOff = 0;

		if (const UBlueprintGeneratedClass* Built = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass); Built != nullptr && Built->UberGraphFunction != nullptr && AssetUbergraphStatements.Num() > 0) {
			FStringOutputDevice Written;
			FKismetBytecodeDisassembler Disassembler(Written);
			Disassembler.DisassembleStructure(Built->UberGraphFunction);

			int32 Matched = 0;
			int32 Seen = 0;
			const FString Text = Written;

			for (int32 At = Text.Find(TEXT("Label_0x")); At != INDEX_NONE; At = Text.Find(TEXT("Label_0x"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 8)) {
				const int32 Colon = Text.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);

				if (Colon == INDEX_NONE) break;

				const int32 Offset = FParse::HexNumber(*Text.Mid(At + 8, Colon - At - 8));

				Seen++;

				if (AssetUbergraphStatements.Contains(Offset)) Matched++;
			}

			OutOff += (int64)(FMath::Max(Seen, AssetUbergraphStatements.Num()) - Matched) * 4;
			OutLanded -= (Matched == AssetUbergraphStatements.Num() && Seen == Matched) ? 0 : 1;
		}

		const UBlueprintGeneratedClass* Compiled = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);

		if (Compiled == nullptr || Compiled->UberGraphFunction == nullptr) return;

		for (const TPair<FName, int32>& Wanted : AssetEntries) {
			const UFunction* Stub = Compiled->FindFunctionByName(Wanted.Key, EIncludeSuperFlag::ExcludeSuper);

			int32 Entered = INDEX_NONE;

			if (Stub != nullptr) {
				const TArray<uint8>& Script = Stub->Script;
				const int32 Span = 1 + sizeof(UFunction*) + 1 + sizeof(int32);

				for (int32 At = 0; At + Span <= Script.Num(); ++At) {
					if (Script[At] != EX_LocalFinalFunction && Script[At] != EX_FinalFunction) continue;

					UFunction* Called = nullptr;
					FMemory::Memcpy(&Called, &Script[At + 1], sizeof(UFunction*));

					if (Called != Compiled->UberGraphFunction || Script[At + 1 + sizeof(UFunction*)] != EX_IntConst) continue;

					FMemory::Memcpy(&Entered, &Script[At + 2 + sizeof(UFunction*)], sizeof(int32));

					break;
				}
			}

			if (Entered == INDEX_NONE) {
				OutOff += 100000;
			} else if (Entered == Wanted.Value) {
				OutLanded++;
			} else {
				OutOff += FMath::Abs(Entered - Wanted.Value);
			}
		}
	};

	int32 BestLanded = 0;
	int64 BestOff = 0;

	Measure(BestLanded, BestOff);

	const int32 Count = EventGraph->Nodes.Num();

	int32 Tries = 0;
	const int32 MostTries = FMath::Max(400, Count * Count * 3);
	const double GiveUpAt = FPlatformTime::Seconds() + 30.0;

	bool bMoved = true;

	while (bMoved && !(BestLanded == AssetEntries.Num() && BestOff == 0) && Tries < MostTries) {
		bMoved = false;

		for (int32 From = 0; From < Count && !(BestLanded == AssetEntries.Num() && BestOff == 0) && Tries < MostTries; ++From) {
			TArray<UEdGraphNode*> Block;

			const TFunction<void(UEdGraphNode*)> Gather = [&Block, &Gather](UEdGraphNode* Node) {
				if (Node == nullptr || Block.Contains(Node)) return;

				Block.Add(Node);

				for (const UEdGraphPin* Pin : Node->Pins) {
					if (Pin == nullptr || Pin->Direction != EGPD_Input) continue;

					for (const UEdGraphPin* From : Pin->LinkedTo) {
						const UK2Node* Feeds = From != nullptr ? Cast<UK2Node>(From->GetOwningNode()) : nullptr;

						if (Feeds != nullptr && Feeds->IsNodePure()) Gather(const_cast<UK2Node*>(Feeds));
					}
				}
			};

			Gather(EventGraph->Nodes[From]);

			Block.Sort([EventGraph](const UEdGraphNode& One, const UEdGraphNode& Other) {
				return EventGraph->Nodes.IndexOfByKey(&One) < EventGraph->Nodes.IndexOfByKey(&Other);
			});

			const int32 Ways = Block.Num() > 1 ? 2 : 1;

			bool bTook = false;

			for (int32 Way = 0; Way < Ways && !bTook; ++Way) {
				TArray<UEdGraphNode*> Moving = Way == 0 ? TArray<UEdGraphNode*>{ EventGraph->Nodes[From] } : Block;

				for (int32 To = 0; To < Count && Tries < MostTries; ++To) {
					const auto Before = EventGraph->Nodes;

					for (UEdGraphNode* Node : Moving) EventGraph->Nodes.Remove(Node);

					const int32 Into = FMath::Min(To, EventGraph->Nodes.Num());

					for (int32 Index = 0; Index < Moving.Num(); ++Index) {
						EventGraph->Nodes.Insert(Moving[Index], Into + Index);
					}

					if (EventGraph->Nodes == Before) continue;

					CompileBlueprintGuarded(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
					Tries++;

					if (FPlatformTime::Seconds() >= GiveUpAt) Tries = MostTries;

					int32 Landed = 0;
					int64 Off = 0;

					Measure(Landed, Off);

					if (Landed > BestLanded || (Landed == BestLanded && Off < BestOff)) {
						BestLanded = Landed;
						BestOff = Off;
						bMoved = true;
						bTook = true;

						break;
					}

					EventGraph->Nodes = Before;
				}
			}
		}
	}

	CompileBlueprintGuarded(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

	UE_LOG(LogReflection, Display, TEXT("laid the event graph out against the asset in %d compile(s)"), Tries);

	return BestLanded;
}

int32 IBlueprintImporter::ConstructVariables() {
	TSet<FString> Components = FBlueprintVariables::GetComponentVariables(GetContainer());

	Components.Append(FBlueprintVariables::GetWidgetVariables(GetContainer()));

	TArray<TSharedPtr<FJsonValue>> Declared;

	for (const TSharedPtr<FJsonValue>& Value : FBlueprintVariables::GetDeclared(GetAssetExport(), GetContainer())) {
		const TSharedPtr<FJsonObject> Property = Value.IsValid() ? Value->AsObject() : nullptr;

		FString Name;

		if (Property.IsValid() && Property->TryGetStringField(TEXT("Name"), Name) && Components.Contains(Name)) continue;

		Declared.Add(Value);
	}

	return FBlueprintVariables::Construct(Blueprint, Declared, GetContainer());
}

void IBlueprintImporter::ConstructScript() const {
	if (!GetAssetDataAsValue().Has("SimpleConstructionScript")) return;

	UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);

	if (USimpleConstructionScript* PreviousSimpleConstructionScript = GeneratedClass->SimpleConstructionScript; PreviousSimpleConstructionScript != nullptr) {
		for (USCS_Node* Node : PreviousSimpleConstructionScript->GetAllNodes()) {
			MoveToTransientPackageAndRename(Node->ComponentTemplate);
		}

		MoveToTransientPackagesAndRename({
			PreviousSimpleConstructionScript,
			Blueprint->SimpleConstructionScript
		});

		const auto Forget = [GeneratedClass](auto& Kept) {
			Kept.RemoveAll([GeneratedClass](const auto& Component) {
				return Component == nullptr || Component->GetOuter() != GeneratedClass;
			});
		};

		Forget(Blueprint->ComponentTemplates);
		Forget(GeneratedClass->ComponentTemplates);
	}

	FUObjectExport* Export = GetContainer()->GetExportByObjectPath(GetAssetDataAsValue().GetObject("SimpleConstructionScript"));

	USimpleConstructionScript* SimpleConstructionScript =
		Cast<USimpleConstructionScript>(
			GetObjectSerializer()->SpawnExport(Export)
		);

	UE_LOG(LogReflection, Display, TEXT("construction script: export %s, spawned %s, %d node(s)"),
		Export != nullptr && Export->IsJsonValid() ? TEXT("found") : TEXT("MISSING"),
		SimpleConstructionScript != nullptr ? TEXT("yes") : TEXT("NO"),
		SimpleConstructionScript != nullptr ? SimpleConstructionScript->GetAllNodes().Num() : -1);

	if (SimpleConstructionScript == nullptr) return;

	Blueprint->SimpleConstructionScript = SimpleConstructionScript;
	GeneratedClass->SimpleConstructionScript = SimpleConstructionScript;

	SimpleConstructionScript->FixupRootNodeParentReferences();
	SimpleConstructionScript->ValidateSceneRootNodes();

	TSet<const FUObjectExport*> Spoken;

	for (USCS_Node* Node : SimpleConstructionScript->GetAllNodes()) {
		if (Node == nullptr || Node->ComponentTemplate != nullptr) continue;

		FUObjectExport* Made = GetContainer()->Find(FName(*(Node->GetVariableName().ToString() + TEXT("_GEN_VARIABLE"))));

		if (Made == nullptr || !Made->IsJsonValid()) {
			Made = nullptr;

			int32 Fits = 0;

			const FString Kindly = Node->ComponentClass != nullptr ? Node->ComponentClass->GetName() : FString();

			for (FUObjectExport* Held : GetContainer()->Exports) {
				if (Held == nullptr || !Held->IsJsonValid() || Held->Object != nullptr || Kindly.IsEmpty()) continue;
				if (Spoken.Contains(Held)) continue;

				FString Says;

				if (!Held->JsonObject->TryGetStringField(TEXT("Type"), Says) || Says != Kindly) continue;
				if (Held->GetClass() != Node->ComponentClass) continue;

				Made = Held;

				Fits++;
			}

			if (Fits != 1) continue;
		}

		UClass* Kind = Made->GetClass();

		if (Kind == nullptr || !Kind->IsChildOf(UActorComponent::StaticClass())) continue;

		const FString Wanted = Made->GetName().ToString();

		if (UObject* Holding = StaticFindObject(nullptr, GeneratedClass, *Wanted)) {
			MoveToTransientPackageAndRename(Holding);
		}

		UActorComponent* Template = NewObject<UActorComponent>(GeneratedClass, Kind, *Wanted, RF_ArchetypeObject | RF_Public | RF_Transactional);

		if (Template == nullptr) continue;

		Made->Object = Template;

		Spoken.Add(Made);

		GetObjectSerializer()->DeserializeObjectProperties(Made->GetProperties(), Template);

		Node->ComponentTemplate = Template;

		UE_LOG(LogReflection, Display, TEXT("\"%s\" had no template of its own, so \"%s\" was made where the class keeps it"),
			*Node->GetVariableName().ToString(), *Wanted);
	}

	{
		TSet<FName> Taken;

		TArray<USCS_Node*> Nameless;

		for (USCS_Node* Node : SimpleConstructionScript->GetAllNodes()) {
			if (Node == nullptr) continue;

			if (Node->GetVariableName().IsNone()) {
				Nameless.Add(Node);
			} else {
				Taken.Add(Node->GetVariableName());
			}
		}

		for (USCS_Node* Node : Nameless) {
			if (Node->ComponentClass == nullptr) continue;

			FName Called = NAME_None;

			int32 Fits = 0;

			for (TFieldIterator<FObjectProperty> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It) {
				if (!It->HasAnyPropertyFlags(CPF_InstancedReference)) continue;
				if (It->PropertyClass != Node->ComponentClass) continue;
				if (Taken.Contains(It->GetFName())) continue;

				Called = It->GetFName();

				Fits++;
			}

			if (Fits != 1) continue;

			Node->SetVariableName(Called, false);

			Taken.Add(Called);

			UE_LOG(LogReflection, Display, TEXT("a %s the script names nothing is what \"%s\" holds, so that is what it is called"),
				*Node->ComponentClass->GetName(), *Called.ToString());
		}
	}

	for (const USCS_Node* Node : SimpleConstructionScript->GetAllNodes()) {
		UE_LOG(LogReflection, Display, TEXT("  scs \"%s\" template %s kept by %s (class keeps %d, blueprint keeps %d)"),
			*Node->GetVariableName().ToString(),
			*GetNameSafe(Node->ComponentTemplate),
			Node->ComponentTemplate != nullptr ? *GetNameSafe(Node->ComponentTemplate->GetOuter()) : TEXT("-"),
			GeneratedClass->ComponentTemplates.Num(),
			Blueprint->ComponentTemplates.Num());
	}
}

class UWidgetTreeAccessor final : public UWidgetTree {
public:

#if ENGINE_UE5
	TArray<TObjectPtr<UWidget>> GetWidgets() {
#else
	TArray<UWidget*> GetWidgets() {
#endif
		return AllWidgets;
	}
};

namespace {
	UK2Node_CustomEvent* EventCalled(const UEdGraph* Graph, const FString& Called) {
		for (UEdGraphNode* Held : Graph->Nodes) {
			UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Held);

			if (Event != nullptr && Event->CustomFunctionName == FName(*Called)) return Event;
		}

		return nullptr;
	}

	void StandsFor(UEdGraph* Graph, UK2Node_Event* Made, UK2Node_CustomEvent* Stood) {
		Made->NodePosX = Stood->NodePosX;
		Made->NodePosY = Stood->NodePosY;

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

		for (UEdGraphPin* Was : Stood->Pins) {
			if (Was == nullptr || Was->Direction != EGPD_Output || Was->LinkedTo.Num() == 0) continue;

			UEdGraphPin* Now = Made->FindPin(Was->PinName, EGPD_Output);

			if (Now == nullptr) continue;

			for (UEdGraphPin* Next : TArray<UEdGraphPin*>(Was->LinkedTo)) Schema->TryCreateConnection(Now, Next);
		}

		Graph->RemoveNode(Stood);
	}
}

int32 IBlueprintImporter::ConstructAnimationEvents() {
	UEdGraph* EventGraph = FBlueprintGraphs::Events(Blueprint);

	if (EventGraph == nullptr || !GetAssetDataAsValue().Has(TEXT("DynamicBindingObjects"))) return 0;

	const UWidgetBlueprint* Owns = Cast<UWidgetBlueprint>(Blueprint);
	const UEnum* Actions = StaticEnum<EWidgetAnimationEvent>();

	int32 Put = 0;

	GetContainer()->ExportsLoop(GetAssetDataAsValue().GetArray(TEXT("DynamicBindingObjects")), [&](FUObjectExport* Binding) {
		if (Binding->GetType() != TEXT("WidgetAnimationDelegateBinding")) return;

		const FUObjectJsonValueExport Properties = Binding->GetPropertiesAsValue();

		if (!Properties.Has(TEXT("WidgetAnimationDelegateBindings"))) return;

		for (const FUObjectJsonValueExport& One : Properties.GetArray(TEXT("WidgetAnimationDelegateBindings"))) {
			const FString Called = One.GetString(TEXT("FunctionNameToBind"));

			if (Called.IsEmpty()) continue;

			UK2Node_CustomEvent* Stood = EventCalled(EventGraph, Called);

			if (Stood == nullptr) continue;

			UK2Node_WidgetAnimationEvent* Runs = NewObject<UK2Node_WidgetAnimationEvent>(EventGraph, UK2Node_WidgetAnimationEvent::StaticClass(), NAME_None, RF_Transactional);

			EventGraph->AddNode(Runs, false, false);

			Runs->CreateNewGuid();
			Runs->PostPlacedNewNode();

			Runs->AnimationPropertyName = FName(*One.GetString(TEXT("AnimationToBind")));
			Runs->UserTag = FName(*One.GetString(TEXT("UserTag")));
			Runs->SourceWidgetBlueprint = Owns;
			Runs->CustomFunctionName = FName(*Called);

			if (FString Says = One.GetString(TEXT("Action")); !Says.IsEmpty() && Actions != nullptr) {
				if (Says.Contains(TEXT("::"))) Says.Split(TEXT("::"), nullptr, &Says);

				if (const int64 Which = Actions->GetValueByNameString(Says); Which != INDEX_NONE) {
					Runs->Action = static_cast<EWidgetAnimationEvent>(Which);
				}
			}

			Runs->AllocateDefaultPins();

			StandsFor(EventGraph, Runs, Stood);

			Put++;
		}
	});

	if (Put > 0) {
		UE_LOG(LogReflection, Display, TEXT("\"%s\" had %d event(s) that one of its animations runs"), *GetAssetName(), Put);
	}

	return Put;
}

int32 IBlueprintImporter::ConstructComponentEvents() {
	UEdGraph* EventGraph = FBlueprintGraphs::Events(Blueprint);
	UClass* Holds = Blueprint != nullptr ? Blueprint->GeneratedClass : nullptr;

	if (EventGraph == nullptr || Holds == nullptr || !GetAssetDataAsValue().Has(TEXT("DynamicBindingObjects"))) return 0;

	int32 Put = 0;

	GetContainer()->ExportsLoop(GetAssetDataAsValue().GetArray(TEXT("DynamicBindingObjects")), [&](FUObjectExport* Binding) {
		if (Binding->GetType() != TEXT("ComponentDelegateBinding")) return;

		const FUObjectJsonValueExport Properties = Binding->GetPropertiesAsValue();

		if (!Properties.Has(TEXT("ComponentDelegateBindings"))) return;

		for (const FUObjectJsonValueExport& One : Properties.GetArray(TEXT("ComponentDelegateBindings"))) {
			const FString Called = One.GetString(TEXT("FunctionNameToBind"));

			if (Called.IsEmpty()) continue;

			UK2Node_CustomEvent* Stood = EventCalled(EventGraph, Called);

			if (Stood == nullptr) continue;

			const FString On = One.GetString(TEXT("ComponentPropertyName"));
			const FString Delegate = One.GetString(TEXT("DelegatePropertyName"));

			const FObjectProperty* Binds = FindFProperty<FObjectProperty>(Holds, *On);

			const FMulticastDelegateProperty* Runs = Binds != nullptr && Binds->PropertyClass != nullptr
				? FindFProperty<FMulticastDelegateProperty>(Binds->PropertyClass, *Delegate)
				: nullptr;

			if (Binds == nullptr || Runs == nullptr) {
				FImportIssues::Report(
					EImportIssue::MissingClass,
					FString::Printf(TEXT("\"%s\" could not be bound"), *Called),
					FString::Printf(TEXT("The class binds it on \"%s\" to \"%s\", and this build has no %s of that name. The event was left as a plain one, so what it runs is still drawn, and nothing runs it."),
						*On, *Delegate, Binds == nullptr ? TEXT("variable") : TEXT("delegate"))
				);

				continue;
			}

			UK2Node_ComponentBoundEvent* Made = NewObject<UK2Node_ComponentBoundEvent>(EventGraph, UK2Node_ComponentBoundEvent::StaticClass(), NAME_None, RF_Transactional);

			EventGraph->AddNode(Made, false, false);

			Made->CreateNewGuid();
			Made->PostPlacedNewNode();

			Made->InitializeComponentBoundEventParams(Binds, Runs);

			Made->CustomFunctionName = FName(*Called);

			Made->AllocateDefaultPins();

			StandsFor(EventGraph, Made, Stood);

			Put++;
		}
	});

	if (Put > 0) {
		UE_LOG(LogReflection, Display, TEXT("\"%s\" had %d event(s) that one of its own widgets or components runs"), *GetAssetName(), Put);
	}

	return Put;
}

int32 IBlueprintImporter::ConstructOverriddenComponents() {
	UBlueprintGeneratedClass* Made = Cast<UBlueprintGeneratedClass>(Blueprint != nullptr ? Blueprint->GeneratedClass : nullptr);

	if (Made == nullptr || !GetAssetDataAsValue().Has(TEXT("InheritableComponentHandler"))) return 0;

	FUObjectExport* Held = GetContainer()->GetExportByObjectPath(GetAssetDataAsValue().GetObject(TEXT("InheritableComponentHandler")));

	if (Held == nullptr || !Held->IsJsonValid()) return 0;

	const FUObjectJsonValueExport Kept = Held->GetPropertiesAsValue();

	if (!Kept.Has(TEXT("Records"))) return 0;

	UInheritableComponentHandler* Handler = Made->GetInheritableComponentHandler(true);

	if (Handler == nullptr) return 0;

	int32 Put = 0;

	for (const FUObjectJsonValueExport& One : Kept.GetArray(TEXT("Records"))) {
		const FString Named = One.GetObject(TEXT("ComponentKey")).GetString(TEXT("SCSVariableName"));

		if (Named.IsEmpty()) continue;

		USCS_Node* Node = nullptr;

		for (UClass* At = Made->GetSuperClass(); At != nullptr && Node == nullptr; At = At->GetSuperClass()) {
			const UBlueprintGeneratedClass* Above = Cast<UBlueprintGeneratedClass>(At);

			if (Above == nullptr || Above->SimpleConstructionScript == nullptr) continue;

			Node = Above->SimpleConstructionScript->FindSCSNode(FName(*Named));
		}

		if (Node == nullptr) {
			FImportIssues::Report(
				EImportIssue::Data,
				FString::Printf(TEXT("\"%s\" could not be overridden"), *Named),
				TEXT("The class says it changes something about a component it inherited, and no class above it keeps a component of that name. What was changed is left at whatever the parent had.")
			);

			continue;
		}

		const FComponentKey Spot(Node);

		UActorComponent* Override = Handler->GetOverridenComponentTemplate(Spot);

		if (Override == nullptr) {
			Override = Handler->CreateOverridenComponentTemplate(Spot);
		}

		if (Override == nullptr) continue;

		FUObjectExport* Says = GetContainer()->GetExportByObjectPath(One.GetObject(TEXT("ComponentTemplate")));

		if (Says == nullptr || !Says->IsJsonValid()) continue;

		Says->Object = Override;

		GetObjectSerializer()->DeserializeObjectProperties(Says->GetProperties(), Override);

		Put++;
	}

	if (Put > 0) {
		UE_LOG(LogReflection, Display, TEXT("\"%s\" changes %d thing(s) about a component it inherited"), *GetAssetName(), Put);
	}

	return Put;
}

void IBlueprintImporter::ConstructWidgetTree() {
	if (!GetAssetDataAsValue().Has("WidgetTree")) return;

	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);

	if (WidgetBlueprint == nullptr || WidgetBlueprint->WidgetTree == nullptr) {
		FImportIssues::Report(
			EImportIssue::Data,
			FString::Printf(TEXT("\"%s\" has widgets and is not a widget blueprint"), *GetAssetName()),
			FString::Printf(
				TEXT("The export describes a tree of widgets, and what was made for '%s' is a plain blueprint with nowhere to keep one. That is the class it says it comes from having read back as something that is not a widget. Everything else about it was imported and the widgets were left out."),
				*GetAssetName()
			)
		);

		return;
	}

	for (UWidget* Widget : Cast<UWidgetTreeAccessor>(WidgetBlueprint->WidgetTree)->GetWidgets()) {
		MoveToTransientPackageAndRename(Widget);
	}

	WidgetBlueprint->WidgetTree->PostLoad();

	for (UWidgetAnimation* WidgetAnimation : WidgetBlueprint->Animations) {
		MoveToTransientPackageAndRename(WidgetAnimation);
	}

	WidgetBlueprint->Animations.Empty();

	FUObjectExport* ClassDefaultObjectExport = GetClassDefaultObject(GetContainer(), GetAssetDataAsValue());

	if (ClassDefaultObjectExport->IsJsonValid()) {
		ClassDefaultObjectExport->Object = WidgetBlueprint;
	}

	SetAsset(WidgetBlueprint);

	MoveToTransientPackageAndRename(WidgetBlueprint->WidgetTree->RootWidget);
	WidgetBlueprint->WidgetTree->RootWidget = nullptr;

	FUObjectExport* Export;

	if (GetAssetDataAsValue().Has("TemplateAsset")) {
		FUObjectExport* TemplateAsset = GetContainer()->GetExportByObjectPath(GetAssetDataAsValue().GetObject("TemplateAsset"));
		Export = GetContainer()->GetExportByObjectPath(TemplateAsset->GetPropertiesAsValue().GetObject("WidgetTree"));
	} else {
		Export = GetContainer()->GetExportByObjectPath(GetAssetDataAsValue().GetObject("WidgetTree"));
	}

	Export->Object = WidgetBlueprint->WidgetTree;
	GetObjectSerializer()->SpawnExport(Export, true);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	GetContainer()->ExportsLoop(GetAssetDataAsValue().GetArray("Animations"), [this, WidgetBlueprint](FUObjectExport* DirectExport) {
		if (UObject* Object = GetObjectSerializer()->SpawnExport(DirectExport)) {
			UWidgetAnimation* WidgetAnimation = Cast<UWidgetAnimation>(Object);

			if (WidgetAnimation == nullptr || WidgetAnimation->MovieScene == nullptr) {
				FImportIssues::Report(
					EImportIssue::Data,
					FString::Printf(TEXT("\"%s\" is not an animation this can read"), *DirectExport->GetName().ToString()),
					FString::Printf(
						TEXT("The export named among the widget's animations came back as %s, and an animation is read through the movie scene it keeps. It was left out and the rest of the widget was imported."),
						WidgetAnimation == nullptr ? TEXT("something that is not an animation") : TEXT("an animation with no movie scene"))
				);

				return;
			}

			WidgetBlueprint->Animations.Add(WidgetAnimation);

			{
				UMovieScene* SortedMovieScene = WidgetAnimation->MovieScene;
				UClass* MovieSceneClass = UMovieScene::StaticClass();

				if (const FArrayProperty* PossessablesProperty = FindFProperty<FArrayProperty>(MovieSceneClass, TEXT("Possessables"))) {
					PossessablesProperty->ContainerPtrToValuePtr<TArray<FMovieScenePossessable>>(SortedMovieScene)->Sort(
						[](const FMovieScenePossessable& Left, const FMovieScenePossessable& Right) { return Left.GetGuid() < Right.GetGuid(); });
				}

				if (const FArrayProperty* SpawnablesProperty = FindFProperty<FArrayProperty>(MovieSceneClass, TEXT("Spawnables"))) {
					SpawnablesProperty->ContainerPtrToValuePtr<TArray<FMovieSceneSpawnable>>(SortedMovieScene)->Sort(
						[](const FMovieSceneSpawnable& Left, const FMovieSceneSpawnable& Right) { return Left.GetGuid() < Right.GetGuid(); });
				}

				if (const FArrayProperty* BindingsProperty = FindFProperty<FArrayProperty>(MovieSceneClass, TEXT("ObjectBindings"))) {
					BindingsProperty->ContainerPtrToValuePtr<TArray<FMovieSceneBinding>>(SortedMovieScene)->Sort(
						[](const FMovieSceneBinding& Left, const FMovieSceneBinding& Right) { return Left.GetObjectGuid() < Right.GetObjectGuid(); });
				}
			}

			for (int32 Index = 0; Index < WidgetAnimation->MovieScene->GetPossessableCount(); ++Index) {
				FMovieScenePossessable& Possessable = WidgetAnimation->MovieScene->GetPossessable(Index);

				TArray<UWidget*> Widgets;
				WidgetBlueprint->WidgetTree->GetAllWidgets(Widgets);

				for (UWidget* Widget : Widgets) {
					if (Widget->GetName() == Possessable.GetName()) {
#if ENGINE_UE5
						Possessable.SetPossessedObjectClass(Widget->GetClass());
#endif
					}
				}
			}

			const UMovieScene* MovieScene = WidgetAnimation->MovieScene;
			for (const FMovieSceneBinding& Binding : MovieScene->GetBindings()) {
				for (UMovieSceneTrack* Track : Binding.GetTracks()) {
					if (!Track)
					{
						continue;
					}

					Track->Modify();
					Track->MarkAsChanged();

					if (UMovieSceneWidgetMaterialTrack* MaterialTrack = Cast<UMovieSceneWidgetMaterialTrack>(Track)) {
						MaterialTrack->SetDisplayName(FText::FromString(MaterialTrack->GetBrushPropertyNamePath()[0].ToString()));
					}
				}
			}
		}
	});

	WidgetBlueprint->Bindings.Empty();

	for (const FUObjectJsonValueExport& Entry : GetAssetDataAsValue().GetArray("Bindings")) {
		const TSharedPtr<FJsonObject> Held = Entry.JsonObject;
		if (!Held.IsValid()) continue;

		FDelegateEditorBinding Binding;
		Binding.ObjectName = Held->GetStringField(TEXT("ObjectName"));
		Binding.PropertyName = FName(*Held->GetStringField(TEXT("PropertyName")));
		Binding.FunctionName = FName(*Held->GetStringField(TEXT("FunctionName")));

		FString Kind;
		Held->TryGetStringField(TEXT("Kind"), Kind);
		Binding.Kind = Kind.EndsWith(TEXT("Property")) ? EBindingKind::Property : EBindingKind::Function;

		WidgetBlueprint->Bindings.Add(Binding);
	}
}
namespace {
	void Bound(const TSharedPtr<FJsonValue>& Value, TSet<FString>& Into) {
		if (!Value.IsValid()) return;

		if (Value->Type == EJson::Object) {
			const TSharedPtr<FJsonObject> Object = Value->AsObject();

			FString Named;

			if (Object->TryGetStringField(TEXT("BoundFunction"), Named) && !Named.IsEmpty() && Named != TEXT("None")) {
				Into.Add(Named);
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values) Bound(Field.Value, Into);
		} else if (Value->Type == EJson::Array) {
			for (const TSharedPtr<FJsonValue>& Held : Value->AsArray()) Bound(Held, Into);
		}
	}

	bool Poses(const TArray<TSharedPtr<FJsonValue>>& Declared) {
		for (const TSharedPtr<FJsonValue>& Value : Declared) {
			const TSharedPtr<FJsonObject> Property = Value.IsValid() ? Value->AsObject() : nullptr;

			if (!Property.IsValid()) continue;

			const TSharedPtr<FJsonObject>* Struct;

			if (!Property->TryGetObjectField(TEXT("Struct"), Struct)) continue;

			FString Named;

			if ((*Struct)->TryGetStringField(TEXT("ObjectName"), Named) && Named.EndsWith(TEXT("PoseLink'"))) return true;
		}

		return false;
	}
}

void IBlueprintImporter::Answers(const FString& Owner, const FName Member, UEdGraphPin* Pin) {
	if (Owner.IsEmpty() || Pin == nullptr) return;

	Decides.Add(FDecided{ Owner, Member, Pin });
}

int32 IBlueprintImporter::ConstructGraphs() {
	if (Blueprint == nullptr) return 0;

	int32 Placed = 0;

	int32 Changed = 0;

	TArray<TSharedPtr<FBytecodeGraph>> Builders;

	bool bSignatures = false;

	TSharedPtr<FBytecodeGraph> Ubergraph;
	TArray<TPair<FString, FBlueprintGraphs::FWritten>> Events;

	TArray<int32> Handlers;

	TArray<FUObjectJsonValueExport> Ubergraphed;

	TArray<TTuple<int32, TWeakObjectPtr<UK2Node>, FName>> Resumed;
	TMap<FString, TArray<TSharedPtr<FJsonValue>>> Signatures;

	TMap<FString, FString> Said;

	const TMap<FString, FString> Contexts = FBlueprintCookedMetaData::WorldContexts(GetContainer());

	TSet<FString> Compilers;

	for (const TSharedPtr<FJsonValue>& Value : GetContainer()->JsonObjects) {
		Bound(Value, Compilers);
	}

	for (FUObjectExport* Export : GetContainer()->Exports) {
		if (Export == nullptr || !Export->IsJsonValid()) continue;
		if (Export->GetType() != TEXT("Function")) continue;
		if (!Export->Has(TEXT("ScriptBytecode"))) continue;

		const FString Name = Export->GetName().ToString();

		if (Name.StartsWith(TEXT("EvaluateGraphExposedInputs"))) {
			for (const FUObjectJsonValueExport& One : Export->GetArray(TEXT("ScriptBytecode"))) {
				if (!One.Has(TEXT("Function")) || !One.Has(TEXT("Parameters"))) continue;

				FString Calls;

				if (One.JsonObject.IsValid() && One.JsonObject->HasTypedField<EJson::String>(TEXT("Function"))) {
					Calls = One.GetString(TEXT("Function"));
				} else {
					Calls = One.GetObject(TEXT("Function")).GetString(TEXT("ObjectName"));
				}

				if (!Calls.Contains(UEdGraphSchema_K2::FN_ExecuteUbergraphBase.ToString())) continue;

				for (const FUObjectJsonValueExport& Parameter : One.GetArray(TEXT("Parameters"))) {
					if (Parameter.GetString(TEXT("Token")) != TEXT("EX_IntConst")) continue;

					Handlers.AddUnique(Parameter.GetInteger(TEXT("Value"), INDEX_NONE));
				}
			}

			continue;
		}

		if (Compilers.Contains(Name)) continue;

		const FUObjectJsonValueExport Function(Export->JsonObject);

		if (Name.EndsWith(TEXT("__DelegateSignature")) && FBlueprintGraphs::Reads(Function).Kind != FBlueprintGraphs::EWritten::Event) continue;

		TArray<TSharedPtr<FJsonValue>> Declared = FBlueprintVariables::GetDeclared(Export->JsonObject, GetContainer());

		TArray<FUObjectJsonValueExport> Locals;

		for (const TSharedPtr<FJsonValue>& Local : Declared) {
			Locals.Add(FUObjectJsonValueExport(Local));
		}

		if (Poses(Declared)) {
			if (Blueprint->BlueprintType != BPTYPE_Interface) continue;

			bool bHeld = false;

			for (const UEdGraph* Held : Blueprint->FunctionGraphs) {
				if (Held != nullptr && Held->GetName() == Name) bHeld = true;
			}

			if (!bHeld && FBlueprintGraphs::MakeLayer(Blueprint, Name, Declared) != nullptr) {
				UE_LOG(LogReflection, Display, TEXT("layer \"%s\" declared"), *Name);

				Changed++;
			}

			continue;
		}

		FString Handing = Contexts.FindRef(Name);

		if (Handing.IsEmpty() && Function.GetString(TEXT("FunctionFlags")).Contains(TEXT("FUNC_Static"))) {
			Handing = TEXT("__WorldContext");
		}

		if (const FString* Handed = Handing.IsEmpty() ? nullptr : &Handing) {
			for (int32 At = Declared.Num() - 1; At >= 0; --At) {
				const TSharedPtr<FJsonObject> Property = Declared[At].IsValid() ? Declared[At]->AsObject() : nullptr;

				FString Called;

				if (Property.IsValid() && Property->TryGetStringField(TEXT("Name"), Called) && Called == *Handed) {
					Declared.RemoveAt(At);

					UE_LOG(LogReflection, Display, TEXT("\"%s\" is handed the world through \"%s\", which its entry node asks for itself"), *Name, **Handed);
				}
			}
		}

		const FBlueprintGraphs::FWritten Written = FBlueprintGraphs::Reads(Function);

		if (const TPair<TWeakObjectPtr<UK2Node>, FName>* Resuming = Resumes.Find(FName(*Name))) {
			if (Written.Kind == FBlueprintGraphs::EWritten::Event && Resuming->Key.IsValid()) {
				Resumed.Emplace(Written.EntryPoint, Resuming->Key, Resuming->Value);
			}

			continue;
		}

		if (Written.Kind == FBlueprintGraphs::EWritten::Event) {
			Signatures.Add(Name, Declared);
			Said.Add(Name, Function.GetString(TEXT("FunctionFlags")));
			Events.Add(TPair<FString, FBlueprintGraphs::FWritten>(Name, Written));

			continue;
		}

		UEdGraph* Graph = nullptr;

		if (Written.Kind == FBlueprintGraphs::EWritten::Ubergraph) {
			Graph = FBlueprintGraphs::Events(Blueprint);
		} else {
			TArray<UEdGraph*> Graphs;
			Blueprint->GetAllGraphs(Graphs);

			for (UEdGraph* Candidate : Graphs) {
				if (Candidate != nullptr && Candidate->GetName() == Name) {
					Graph = Candidate;

					break;
				}
			}

			if (Graph != nullptr && Graph->GetSchema() != nullptr && Graph->GetSchema()->GetClass() != UEdGraphSchema_K2::StaticClass()) {
				continue;
			}

			if (Graph == nullptr) {
				Graph = FBlueprintGraphs::Make(Blueprint, Name, Function, Declared);

				bSignatures = bSignatures || Graph != nullptr;

				Changed++;
			}
		}

		if (Graph == nullptr) {
			FImportIssues::Report(
				EImportIssue::MissingClass,
				TEXT("No graph for a function the class carries"),
				FString::Printf(TEXT("'%s' was cooked with bytecode, and no graph could be made to lay it back out in."), *Name)
			);

			continue;
		}

		TSharedPtr<FBytecodeGraph> Made = MakeShared<FBytecodeGraph>(
			Graph,
			Export->GetArray(TEXT("ScriptBytecode")),
			Locals,
			GetContainer()
		);

		Made->SetObjectSerializer(GetObjectSerializer());

		for (const TPair<FString, TPair<TWeakObjectPtr<UK2Node>, FName>>& Handout : Handouts) {
			if (Handout.Value.Key.IsValid()) {
				Made->HandOverTrack(Handout.Key, Handout.Value.Key.Get(), Handout.Value.Value);
			}
		}

		if (Written.Kind == FBlueprintGraphs::EWritten::Ubergraph) {
			Ubergraph = Made;
			Ubergraphed = Export->GetArray(TEXT("ScriptBytecode"));

			for (const FUObjectJsonValueExport& Statement : Ubergraphed) {
				AssetUbergraphStatements.Add(Statement.GetInteger(TEXT("StatementIndex"), INDEX_NONE));
			}
		}

		Builders.Add(Made);
	}

	TArray<TSharedPtr<FBytecodeGraph>> Rules;

	UE_LOG(LogReflection, Display, TEXT("%d stretch(es) of the ubergraph are entered by a handler, and %d node member(s) are answered through a pin"), Handlers.Num(), Decides.Num());

	if (Ubergraph.IsValid() && Handlers.Num() > 0 && Decides.Num() > 0) {
		for (const int32 From : Handlers) {
			if (From < 0) continue;

			int32 At = INDEX_NONE;

			for (int32 Index = 0; Index < Ubergraphed.Num(); ++Index) {
				if (Ubergraphed[Index].GetInteger(TEXT("StatementIndex"), INDEX_NONE) == From) {
					At = Index;

					break;
				}
			}

			if (At == INDEX_NONE) continue;

			int32 To = INDEX_NONE;

			const FDecided* Into = nullptr;

			for (int32 Index = At; Index < Ubergraphed.Num(); ++Index) {
				const FUObjectJsonValueExport& One = Ubergraphed[Index];

				const FString Token = One.GetString(TEXT("Token"));

				if (Token == TEXT("EX_Jump") || Token == TEXT("EX_JumpIfNot") || Token == TEXT("EX_ComputedJump")
					|| Token == TEXT("EX_PushExecutionFlow") || Token == TEXT("EX_PopExecutionFlow")
					|| Token == TEXT("EX_PopExecutionFlowIfNot")) {
					break;
				}

				const FUObjectJsonValueExport Variable = One.GetObject(TEXT("Variable"));

				if (Variable.GetString(TEXT("Token")) == TEXT("EX_StructMemberContext")) {
					FString Owner, Member;

					ReadStructMemberContext(Variable, Owner, Member);

					for (const FDecided& Says : Decides) {
						if (Says.Owner == Owner && Says.Member == FName(*Member)) {
							Into = &Says;

							To = One.GetInteger(TEXT("StatementIndex"), INDEX_NONE);

							break;
						}
					}
				}

				if (Into != nullptr) break;

				if (Token == TEXT("EX_Return") || Token == TEXT("EX_EndOfScript")) break;
			}

			if (To == INDEX_NONE || To < From) continue;

			if (Into == nullptr || Into->Pin == nullptr || Into->Pin->GetOwningNode() == nullptr) continue;

			UEdGraph* Rule = Into->Pin->GetOwningNode()->GetGraph();

			if (Rule == nullptr) continue;

			TSharedPtr<FBytecodeGraph> Made = MakeShared<FBytecodeGraph>(
				Rule,
				Ubergraphed,
				TArray<FUObjectJsonValueExport>(),
				GetContainer()
			);

			Made->SetObjectSerializer(GetObjectSerializer());

			Made->Only(From, To);
			Made->HandsInto(Into->Owner, Into->Member, Into->Pin);

			Ubergraph->LeaveOut(From, To);

			Rules.Add(Made);

			UE_LOG(LogReflection, Display, TEXT("%d..%d decides '%s.%s', so it is laid out in '%s' rather than among the events"),
				From, To, *Into->Owner, *Into->Member.ToString(), *Rule->GetName());
		}
	}

	if (Ubergraph.IsValid() && Handlers.Num() > 0 && Ubergraphed.Num() > 0) {
		TMap<int32, int32> Where;

		for (int32 Index = 0; Index < Ubergraphed.Num(); ++Index) {
			Where.Add(Ubergraphed[Index].GetInteger(TEXT("StatementIndex"), INDEX_NONE), Index);
		}

		const auto Follow = [&Where, &Ubergraphed](const int32 From, TSet<int32>& Into) {
			TArray<int32> Waiting;
			Waiting.Add(From);

			while (Waiting.Num() > 0) {
				int32 At = Waiting.Pop();

				while (const int32* Index = Where.Find(At)) {
					const FUObjectJsonValueExport& One = Ubergraphed[*Index];

					if (Into.Contains(At)) break;

					Into.Add(At);

					const FString Token = One.GetString(TEXT("Token"));

					if (Token == TEXT("EX_Jump")) {
						At = One.GetInteger(TEXT("CodeOffset"), INDEX_NONE);

						continue;
					}

					if (Token == TEXT("EX_JumpIfNot")) {
						Waiting.Add(One.GetInteger(TEXT("CodeOffset"), INDEX_NONE));
					} else if (Token == TEXT("EX_PushExecutionFlow")) {
						Waiting.Add(One.GetInteger(TEXT("PushingAddress"), INDEX_NONE));
					} else if (Token == TEXT("EX_Return") || Token == TEXT("EX_EndOfScript")
						|| Token == TEXT("EX_PopExecutionFlow") || Token == TEXT("EX_ComputedJump")) {
						break;
					}

					if (!Ubergraphed.IsValidIndex(*Index + 1)) break;

					At = Ubergraphed[*Index + 1].GetInteger(TEXT("StatementIndex"), INDEX_NONE);
				}
			}
		};

		TSet<int32> Written;
		TSet<int32> Worked;

		for (const TPair<FString, FBlueprintGraphs::FWritten>& Event : Events) {
			Follow(Event.Value.EntryPoint, Written);
		}

		for (const TTuple<int32, TWeakObjectPtr<UK2Node>, FName>& Resuming : Resumed) {
			Follow(Resuming.Get<0>(), Written);
		}

		for (const int32 One : Handlers) {
			Follow(One, Worked);
		}

		int32 Left = 0;

		for (const int32 One : Worked) {
			if (Written.Contains(One)) continue;

			Ubergraph->LeaveOutAt(One);

			Left++;
		}

		UE_LOG(LogReflection, Display, TEXT("%d statement(s) of the ubergraph are the compiler's own working out, and are left out of the events"), Left);
	}

	for (const TSharedPtr<FJsonValue>& Value : FBlueprintVariables::GetDeclared(GetAssetExport(), GetContainer())) {
		const TSharedPtr<FJsonObject> Property = Value.IsValid() ? Value->AsObject() : nullptr;

		if (!Property.IsValid()) continue;

		FString Kind;

		if (!Property->TryGetStringField(TEXT("Type"), Kind)) continue;
		if (!Kind.StartsWith(TEXT("Multicast")) || !Kind.EndsWith(TEXT("DelegateProperty"))) continue;

		FString Named;

		if (!Property->TryGetStringField(TEXT("Name"), Named) || Named.IsEmpty()) continue;

		FUObjectExport* Signature = GetContainer()->Find(FName(*(Named + TEXT("__DelegateSignature"))));

		const TArray<TSharedPtr<FJsonValue>> Hands = Signature != nullptr && Signature->IsJsonValid()
			? FBlueprintVariables::GetDeclared(Signature->JsonObject, GetContainer())
			: TArray<TSharedPtr<FJsonValue>>();

		const bool bMade = FBlueprintGraphs::MakeDispatcher(Blueprint, Named, Hands) != nullptr;

		UE_LOG(LogReflection, Display, TEXT("dispatcher \"%s\": signature %s, %d handed over, made %s"),
			*Named,
			Signature != nullptr && Signature->IsJsonValid() ? TEXT("found") : TEXT("MISSING"),
			Hands.Num(),
			bMade ? TEXT("yes") : TEXT("NO"));

		if (bMade) bSignatures = true;
	}

	if (Ubergraph.IsValid()) {
		UEdGraph* EventGraph = FBlueprintGraphs::Events(Blueprint);

		TSet<FString> Written;
		TArray<TPair<int32, UEdGraphNode*>> EnteredInOrder;

		for (const TPair<FString, FBlueprintGraphs::FWritten>& Event : Events) {
			UK2Node* Node = FBlueprintGraphs::MakeEvent(Blueprint, EventGraph, Event.Key, Signatures.FindRef(Event.Key));

			bSignatures = bSignatures || Node != nullptr;

			if (Node == nullptr) continue;

			EnteredInOrder.Add(TPair<int32, UEdGraphNode*>(Event.Value.EntryPoint, Node));

			if (UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(Node)) {
				const FString& Flags = Said.FindRef(Event.Key);

				const TPair<const TCHAR*, EFunctionFlags> Marks[] = {
					TPair<const TCHAR*, EFunctionFlags>(TEXT("FUNC_NetMulticast"), FUNC_NetMulticast),
					TPair<const TCHAR*, EFunctionFlags>(TEXT("FUNC_NetServer"), FUNC_NetServer),
					TPair<const TCHAR*, EFunctionFlags>(TEXT("FUNC_NetClient"), FUNC_NetClient),
					TPair<const TCHAR*, EFunctionFlags>(TEXT("FUNC_NetReliable"), FUNC_NetReliable),
				};

				for (const TPair<const TCHAR*, EFunctionFlags>& Mark : Marks) {
					if (Flags.Contains(Mark.Key)) Custom->FunctionFlags |= Mark.Value;
				}

				if (Custom->FunctionFlags & FUNC_NetFuncFlags) Custom->FunctionFlags |= FUNC_Net;
			}

			Written.Add(Event.Key);

			Changed++;

			Ubergraph->EnterAt(Event.Value.EntryPoint, Node);
			Ubergraph->SitsAt(Event.Value.EntryPoint, Node);
			AssetEntries.Add(FName(*Event.Key), Event.Value.EntryPoint);

			for (const TPair<FString, FString>& Given : Event.Value.Frame) {
				Ubergraph->HandOver(Given.Key, Given.Value, Node);
			}
		}

		if (EventGraph != nullptr) {
			EnteredInOrder.StableSort([](const TPair<int32, UEdGraphNode*>& One, const TPair<int32, UEdGraphNode*>& Other) {
				return One.Key < Other.Key;
			});

			for (int32 Index = EnteredInOrder.Num() - 1; Index >= 0; --Index) {
				UEdGraphNode* Node = EnteredInOrder[Index].Value;

				if (EventGraph->Nodes.Remove(Node) > 0) {
					EventGraph->Nodes.Insert(Node, 0);
				}
			}
		}

		for (const TTuple<int32, TWeakObjectPtr<UK2Node>, FName>& Resuming : Resumed) {
			if (!Resuming.Get<1>().IsValid()) continue;

			Ubergraph->EnterAt(Resuming.Get<0>(), Resuming.Get<1>().Get(), Resuming.Get<2>());

			Changed++;
		}

		Changed += FBlueprintGraphs::RemoveUnwrittenEvents(EventGraph, Written);
	} else if (Events.Num() > 0) {
		FImportIssues::Report(
			EImportIssue::MissingClass,
			TEXT("Events with nowhere to lay them out"),
			FString::Printf(TEXT("%d event(s) were cooked with bytecode, and the class carries no ubergraph holding their bodies."), Events.Num())
		);
	}

	for (const TSharedPtr<FBytecodeGraph>& Builder : Builders) {
		Builder->Clear();
	}

	int32 Declared = 0;

	for (const TSharedPtr<FBytecodeGraph>& Builder : Builders) {
		Declared += Builder->DeclareLocals();
	}

	{
		TArray<UEdGraph*> Every;
		Blueprint->GetAllGraphs(Every);

		for (UEdGraph* One : Every) {
			if (One == nullptr || ExactCast<UAnimationGraphSchema>(One->GetSchema()) == nullptr) continue;

			TArray<UAnimGraphNode_Root*> Poses;
			One->GetNodesOfClass<UAnimGraphNode_Root>(Poses);

			if (Poses.Num() == 1) continue;

			if (Poses.Num() == 0) {
				One->GetSchema()->CreateDefaultNodesForGraph(*One);

				UE_LOG(LogReflection, Warning, TEXT("\"%s\" had no output pose, so it was given one"), *One->GetName());

				continue;
			}

			const auto IsFed = [](const UAnimGraphNode_Root* Pose) {
				for (const UEdGraphPin* Pin : Pose->Pins) {
					if (Pin != nullptr && Pin->LinkedTo.Num() > 0) return true;
				}

				return false;
			};

			int32 Kept = 0;

			for (int32 At = 1; At < Poses.Num(); ++At) {
				if (IsFed(Poses[At]) && !IsFed(Poses[Kept])) Kept = At;
			}

			for (int32 At = Poses.Num() - 1; At >= 0; --At) {
				if (At != Kept) One->RemoveNode(Poses[At]);
			}

			UE_LOG(LogReflection, Warning, TEXT("\"%s\" had %d output poses, so it was left with the one that answers"), *One->GetName(), Poses.Num());
		}
	}

	{
		TArray<UK2Node_FunctionEntry*> Entries;
		for (UEdGraph* Function : Blueprint->FunctionGraphs) {
			if (Function != nullptr) Function->GetNodesOfClass<UK2Node_FunctionEntry>(Entries);
		}
		for (UK2Node_FunctionEntry* Entry : Entries) {
			Entry->bEnforceConstCorrectness = false;
		}

		CompileBlueprintGuarded(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	}

	for (const TSharedPtr<FBytecodeGraph>& Builder : Builders) {
		Placed += Builder->Build();
	}

	for (const TSharedPtr<FBytecodeGraph>& Rule : Rules) {
		Placed += Rule->Build();
	}

	return Placed + Changed;
}

int32 IBlueprintImporter::ConstructTimelines() {
	if (Blueprint == nullptr) return 0;

	UEdGraph* Events = FBlueprintGraphs::Events(Blueprint);

	int32 Made = 0;

	for (FUObjectExport* Export : GetContainer()->Exports) {
		if (Export == nullptr || !Export->IsJsonValid()) continue;
		if (Export->GetType() != TEXT("TimelineTemplate")) continue;

		const FUObjectJsonValueExport Held(Export->GetProperties());

		const bool bNamesItself = Held.Has(TEXT("VariableName"));
		FString Called = bNamesItself ? Held.GetString(TEXT("VariableName")) : FString();

		if (Called.IsEmpty()) {
			Called = Export->GetName().ToString();
			Called.RemoveFromEnd(TEXT("_Template"));
		}

		if (Called.IsEmpty()) continue;

		UTimelineTemplate* Template = Blueprint->FindTimelineTemplateByVariableName(FName(*Called));

		if (Template == nullptr) {
			Template = FBlueprintEditorUtils::AddNewTimeline(Blueprint, FName(*Called));
		}

		if (Template == nullptr) continue;

		Export->Object = Template;

		for (FUObjectExport* Curve : GetContainer()->Exports) {
			if (Curve == nullptr || !Curve->IsJsonValid() || Curve->Object != nullptr) continue;
			if (!Curve->GetType().ToString().StartsWith(TEXT("Curve"))) continue;

			GetObjectSerializer()->SpawnExport(Curve);
		}

		GetObjectSerializer()->DeserializeObjectProperties(Export->GetProperties(), Template);

		if (!bNamesItself) {
			Template->PostEditImport();
		}

#if !UE4_25_BELOW
		if (Template->GetNumDisplayTracks() == 0) {
			for (int32 Index = 0; Index < Template->EventTracks.Num(); ++Index) {
				Template->AddDisplayTrack(FTTTrackId(FTTTrackBase::TT_Event, Index));
			}

			for (int32 Index = 0; Index < Template->FloatTracks.Num(); ++Index) {
				Template->AddDisplayTrack(FTTTrackId(FTTTrackBase::TT_FloatInterp, Index));
			}

			for (int32 Index = 0; Index < Template->VectorTracks.Num(); ++Index) {
				Template->AddDisplayTrack(FTTTrackId(FTTTrackBase::TT_VectorInterp, Index));
			}

			for (int32 Index = 0; Index < Template->LinearColorTracks.Num(); ++Index) {
				Template->AddDisplayTrack(FTTTrackId(FTTTrackBase::TT_LinearColorInterp, Index));
			}
		}
#endif

		if (Events != nullptr) {
			UK2Node_Timeline* Node = nullptr;

			for (UEdGraphNode* Sat : Events->Nodes) {
				if (UK2Node_Timeline* Already = Cast<UK2Node_Timeline>(Sat); Already != nullptr && Already->TimelineName == FName(*Called)) {
					Node = Already;

					break;
				}
			}

			if (Node == nullptr) {
				Node = NewObject<UK2Node_Timeline>(Events, UK2Node_Timeline::StaticClass(), NAME_None, RF_Transactional);

				Events->AddNode(Node, false, false);

				Node->CreateNewGuid();
				Node->PostPlacedNewNode();
			}

			Node->TimelineName = FName(*Called);
			Node->bAutoPlay = Template->bAutoPlay;
			Node->bLoop = Template->bLoop;
			Node->bReplicated = Template->bReplicated;
			Node->bIgnoreTimeDilation = Template->bIgnoreTimeDilation;
			Node->TimelineGuid = Template->TimelineGuid;

			Node->ReconstructNode();

			for (const FTTFloatTrack& Track : Template->FloatTracks) {
				Handouts.Add(Track.GetPropertyName().ToString(), TPair<TWeakObjectPtr<UK2Node>, FName>(Node, Track.GetTrackName()));
			}

			for (const FTTVectorTrack& Track : Template->VectorTracks) {
				Handouts.Add(Track.GetPropertyName().ToString(), TPair<TWeakObjectPtr<UK2Node>, FName>(Node, Track.GetTrackName()));
			}

			for (const FTTLinearColorTrack& Track : Template->LinearColorTracks) {
				Handouts.Add(Track.GetPropertyName().ToString(), TPair<TWeakObjectPtr<UK2Node>, FName>(Node, Track.GetTrackName()));
			}

			Handouts.Add(Template->GetDirectionPropertyName().ToString(), TPair<TWeakObjectPtr<UK2Node>, FName>(Node, TEXT("Direction")));

			if (const UEdGraphPin* Playing = Node->GetUpdatePin()) {
				Resumes.Add(Template->GetUpdateFunctionName(), TPair<TWeakObjectPtr<UK2Node>, FName>(Node, Playing->PinName));
			}

			if (const UEdGraphPin* Done = Node->GetFinishedPin()) {
				Resumes.Add(Template->GetFinishedFunctionName(), TPair<TWeakObjectPtr<UK2Node>, FName>(Node, Done->PinName));
			}

			for (const FTTEventTrack& Track : Template->EventTracks) {
				Resumes.Add(Track.GetFunctionName(), TPair<TWeakObjectPtr<UK2Node>, FName>(Node, Track.GetTrackName()));
			}
		}

		Made++;

		UE_LOG(LogReflection, Display, TEXT("timeline \"%s\": %d float track(s), %g long"), *Called, Template->FloatTracks.Num(), Template->TimelineLength);
	}

	if (Made > 0) {
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	return Made;
}
