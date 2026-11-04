#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/BlueprintCompile.h"

#include "Engine/EngineUtilities.h"
#include "Importers/Constructor/ImportIssues.h"

#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FeedbackContext.h"
#include "Misc/SlowTask.h"

namespace {
	int32 SlowTasks() {
		return GWarn != nullptr ? GWarn->GetScopeStack().Num() : 0;
	}

	void DropSlowTasksTo(const int32 Depth) {
		if (GWarn == nullptr) return;

		FSlowTaskStack& Stack = const_cast<FSlowTaskStack&>(GWarn->GetScopeStack());

		if (Stack.Num() > Depth) {
			Stack.SetNum(Depth);
		}
	}

	bool Attempt(UBlueprint* Blueprint, const EBlueprintCompileOptions Options) {
#if PLATFORM_WINDOWS && !PLATFORM_SEH_EXCEPTIONS_DISABLED
		__try {
			FKismetEditorUtilities::CompileBlueprint(Blueprint, Options);

			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
#else
		FKismetEditorUtilities::CompileBlueprint(Blueprint, Options);

		return true;
#endif
	}

	UBlueprint* AttemptCreate(UClass* ParentClass, UObject* Outer, const FName Name,
		const EBlueprintType BlueprintType, UClass* BlueprintClassType, UClass* BlueprintGeneratedClassType) {
#if PLATFORM_WINDOWS && !PLATFORM_SEH_EXCEPTIONS_DISABLED
		__try {
			return FKismetEditorUtilities::CreateBlueprint(ParentClass, Outer, Name, BlueprintType,
				BlueprintClassType, BlueprintGeneratedClassType);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
#else
		return FKismetEditorUtilities::CreateBlueprint(ParentClass, Outer, Name, BlueprintType,
			BlueprintClassType, BlueprintGeneratedClassType);
#endif
	}
}

bool CompileBlueprintGuarded(UBlueprint* Blueprint, const EBlueprintCompileOptions Options) {
	if (Blueprint == nullptr) return false;

	const FString Named = Blueprint->GetName();

	const int32 Depth = SlowTasks();

	if (Attempt(Blueprint, Options)) {
		return true;
	}

	DropSlowTasksTo(Depth);

	if (UPackage* Held = Blueprint->GetOutermost()) {
		Held->SetDirtyFlag(false);
		Held->SetFlags(RF_Transient);
	}

	UE_LOG(LogReflection, Error, TEXT("\"%s\" could not be compiled and nothing was written for it"), *Named);

	FImportIssues::Report(
		EImportIssue::Data,
		FString::Printf(TEXT("\"%s\" could not be compiled"), *Named),
		FString::Printf(
			TEXT("The blueprint was built from the export and the compiler could not read it back. That is the asset being wrong in a way nothing here checked for, most often a class it says it comes from or a type it names being something this engine hasn't got. What a compile leaves half built cannot be read back either, so nothing was written for '%s' rather than leaving a package that stops the editor whenever anything opens it. Everything importing alongside it carried on."),
			*Named)
	);

	return false;
}

UBlueprint* CreateBlueprintGuarded(UClass* ParentClass, UObject* Outer, const FName Name,
	const EBlueprintType BlueprintType, UClass* BlueprintClassType, UClass* BlueprintGeneratedClassType) {
	const int32 Depth = SlowTasks();

	if (UBlueprint* Made = AttemptCreate(ParentClass, Outer, Name, BlueprintType, BlueprintClassType, BlueprintGeneratedClassType)) {
		return Made;
	}

	DropSlowTasksTo(Depth);

	UE_LOG(LogReflection, Error, TEXT("\"%s\" could not be made and nothing was imported for it"), *Name.ToString());

	FImportIssues::Report(
		EImportIssue::Data,
		FString::Printf(TEXT("\"%s\" could not be made"), *Name.ToString()),
		FString::Printf(
			TEXT("Making a blueprint compiles it, and the compiler could not read what it was given. Usually that is the class it says it comes from being something this engine hasn't got, or having come across broken itself. Nothing was written for '%s' and the rest of the import carried on."),
			*Name.ToString())
	);

	return nullptr;
}
