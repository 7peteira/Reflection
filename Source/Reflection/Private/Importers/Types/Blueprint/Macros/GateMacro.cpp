#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/MacroPattern.h"

using namespace MacroReading;

struct FGateMacro final : FMacroPattern {
	virtual const TCHAR* GetName() const override { return TEXT("Gate"); }

	virtual bool Match(const TArray<FUObjectJsonValueExport>& Statements, const int32 At, FMacroMatch& Out) const override {
		if (!Statements.IsValidIndex(At) || TokenOf(Statements[At]) != TEXT("EX_JumpIfNot")) return false;

		const int32 Opens = IndexOfAddress(Statements, Statements[At].GetInteger(TEXT("CodeOffset"), INDEX_NONE));

		if (!Statements.IsValidIndex(At + 1) || !Statements.IsValidIndex(Opens)) return false;

		const FString Gate = WrittenTo(Statements[At + 1]);

		if (Gate.IsEmpty() || !Gate.StartsWith(TEXT("Temp_"))) return false;
		if (WrittenTo(Statements[Opens]) != Gate) return false;

		if (TokenOf(Statements[At + 1].GetObject(TEXT("Expression"))) != TEXT("EX_False")) return false;
		if (TokenOf(Statements[Opens].GetObject(TEXT("Expression"))) != TEXT("EX_True")) return false;

		if (!Statements.IsValidIndex(At + 2) || TokenOf(Statements[At + 2]) != TEXT("EX_PopExecutionFlow")) return false;
		if (!Statements.IsValidIndex(Opens + 1) || TokenOf(Statements[Opens + 1]) != TEXT("EX_PopExecutionFlow")) return false;

		TSet<int32> Inside;

		Inside.Add(At);
		Inside.Add(At + 1);
		Inside.Add(At + 2);
		Inside.Add(Opens);
		Inside.Add(Opens + 1);

		int32 Enters = INDEX_NONE;

		for (int32 Look = 0; Look < Statements.Num(); ++Look) {
			if (TokenOf(Statements[Look]) != TEXT("EX_PopExecutionFlowIfNot")) continue;
			if (ReadFrom(Statements[Look].GetObject(TEXT("BooleanExpression"))) != Gate) continue;

			Enters = Look;

			break;
		}

		if (Enters == INDEX_NONE || !Statements.IsValidIndex(Enters + 1)) return false;

		Inside.Add(Enters);

		Out.Leads.Add(TEXT("Exit"), AddressOf(Statements[Enters + 1]));

		Out.Inputs.Add(TEXT("Start Closed"), Statements[At].GetObject(TEXT("BooleanExpression")));

		for (int32 Look = 0; Look < Statements.Num(); ++Look) {
			if (Inside.Contains(Look) || !IsLet(TokenOf(Statements[Look]))) continue;
			if (WrittenTo(Statements[Look]) != Gate) continue;
			if (!Statements.IsValidIndex(Look + 1) || TokenOf(Statements[Look + 1]) != TEXT("EX_PopExecutionFlow")) continue;

			const FString Says = TokenOf(Statements[Look].GetObject(TEXT("Expression")));

			if (Says != TEXT("EX_True") && Says != TEXT("EX_False")) continue;

			Out.Takes.Add(Look, Says == TEXT("EX_True") ? TEXT("Open") : TEXT("Close"));

			Inside.Add(Look);
			Inside.Add(Look + 1);
		}

		Out.First = At;
		Out.Last = At;

		for (const int32 Which : Inside) {
			Out.Internal.Add(Which);

			Out.Last = FMath::Max(Out.Last, Which);
		}

		return true;
	}
};

REGISTER_MACRO(FGateMacro)
