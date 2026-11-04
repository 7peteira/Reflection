#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/MacroPattern.h"

using namespace MacroReading;

struct FForEachLoopMacro final : FMacroPattern {
	virtual const TCHAR* GetName() const override { return TEXT("ForEachLoop"); }

	virtual bool Match(const TArray<FUObjectJsonValueExport>& Statements, const int32 At, FMacroMatch& Out) const override {
		if (!Statements.IsValidIndex(At)) return false;

		TSet<int32> Inside;

		int32 Where = AddressOf(Statements[At]);
		int32 Look = At;

		if (!IsLet(TokenOf(Statements[Look]))) return false;

		const FString Counter = WrittenTo(Statements[Look]);

		if (!Counter.StartsWith(TEXT("Temp_"))) return false;
		if (TokenOf(Statements[Look].GetObject(TEXT("Expression"))) != TEXT("EX_IntConst")) return false;

		Inside.Add(Where);

		auto Carry = [&Statements, &Inside, &Where, &Look]() {
			Where = NextInRun(Statements, Where, Inside);
			Look = Where == INDEX_NONE ? INDEX_NONE : IndexOfAddress(Statements, Where);

			return Look != INDEX_NONE;
		};

		if (!Carry() || !IsLet(TokenOf(Statements[Look]))) return false;

		const FString Index = WrittenTo(Statements[Look]);

		if (!Index.StartsWith(TEXT("Temp_")) || Index == Counter) return false;
		if (TokenOf(Statements[Look].GetObject(TEXT("Expression"))) != TEXT("EX_IntConst")) return false;

		Inside.Add(Where);

		if (!Carry() || !IsLet(TokenOf(Statements[Look]))) return false;

		const FString Length = WrittenTo(Statements[Look]);
		const FUObjectJsonValueExport Measuring = Statements[Look].GetObject(TEXT("Expression"));
		const FUObjectJsonValueExport Measures = Measuring.Has(TEXT("ContextExpression")) ? Measuring.GetObject(TEXT("ContextExpression")) : Measuring;

		if (CallsTo(Measures) != TEXT("Array_Length")) return false;

		const TArray<FUObjectJsonValueExport> Measured = Measures.Has(TEXT("Parameters")) ? Measures.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

		if (Measured.Num() < 1) return false;

		Inside.Add(Where);

		const int32 Compare = Where;

		if (!Carry() || !IsLet(TokenOf(Statements[Look]))) return false;

		const FUObjectJsonValueExport Condition = Statements[Look].GetObject(TEXT("Expression"));

		if (CallsTo(Condition) != TEXT("Less_IntInt")) return false;

		const TArray<FUObjectJsonValueExport> Operands = Condition.Has(TEXT("Parameters")) ? Condition.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

		if (Operands.Num() < 2 || ReadFrom(Operands[0]) != Counter || ReadFrom(Operands[1]) != Length) return false;

		Inside.Add(Where);

		if (!Carry()) return false;

		const FString Ending = TokenOf(Statements[Look]);

		if (Ending != TEXT("EX_PopExecutionFlowIfNot") && Ending != TEXT("EX_JumpIfNot")) return false;

		Inside.Add(Where);

		if (Ending == TEXT("EX_JumpIfNot")) {
			Out.Leads.Add(TEXT("Completed"), Statements[Look].GetInteger(TEXT("CodeOffset"), INDEX_NONE));
		}

		if (!Carry() || !IsLet(TokenOf(Statements[Look]))) return false;
		if (WrittenTo(Statements[Look]) != Index) return false;
		if (ReadFrom(Statements[Look].GetObject(TEXT("Expression"))) != Counter) return false;

		Inside.Add(Where);

		if (!Carry() || TokenOf(Statements[Look]) != TEXT("EX_PushExecutionFlow")) return false;

		const int32 Raise = Statements[Look].GetInteger(TEXT("PushingAddress"), INDEX_NONE);

		Inside.Add(Where);

		if (!Carry()) return false;

		const FUObjectJsonValueExport Reading = Statements[Look].Has(TEXT("ContextExpression")) ? Statements[Look].GetObject(TEXT("ContextExpression")) : Statements[Look];

		if (CallsTo(Reading) == TEXT("Array_Get")) {
			const TArray<FUObjectJsonValueExport> Got = Reading.Has(TEXT("Parameters")) ? Reading.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

			if (Got.Num() >= 3 && Same(Got[0], Measured[0]) && ReadFrom(Got[1]) == Index) {
				const FString Element = ReadFrom(Got[2]);

				if (!Element.IsEmpty()) {
					Out.Handouts.Add(Element, TEXT("Array Element"));
				}

				Inside.Add(Where);

				if (!Carry()) return false;
			}
		}

		Out.Leads.Add(TEXT("LoopBody"), Where);

		const int32 Raising = Raise == INDEX_NONE ? INDEX_NONE : IndexOfAddress(Statements, Raise);

		if (Raising == INDEX_NONE || !IsLet(TokenOf(Statements[Raising]))) return false;
		if (CallsTo(Statements[Raising].GetObject(TEXT("Expression"))) != TEXT("Add_IntInt")) return false;

		Inside.Add(Raise);

		Where = Raise;

		if (!Carry() || WrittenTo(Statements[Look]) != Counter) return false;

		Inside.Add(Where);

		if (!Carry() || Where != Compare) return false;

		Out.Handouts.Add(Index, TEXT("Array Index"));

		Out.Inputs.Add(TEXT("Array"), Measured[0]);

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

REGISTER_MACRO(FForEachLoopMacro)
