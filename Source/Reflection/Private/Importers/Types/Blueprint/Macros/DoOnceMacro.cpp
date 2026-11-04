#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/MacroPattern.h"

using namespace MacroReading;

struct FDoOnceMacro final : FMacroPattern {
	virtual const TCHAR* GetName() const override { return TEXT("DoOnce"); }

	virtual bool Match(const TArray<FUObjectJsonValueExport>& Statements, const int32 At, FMacroMatch& Out) const override {
		if (!Statements.IsValidIndex(At)) return false;

		TSet<int32> Inside;

		const auto Land = [&Statements, &Inside](int32 Address) -> int32 {
			TSet<int32> Walked;

			while (Address != INDEX_NONE && !Walked.Contains(Address)) {
				Walked.Add(Address);

				const int32 Landed = IndexOfAddress(Statements, Address);

				if (Landed == INDEX_NONE) return INDEX_NONE;
				if (TokenOf(Statements[Landed]) != TEXT("EX_Jump")) return Landed;

				Inside.Add(Address);

				Address = Statements[Landed].GetInteger(TEXT("CodeOffset"), INDEX_NONE);
			}

			return INDEX_NONE;
		};

		const auto Next = [&Statements, &Land](const int32 Index) -> int32 {
			return Statements.IsValidIndex(Index + 1) ? Land(AddressOf(Statements[Index + 1])) : INDEX_NONE;
		};

		if (TokenOf(Statements[At]) != TEXT("EX_PushExecutionFlow")) return false;

		const int32 Testing = Statements[At].GetInteger(TEXT("PushingAddress"), INDEX_NONE);

		if (Testing == INDEX_NONE) return false;

		const int32 Asking = Next(At);

		if (Asking == INDEX_NONE || TokenOf(Statements[Asking]) != TEXT("EX_JumpIfNot")) return false;

		const FString Reached = ReadFrom(Statements[Asking].GetObject(TEXT("BooleanExpression")));

		if (!Reached.StartsWith(TEXT("Temp_bool_"))) return false;

		const int32 Settled = Next(Asking);

		if (Settled == INDEX_NONE || TokenOf(Statements[Settled]) != TEXT("EX_PopExecutionFlow")) return false;

		const int32 Settling = Land(Statements[Asking].GetInteger(TEXT("CodeOffset"), INDEX_NONE));

		if (Settling == INDEX_NONE || !IsLet(TokenOf(Statements[Settling]))) return false;
		if (WrittenTo(Statements[Settling]) != Reached) return false;

		const int32 Asked = Next(Settling);

		if (Asked == INDEX_NONE || TokenOf(Statements[Asked]) != TEXT("EX_PopExecutionFlowIfNot")) return false;

		const FUObjectJsonValueExport Closing = Statements[Asked].GetObject(TEXT("BooleanExpression"));

		const int32 Closed = Next(Asked);

		if (Closed == INDEX_NONE || !IsLet(TokenOf(Statements[Closed]))) return false;

		const FString Shut = WrittenTo(Statements[Closed]);

		if (!Shut.StartsWith(TEXT("Temp_bool_")) || Shut == Reached) return false;

		const int32 Ends = Next(Closed);

		if (Ends == INDEX_NONE || TokenOf(Statements[Ends]) != TEXT("EX_PopExecutionFlow")) return false;

		const int32 Gate = Land(Testing);

		if (Gate == INDEX_NONE || TokenOf(Statements[Gate]) != TEXT("EX_JumpIfNot")) return false;
		if (ReadFrom(Statements[Gate].GetObject(TEXT("BooleanExpression"))) != Shut) return false;

		const int32 Refused = Next(Gate);

		if (Refused == INDEX_NONE || TokenOf(Statements[Refused]) != TEXT("EX_PopExecutionFlow")) return false;

		const int32 Opening = Land(Statements[Gate].GetInteger(TEXT("CodeOffset"), INDEX_NONE));

		if (Opening == INDEX_NONE || !IsLet(TokenOf(Statements[Opening]))) return false;
		if (WrittenTo(Statements[Opening]) != Shut) return false;

		const int32 Through = Next(Opening);

		if (Through == INDEX_NONE) return false;

		Out.Leads.Add(TEXT("Completed"), AddressOf(Statements[Through]));

		Out.Inputs.Add(TEXT("Start Closed"), Closing);

		for (const int32 Which : { At, Asking, Settled, Settling, Asked, Closed, Ends, Gate, Refused, Opening }) {
			Inside.Add(AddressOf(Statements[Which]));
		}

		for (int32 Look = 0; Look < Statements.Num(); ++Look) {
			if (Inside.Contains(AddressOf(Statements[Look])) || !IsLet(TokenOf(Statements[Look]))) continue;

			if (WrittenTo(Statements[Look]) != Shut) continue;
			if (TokenOf(Statements[Look].GetObject(TEXT("Expression"))) != TEXT("EX_False")) continue;

			Out.Takes.Add(Look, TEXT("Reset"));

			Inside.Add(AddressOf(Statements[Look]));

			if (const int32 Marked = Next(Look); Marked != INDEX_NONE && IsLet(TokenOf(Statements[Marked])) && WrittenTo(Statements[Marked]) == Reached) {
				Inside.Add(AddressOf(Statements[Marked]));

				if (const int32 Done = Next(Marked); Done != INDEX_NONE && TokenOf(Statements[Done]) == TEXT("EX_PopExecutionFlow")) {
					Inside.Add(AddressOf(Statements[Done]));
				}
			}

			break;
		}

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

REGISTER_MACRO(FDoOnceMacro)
