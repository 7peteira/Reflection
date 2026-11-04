#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/MacroPattern.h"

using namespace MacroReading;

struct FForLoopMacro final : FMacroPattern {
	virtual const TCHAR* GetName() const override { return TEXT("ForLoop"); }

	virtual bool Match(const TArray<FUObjectJsonValueExport>& Statements, const int32 At, FMacroMatch& Out) const override {
		if (!Statements.IsValidIndex(At) || !IsLet(TokenOf(Statements[At]))) return false;

		const FString Counter = WrittenTo(Statements[At]);

		if (!Counter.StartsWith(TEXT("Temp_"))) return false;

		int32 Compare = INDEX_NONE;

		for (int32 Look = At + 1; Look < Statements.Num(); ++Look) {
			const FUObjectJsonValueExport& Statement = Statements[Look];

			if (WrittenTo(Statement) == Counter) return false;
			if (!IsLet(TokenOf(Statement))) break;

			if (CallsTo(Statement.GetObject(TEXT("Expression"))) == TEXT("LessEqual_IntInt")) {
				Compare = Look;

				break;
			}
		}

		if (Compare == INDEX_NONE || Compare + 2 >= Statements.Num()) return false;

		const FUObjectJsonValueExport Condition = Statements[Compare].GetObject(TEXT("Expression"));
		const TArray<FUObjectJsonValueExport> Operands = Condition.Has(TEXT("Parameters")) ? Condition.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

		if (Operands.Num() < 2 || ReadFrom(Operands[0]) != Counter) return false;

		const FString Ending = TokenOf(Statements[Compare + 1]);

		if (Ending != TEXT("EX_PopExecutionFlowIfNot") && Ending != TEXT("EX_JumpIfNot")) return false;
		if (TokenOf(Statements[Compare + 2]) != TEXT("EX_PushExecutionFlow")) return false;

		const int32 Raise = IndexOfAddress(Statements, Statements[Compare + 2].GetInteger(TEXT("PushingAddress"), -1));

		if (Raise == INDEX_NONE || Raise + 2 >= Statements.Num()) return false;
		if (CallsTo(Statements[Raise].GetObject(TEXT("Expression"))) != TEXT("Add_IntInt")) return false;
		if (WrittenTo(Statements[Raise + 1]) != Counter) return false;
		if (TokenOf(Statements[Raise + 2]) != TEXT("EX_Jump")) return false;

		Out.First = At;
		Out.Last = Raise + 2;

		Out.Inputs.Add(TEXT("FirstIndex"), Statements[At].GetObject(TEXT("Expression")));
		Out.Deferred.Add(TEXT("LastIndex"), Operands[1]);

		Out.Handouts.Add(Counter, TEXT("Index"));

		if (Statements.IsValidIndex(Compare + 3)) {
			Out.Leads.Add(TEXT("LoopBody"), AddressOf(Statements[Compare + 3]));
		}

		if (TokenOf(Statements[Compare + 1]) == TEXT("EX_JumpIfNot")) {
			Out.Leads.Add(TEXT("Completed"), Statements[Compare + 1].GetInteger(TEXT("CodeOffset"), INDEX_NONE));
		}

		Out.Internal.Add(At);
		Out.Internal.Add(Compare);
		Out.Internal.Add(Compare + 1);
		Out.Internal.Add(Compare + 2);
		Out.Internal.Add(Raise);
		Out.Internal.Add(Raise + 1);
		Out.Internal.Add(Raise + 2);

		return true;
	}
};

REGISTER_MACRO(FForLoopMacro)
