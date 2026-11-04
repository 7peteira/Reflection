#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/GraphTidy.h"

#include "BlueprintActionDatabase.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_AddDelegate.h"
#if __has_include("K2Node_AsyncAction.h")
#include "K2Node_AsyncAction.h"
#else
#include "Nodes/K2Node_AsyncAction.h"
#endif
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionTidyAsyncTask, All, All);

namespace {
	void TakeOverAsync(UEdGraphPin* From, UEdGraphPin* To) {
		if (From == nullptr || To == nullptr) return;

		for (UEdGraphPin* Held : From->LinkedTo) {
			if (Held != nullptr) To->MakeLinkTo(Held);
		}
	}

	bool ReadsAsync(const UEdGraphNode* Node, UEdGraphPin* Made, TArray<UK2Node*>& Through, const int32 Removes = 2) {
		if (Node == nullptr || Made == nullptr || Removes < 0) return false;

		for (const UEdGraphPin* Pin : Node->Pins) {
			if (Pin == nullptr || Pin->Direction != EGPD_Input) continue;

			for (UEdGraphPin* From : Pin->LinkedTo) {
				if (From == Made) return true;

				UK2Node* Works = From != nullptr ? Cast<UK2Node>(From->GetOwningNode()) : nullptr;

				if (Works == nullptr || !Works->IsNodePure()) continue;

				if (ReadsAsync(Works, Made, Through, Removes - 1)) {
					Through.AddUnique(Works);

					return true;
				}
			}
		}

		return false;
	}

	FGuid NodeNamedBy(const FName Handler) {
		const FString Name = Handler.ToString();

		int32 Under = INDEX_NONE;

		if (!Name.FindLastChar(TEXT('_'), Under)) return FGuid();

		FGuid Written;

		if (!FGuid::ParseExact(Name.Mid(Under + 1), EGuidFormats::Digits, Written)) return FGuid();

		uint32 Table[256];

		for (uint32 Index = 0; Index < 256; ++Index) {
			uint32 Entry = Index;

			for (int32 Bit = 0; Bit < 8; ++Bit) {
				Entry = (Entry & 1) ? (Entry >> 1) ^ 0xEDB88320u : Entry >> 1;
			}

			Table[Index] = Entry;
		}

		const uint32 Kept[3] = { Written.A, Written.B, Written.C };
		const uint8* KeptBytes = reinterpret_cast<const uint8*>(Kept);

		uint32 Register = 0xFFFFFFFFu;

		for (int32 Index = 0; Index < (int32)sizeof(Kept); ++Index) {
			Register = Table[(Register ^ KeptBytes[Index]) & 0xFF] ^ (Register >> 8);
		}

		uint8 Used[4];
		uint32 Wanted = ~Written.D;

		for (int32 Step = 3; Step >= 0; --Step) {
			int32 Found = INDEX_NONE;

			for (int32 Index = 0; Index < 256; ++Index) {
				if ((Table[Index] >> 24) == (Wanted >> 24)) {
					Found = Index;

					break;
				}
			}

			if (Found == INDEX_NONE) return FGuid();

			Used[Step] = (uint8)Found;
			Wanted = (Wanted ^ Table[Found]) << 8;
		}

		uint8 Tail[4];

		for (int32 Step = 0; Step < 4; ++Step) {
			Tail[Step] = (uint8)((Register ^ Used[Step]) & 0xFF);
			Register = Table[(Register ^ Tail[Step]) & 0xFF] ^ (Register >> 8);
		}

		uint32 Last = 0;
		FMemory::Memcpy(&Last, Tail, sizeof(Last));

		const FGuid Node(Written.A, Written.B, Written.C, Last);

		if (FCrc::MemCrc32(&Node, sizeof(Node)) != Written.D) return FGuid();

		return Node;
	}

	bool GoesWhereAsync(const UEdGraphPin* One, const UEdGraphPin* Other) {
		const int32 OneCount = One != nullptr ? One->LinkedTo.Num() : 0;
		const int32 OtherCount = Other != nullptr ? Other->LinkedTo.Num() : 0;

		if (OneCount != OtherCount) return false;

		for (int32 Index = 0; Index < OneCount; ++Index) {
			if (!Other->LinkedTo.Contains(One->LinkedTo[Index])) return false;
		}

		return true;
	}

	int32 WaysOutOf(const UEdGraphNode* Node) {
		int32 Ways = 0;

		for (const UEdGraphPin* Pin : Node->Pins) {
			if (Pin != nullptr && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) Ways++;
		}

		return Ways;
	}

	UEdGraphPin* LeavesBy(const UEdGraphNode* Node) {
		UEdGraphPin* Only = nullptr;

		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin == nullptr || Pin->Direction != EGPD_Output) continue;
			if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
			if (Pin->LinkedTo.Num() == 0) continue;

