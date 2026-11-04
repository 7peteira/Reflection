#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/MacroPattern.h"

using namespace MacroReading;

struct FForLoopWithBreakMacro final : FMacroPattern {
	virtual const TCHAR* GetName() const override { return TEXT("ForLoopWithBreak"); }

	virtual bool Match(const TArray<FUObjectJsonValueExport>& Statements, const int32 At, FMacroMatch& Out) const override {
		if (!Statements.IsValidIndex(At)) return false;

		TSet<int32> Inside;

		int32 Where = AddressOf(Statements[At]);
		int32 Look = At;

		if (!IsLet(TokenOf(Statements[Look]))) return false;

		const FString Broke = WrittenTo(Statements[Look]);

		if (!Broke.StartsWith(TEXT("Temp_"))) return false;
		if (TokenOf(Statements[Look].GetObject(TEXT("Expression"))) != TEXT("EX_False")) return false;

		Inside.Add(Where);

		auto Carry = [&Statements, &Inside, &Where, &Look]() {
			Where = NextInRun(Statements, Where, Inside);
			Look = Where == INDEX_NONE ? INDEX_NONE : IndexOfAddress(Statements, Where);

			return Look != INDEX_NONE;
		};

		if (!Carry() || !IsLet(TokenOf(Statements[Look]))) return false;

		const FString Counter = WrittenTo(Statements[Look]);

		if (!Counter.StartsWith(TEXT("Temp_")) || Counter == Broke) return false;

		const FUObjectJsonValueExport From = Statements[Look].GetObject(TEXT("Expression"));

		Inside.Add(Where);

		if (!Carry() || !IsLet(TokenOf(Statements[Look]))) return false;

		const int32 Compare = Where;

		const FUObjectJsonValueExport Asking = Statements[Look].GetObject(TEXT("Expression"));

		if (CallsTo(Asking) != TEXT("Not_PreBool")) return false;

		const TArray<FUObjectJsonValueExport> Asked = Asking.Has(TEXT("Parameters")) ? Asking.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

		if (Asked.Num() < 1 || ReadFrom(Asked[0]) != Broke) return false;

		const FString Carrying = WrittenTo(Statements[Look]);

		Inside.Add(Where);

		if (!Carry() || !IsLet(TokenOf(Statements[Look]))) return false;

		const FUObjectJsonValueExport Condition = Statements[Look].GetObject(TEXT("Expression"));

		if (CallsTo(Condition) != TEXT("LessEqual_IntInt")) return false;

		const TArray<FUObjectJsonValueExport> Operands = Condition.Has(TEXT("Parameters")) ? Condition.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

		if (Operands.Num() < 2 || ReadFrom(Operands[0]) != Counter) return false;

		const FString Within = WrittenTo(Statements[Look]);

		Inside.Add(Where);

		if (!Carry() || !IsLet(TokenOf(Statements[Look]))) return false;

		const FUObjectJsonValueExport Both = Statements[Look].GetObject(TEXT("Expression"));

		if (CallsTo(Both) != TEXT("BooleanAND")) return false;

		const TArray<FUObjectJsonValueExport> Sides = Both.Has(TEXT("Parameters")) ? Both.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

		if (Sides.Num() < 2) return false;

		const FString Left = ReadFrom(Sides[0]);
		const FString Right = ReadFrom(Sides[1]);

		if (!((Left == Carrying && Right == Within) || (Left == Within && Right == Carrying))) return false;

		Inside.Add(Where);

		if (!Carry()) return false;

		const FString Ending = TokenOf(Statements[Look]);

		if (Ending != TEXT("EX_PopExecutionFlowIfNot") && Ending != TEXT("EX_JumpIfNot")) return false;

		Inside.Add(Where);

		if (Ending == TEXT("EX_JumpIfNot")) {
			Out.Leads.Add(TEXT("Completed"), Statements[Look].GetInteger(TEXT("CodeOffset"), INDEX_NONE));
		}

		if (!Carry() || TokenOf(Statements[Look]) != TEXT("EX_PushExecutionFlow")) return false;

		const int32 Raise = Statements[Look].GetInteger(TEXT("PushingAddress"), INDEX_NONE);

		Inside.Add(Where);

		if (!Carry()) return false;

		Out.Leads.Add(TEXT("LoopBody"), Where);

		const int32 Raising = Raise == INDEX_NONE ? INDEX_NONE : IndexOfAddress(Statements, Raise);

		if (Raising == INDEX_NONE || !IsLet(TokenOf(Statements[Raising]))) return false;
		if (CallsTo(Statements[Raising].GetObject(TEXT("Expression"))) != TEXT("Add_IntInt")) return false;

		Inside.Add(Raise);

		Where = Raise;

		if (!Carry() || WrittenTo(Statements[Look]) != Counter) return false;

		Inside.Add(Where);

		if (!Carry() || Where != Compare) return false;

		for (int32 Which = 0; Which < Statements.Num(); Which++) {
			if (!IsLet(TokenOf(Statements[Which]))) continue;
			if (WrittenTo(Statements[Which]) != Broke) continue;
			if (TokenOf(Statements[Which].GetObject(TEXT("Expression"))) != TEXT("EX_True")) continue;

			Out.Internal.Add(Which);
			Out.Takes.Add(Which, TEXT("Break"));
		}

		Out.Handouts.Add(Counter, TEXT("Index"));

		Out.Inputs.Add(TEXT("FirstIndex"), From);
		Out.Deferred.Add(TEXT("LastIndex"), Operands[1]);

		Out.First = At;
		Out.Last = At;

		for (const int32 Address : Inside) {
			const int32 Which = IndexOfAddress(Statements, Address);

			if (Which == INDEX_NONE) continue;

			Out.Internal.Add(Which);

			Out.Last = FMath::Max(Out.Last, Which);
		}

		return true;
	}
};

REGISTER_MACRO(FForLoopWithBreakMacro)
