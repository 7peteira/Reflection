#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Animation/CurveExpressionRig.h"
#include "Engine/Log.h"

#if REFLECTION_RIGVM

#include "Importers/Constructor/ImportIssues.h"
#include "Modules/Metadata.h"

#include "Engine/ControlRigCompatibility.h"

namespace {
	FVector2D At(const int32 Column, const int32 Row) {
		return FVector2D(360.0 * Column, 130.0 * Row);
	}

	FString PathOf(const URigVMNode* Node, const TCHAR* Pin) {
		if (Node == nullptr) return FString();

		const URigVMPin* Found = Node->FindPin(Pin);

		return Found != nullptr ? Found->GetPinPath() : FString();
	}

	FString Number(const double Value) {
		return FString::Printf(TEXT("%.9f"), Value);
	}
}

bool FCurveExpressionRig::Build(UControlRigBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& Expressions,
	const FString& Named, FCurveExpressionRigStats& OutStats) {
	if (Blueprint == nullptr || Expressions.Num() == 0) return false;

	URigVMGraph* Graph = RigModelOf(Blueprint);
	URigVMController* Controller = RigControllerOf(Blueprint, Graph);

	if (Graph == nullptr || Controller == nullptr) return false;

	TArray<FString> Reads;

	for (const TSharedPtr<FJsonValue>& Value : Expressions) {
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;

		if (!Entry.IsValid()) continue;

		FString Form;

		Entry->TryGetStringField(TEXT("form"), Form);

		if (Form != TEXT("linear") && Form != TEXT("clamped")) continue;

		const TSharedPtr<FJsonObject>* Weights = nullptr;

		if (!Entry->TryGetObjectField(TEXT("weights"), Weights)) continue;

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Weight : (*Weights)->Values) {
			Reads.AddUnique(Weight.Key);
		}
	}

	if (Reads.Num() == 0) return false;

	Reads.Sort();

	int32 Made = 0;

	URigVMNode* Begin = RigAddNode(Controller, RigUnits::BeginExecution, At(0, 0), TEXT("RigUnit_BeginExecution"));

	if (Begin == nullptr) return false;

	Made++;

	TMap<FString, FString> ReadValue;

	for (int32 Index = 0; Index < Reads.Num(); ++Index) {
		URigVMNode* Read = RigAddNode(Controller, RigUnits::GetCurveValue, At(1, Index + 1),
			FString::Printf(TEXT("Get_%s"), *Reads[Index]));

		if (Read == nullptr) continue;

		Made++;

		const FString Curve = PathOf(Read, TEXT("Curve"));

		RigSetDefault(Controller, Curve, Reads[Index]);

		ReadValue.Add(Reads[Index], PathOf(Read, TEXT("Value")));
	}

	OutStats.Read = ReadValue.Num();

	const URigVMPin* Started = RigExecutePin(Begin);

	FString Running = Started != nullptr ? Started->GetPinPath() : FString();

	int32 Row = 0;

	for (const TSharedPtr<FJsonValue>& Value : Expressions) {
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;

		if (!Entry.IsValid()) continue;

		FString Target, Form;

		if (!Entry->TryGetStringField(TEXT("target"), Target) || Target.IsEmpty()) continue;

		OutStats.Targets++;

		Entry->TryGetStringField(TEXT("form"), Form);

		if (Form != TEXT("linear") && Form != TEXT("clamped")) {
			OutStats.Refused.Add(Target);

			continue;
		}

		const TSharedPtr<FJsonObject>* Weights = nullptr;

		if (!Entry->TryGetObjectField(TEXT("weights"), Weights) || (*Weights)->Values.Num() == 0) {
			OutStats.Refused.Add(Target);

			continue;
		}

		TArray<FString> Order;

		if (const TArray<TSharedPtr<FJsonValue>>* Constants = nullptr; Entry->TryGetArrayField(TEXT("constants"), Constants)) {
			for (const TSharedPtr<FJsonValue>& One : *Constants) {
				FString Read;

				if (One.IsValid() && One->TryGetString(Read)) Order.AddUnique(Read);
			}
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Weight : (*Weights)->Values) {
			Order.AddUnique(Weight.Key);
		}

		Row++;

		TArray<FString> Terms;

		int32 Depth = 0;

		for (const FString& Read : Order) {
			const TSharedPtr<FJsonValue> Held = (*Weights)->TryGetField(Read);

			if (!Held.IsValid()) continue;

			const FString* Source = ReadValue.Find(Read);

			if (Source == nullptr || Source->IsEmpty()) continue;

			const double Amount = Held->AsNumber();

			if (FMath::IsNearlyEqual(Amount, 1.0)) {
				Terms.Add(*Source);

				continue;
			}

			URigVMNode* Scale = RigAddNode(Controller, RigUnits::Multiply, At(2 + Depth++, Row),
				FString::Printf(TEXT("%s_%s_Weighed"), *Target, *Read));

			if (Scale == nullptr) continue;

			Made++;

			RigLink(Controller, *Source, PathOf(Scale, TEXT("A")));
			RigSetDefault(Controller, PathOf(Scale, TEXT("B")), Number(Amount));

			Terms.Add(PathOf(Scale, TEXT("Result")));
		}

		if (Terms.Num() == 0) {
			OutStats.Refused.Add(Target);

			continue;
		}

		FString Total = Terms[0];

		for (int32 Index = 1; Index < Terms.Num(); ++Index) {
			URigVMNode* Add = RigAddNode(Controller, RigUnits::Add, At(4 + Index, Row),
				FString::Printf(TEXT("%s_Sum_%d"), *Target, Index));

			if (Add == nullptr) continue;

			Made++;

			RigLink(Controller, Total, PathOf(Add, TEXT("A")));
			RigLink(Controller, Terms[Index], PathOf(Add, TEXT("B")));

			Total = PathOf(Add, TEXT("Result"));
		}

		if (Form == TEXT("clamped")) {
			URigVMNode* Clamp = RigAddNode(Controller, RigUnits::Clamp, At(4 + Terms.Num(), Row),
				FString::Printf(TEXT("%s_Clamp"), *Target));

			if (Clamp != nullptr) {
				Made++;

				RigLink(Controller, Total, PathOf(Clamp, TEXT("Value")));
				RigSetDefault(Controller, PathOf(Clamp, TEXT("Minimum")), Number(0.0));
				RigSetDefault(Controller, PathOf(Clamp, TEXT("Maximum")), Number(1.0));

				Total = PathOf(Clamp, TEXT("Result"));
			}
		}

		URigVMNode* Write = RigAddNode(Controller, RigUnits::SetCurveValue, At(6 + Terms.Num(), Row),
			FString::Printf(TEXT("Set_%s"), *Target));

		if (Write == nullptr) {
			OutStats.Refused.Add(Target);

			continue;
		}

		Made++;

		RigSetDefault(Controller, PathOf(Write, TEXT("Curve")), Target);
		RigLink(Controller, Total, PathOf(Write, TEXT("Value")));

		const URigVMPin* Carries = RigExecutePin(Write);

		if (Carries != nullptr && !Running.IsEmpty()) {
			RigLink(Controller, Running, Carries->GetPinPath());

			Running = Carries->GetPinPath();
		}

		OutStats.Written++;
	}

	OutStats.Nodes = Made;

	if (OutStats.Refused.Num() > 0) {
		FImportIssues::Report(
			EImportIssue::Data,
			FString::Printf(TEXT("%d of %d curves aren't a weighted sum"), OutStats.Refused.Num(), OutStats.Targets),
			FString::Printf(
				TEXT("The rig performs the rest. These drive their curve by arithmetic that weights alone don't describe, so nothing was built for them: %s"),
				*FString::Join(OutStats.Refused, TEXT(", ")))
		);
	}

	UE_LOG(LogReflection, Display,
		TEXT("\"%s\" built %d node(s): %d curve(s) written out of %d read"),
		*Named, OutStats.Nodes, OutStats.Written, OutStats.Read);

	return OutStats.Written > 0;
}

#endif