			if (Only != nullptr) return nullptr;

			Only = Pin;
		}

		return Only;
	}

	UK2Node* CarriesOnTo(const UEdGraphPin* Leaves) {
		return Leaves != nullptr && Leaves->LinkedTo.Num() == 1 ? Cast<UK2Node>(Leaves->LinkedTo[0]->GetOwningNode()) : nullptr;
	}

	template <typename PropertyType, typename ValueType>
	void SayTo(UObject* Object, const TCHAR* Named, ValueType Value) {
		if (Object == nullptr) return;

		if (PropertyType* Held = FindFProperty<PropertyType>(Object->GetClass(), Named)) {
			Held->SetPropertyValue_InContainer(Object, Value);
		}
	}

	UClass* DrawnAs(const UFunction* Makes, const UEdGraph* Graph) {
		if (Makes != nullptr) {
			const FBlueprintActionDatabase::FActionRegistry& Every = FBlueprintActionDatabase::Get().GetAllActions();

			if (const FBlueprintActionDatabase::FActionList* Claimed = Every.Find(FObjectKey(Makes->GetOwnerClass()))) {
				for (const UBlueprintNodeSpawner* Spawner : *Claimed) {
					if (Spawner == nullptr || Spawner->NodeClass == nullptr) continue;
					if (!Spawner->NodeClass->IsChildOf(UK2Node_BaseAsyncTask::StaticClass())) continue;

					const UBlueprintFunctionNodeSpawner* ForCall = Cast<UBlueprintFunctionNodeSpawner>(Spawner);

					if (ForCall == nullptr || ForCall->GetFunction() != Makes) continue;

					const UEdGraphNode* Kind = Spawner->NodeClass->GetDefaultObject<UEdGraphNode>();

					if (Graph != nullptr && Kind != nullptr && !Kind->IsCompatibleWithGraph(Graph)) continue;

					return Spawner->NodeClass;
				}
			}
		}

		return UK2Node_AsyncAction::StaticClass();
	}
}

struct FAsyncTaskTidy final : FGraphTidy {
	virtual const TCHAR* GetName() const override { return TEXT("AsyncTask"); }

	virtual int32 Apply(UEdGraph* Graph) const override {
		if (Graph == nullptr) return 0;

		int32 Put = 0;

		TArray<UEdGraphNode*> Nodes = Graph->Nodes;

		for (UEdGraphNode* Node : Nodes) {
			UK2Node_CallFunction* Makes = Cast<UK2Node_CallFunction>(Node);

			if (Makes == nullptr) continue;

			UFunction* Called = Makes->GetTargetFunction();

			if (Called == nullptr || !Called->HasMetaData(FBlueprintMetadata::MD_BlueprintInternalUseOnly)) continue;

			UEdGraphPin* Made = Makes->GetReturnValuePin();

			if (Made == nullptr || Made->LinkedTo.Num() == 0) continue;

			UClass* Task = Cast<UClass>(Made->PinType.PinSubCategoryObject.Get());

			if (Task == nullptr) continue;

			const FString Named = Called->GetName();

			UEdGraphPin* Entered = Makes->GetExecPin();

			TArray<UK2Node_AddDelegate*> Binds;
			TArray<UK2Node*> Looks;
			TArray<UK2Node*> Workings;

			UK2Node_CallFunction* Begins = nullptr;
			UEdGraphPin* Leaves = LeavesBy(Makes);
			TArray<UEdGraphPin*> Otherwise;

			UK2Node_CallFunction* SpawnStarts = nullptr;
			UK2Node_CallFunction* SpawnFinishes = nullptr;

			while (UK2Node* Along = CarriesOnTo(Leaves)) {
				UK2Node_IfThenElse* AsksSpawn = SpawnStarts != nullptr && SpawnFinishes == nullptr ? Cast<UK2Node_IfThenElse>(Along) : nullptr;

				if (AsksSpawn != nullptr && !AsksSpawn->GetConditionPin()->LinkedTo.Contains(SpawnStarts->GetReturnValuePin())) AsksSpawn = nullptr;

				if (AsksSpawn == nullptr && !ReadsAsync(Along, Made, Workings)) break;

				if (UK2Node_AddDelegate* Binding = Cast<UK2Node_AddDelegate>(Along)) {
					Binds.Add(Binding);
				} else if (UK2Node_CallFunction* Begin = Cast<UK2Node_CallFunction>(Along)) {
					const UFunction* Starts = Begin->GetTargetFunction();

					if (Starts == nullptr || Starts->HasAnyFunctionFlags(FUNC_Static) || !Task->IsChildOf(Starts->GetOwnerClass())) break;

					if (SpawnStarts == nullptr && Starts->GetFName() == TEXT("BeginSpawningActor")) {
						SpawnStarts = Begin;
					} else if (SpawnStarts != nullptr && SpawnFinishes == nullptr && Starts->GetFName() == TEXT("FinishSpawningActor")) {
						SpawnFinishes = Begin;
					} else {
						Begins = Begin;
					}
				} else if (WaysOutOf(Along) < 2 || LeavesBy(Along) != nullptr) {
					Looks.Add(Along);
				} else if (UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Along); Branch != nullptr && (Otherwise.Num() == 0 || SpawnStarts != nullptr)) {
					Looks.Add(Along);
					Otherwise.Add(Branch->GetElsePin());
					Leaves = Branch->GetThenPin();

					continue;
				} else {
					break;
				}

				Leaves = LeavesBy(Along);

				if (Begins != nullptr || Leaves == nullptr) break;
			}

			if (Binds.Num() == 0) {
				TArray<FString> Went;

				for (UEdGraphPin* Step = LeavesBy(Makes); Step != nullptr && Went.Num() < 6;) {
					UK2Node* Along = CarriesOnTo(Step);

					if (Along == nullptr) break;

					TArray<UK2Node*> Ignored;

					Went.Add(FString::Printf(TEXT("%s%s"), *Along->GetClass()->GetName(), ReadsAsync(Along, Made, Ignored) ? TEXT("") : TEXT(" (reads it not)")));

					Step = LeavesBy(Along);
				}

				UE_LOG(LogReflectionTidyAsyncTask, Warning, TEXT("\"%s\" starts \"%s\" and was left as it was read: nothing binds to it, and the run goes into %s"),
					*Graph->GetName(), *Named, Went.Num() > 0 ? *FString::Join(Went, TEXT(", ")) : TEXT("nothing"));

				continue;
			}

			TMap<FName, UK2Node_CustomEvent*> Answers;
			TArray<UK2Node_CreateDelegate*> Handlers;

			FString Missed;

			for (const UK2Node_AddDelegate* Binding : Binds) {
				const UEdGraphPin* Handler = Binding->GetDelegatePin();

				UK2Node_CreateDelegate* Laid = Handler != nullptr && Handler->LinkedTo.Num() == 1
					? Cast<UK2Node_CreateDelegate>(Handler->LinkedTo[0]->GetOwningNode())
					: nullptr;

				if (Laid == nullptr) {
					Missed = FString::Printf(TEXT("nothing lays a handler down for \"%s\""), *Binding->GetPropertyName().ToString());

					break;
				}

				UK2Node_CustomEvent* Answered = nullptr;

				for (UEdGraphNode* Held : Graph->Nodes) {
					UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Held);

					if (Event != nullptr && Event->CustomFunctionName == Laid->GetFunctionName()) {
						Answered = Event;

						break;
					}
				}

				if (Answered == nullptr) {
					Missed = FString::Printf(TEXT("the handler for \"%s\" is \"%s\", which no event in this graph answers to"),
						*Binding->GetPropertyName().ToString(), *Laid->GetFunctionName().ToString());

					break;
				}

				Answers.Add(Binding->GetPropertyName(), Answered);
				Handlers.Add(Laid);
			}

			if (Answers.Num() != Binds.Num()) {
				UE_LOG(LogReflectionTidyAsyncTask, Warning, TEXT("\"%s\" starts \"%s\" and was left as it was read: %s"), *Graph->GetName(), *Named, *Missed);

				continue;
			}

			if (SpawnStarts != nullptr && (SpawnFinishes == nullptr || Begins == nullptr)) {
				UE_LOG(LogReflectionTidyAsyncTask, Warning, TEXT("\"%s\" starts \"%s\" and was left as it was read: it starts spawning an actor, and what follows is not the finish and the word to begin"), *Graph->GetName(), *Named);

				continue;
			}

			bool bDrawnBranch = false;

			for (const UEdGraphPin* Other : Otherwise) {
				if (!GoesWhereAsync(Other, Leaves)) bDrawnBranch = true;
			}

			if (bDrawnBranch) {
				UE_LOG(LogReflectionTidyAsyncTask, Warning, TEXT("\"%s\" starts \"%s\" and was left as it was read: the look to see it was made goes somewhere of its own when it was not"), *Graph->GetName(), *Named);

				continue;
			}

			UK2Node_BaseAsyncTask* Waits = NewObject<UK2Node_BaseAsyncTask>(Graph, DrawnAs(Called, Graph), NAME_None, RF_Transactional);

			Graph->AddNode(Waits, false, false);

			Waits->CreateNewGuid();
			Waits->PostPlacedNewNode();

			if (Handlers.Num() > 0) {
				const FGuid Recovered = NodeNamedBy(Handlers[0]->GetFunctionName());

				if (Recovered.IsValid()) {
					Waits->NodeGuid = Recovered;
				} else {
					UE_LOG(LogReflectionTidyAsyncTask, Warning, TEXT("\"%s\" starts \"%s\": no node guid gives the handler name \"%s\", so it keeps a fresh one"),
						*Graph->GetName(), *Named, *Handlers[0]->GetFunctionName().ToString());
				}
			}

			SayTo<FNameProperty>(Waits, TEXT("ProxyFactoryFunctionName"), Called->GetFName());
			SayTo<FObjectProperty>(Waits, TEXT("ProxyFactoryClass"), Called->GetOwnerClass());
			SayTo<FObjectProperty>(Waits, TEXT("ProxyClass"), Task);

			SayTo<FNameProperty>(Waits, TEXT("ProxyActivateFunctionName"), Begins != nullptr && Begins->GetTargetFunction() != nullptr
				? Begins->GetTargetFunction()->GetFName()
				: FName(NAME_None));

			if (Waits->Pins.Num() == 0) Waits->AllocateDefaultPins();

			Waits->NodePosX = Makes->NodePosX;
			Waits->NodePosY = Makes->NodePosY;

			TakeOverAsync(Entered, Waits->GetExecPin());
			TakeOverAsync(Leaves, Waits->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output));

			for (UEdGraphPin* Given : Makes->Pins) {
				if (Given == nullptr || Given->Direction != EGPD_Input) continue;
				if (Given->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;

				UEdGraphPin* Takes = Waits->FindPin(Given->PinName, EGPD_Input);

				if (Takes == nullptr) continue;

				TakeOverAsync(Given, Takes);

				if (Given->LinkedTo.Num() == 0) {
					Takes->DefaultValue = Given->DefaultValue;
					Takes->DefaultObject = Given->DefaultObject;
					Takes->DefaultTextValue = Given->DefaultTextValue;
				}
			}

			int32 Hung = 0;

			for (const TPair<FName, UK2Node_CustomEvent*>& Answer : Answers) {
				UEdGraphPin* Way = Waits->FindPin(Answer.Key, EGPD_Output);

				if (Way == nullptr) continue;

				for (UEdGraphPin* Held : Answer.Value->Pins) {
					if (Held == nullptr || Held->Direction != EGPD_Output) continue;

					if (Held->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) {
						TakeOverAsync(Held, Way);

						continue;
					}

					TakeOverAsync(Held, Waits->FindPin(Held->PinName, EGPD_Output));
				}

				Hung++;
			}

			if (Hung == 0) {
				TArray<FString> Grew;

				for (const UEdGraphPin* Pin : Waits->Pins) {
					if (Pin != nullptr && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) Grew.Add(Pin->PinName.ToString());
				}

				UE_LOG(LogReflectionTidyAsyncTask, Warning, TEXT("\"%s\" starts \"%s\" and was left as it was read: the node grew no way out for any of them, only %s"),
					*Graph->GetName(), *Named, Grew.Num() > 0 ? *FString::Join(Grew, TEXT(", ")) : TEXT("none"));

				Graph->RemoveNode(Waits);

				continue;
			}

			for (const TPair<FName, UK2Node_CustomEvent*>& Answer : Answers) {
				Graph->RemoveNode(Answer.Value);
			}

			for (UK2Node_CreateDelegate* Laid : Handlers) {
				Graph->RemoveNode(Laid);
			}

			for (UK2Node_AddDelegate* Binding : Binds) {
				Graph->RemoveNode(Binding);
			}

			for (UK2Node* Held : Looks) {
				Graph->RemoveNode(Held);
			}

			if (Begins != nullptr) Graph->RemoveNode(Begins);
			if (SpawnStarts != nullptr) Graph->RemoveNode(SpawnStarts);
			if (SpawnFinishes != nullptr) Graph->RemoveNode(SpawnFinishes);

			Graph->RemoveNode(Makes);

			for (UK2Node* Works : Workings) {
				bool bWanted = false;

				for (const UEdGraphPin* Pin : Works->Pins) {
					if (Pin != nullptr && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0) bWanted = true;
				}

				if (!bWanted) Graph->RemoveNode(Works);
			}

			Put++;
		}

		if (Put > 0) {
			UE_LOG(LogReflectionTidyAsyncTask, Display, TEXT("\"%s\" had %d task(s) written as everything it takes to start one"), *Graph->GetName(), Put);
		}

		return Put;
	}
};

REGISTER_TIDY(FAsyncTaskTidy)
