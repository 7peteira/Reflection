#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Animation/LegacyCurves.h"

#include "Importers/Constructor/ImportIssues.h"
#include "Modules/Cloud/Cloud.h"
#include "Modules/Metadata.h"
#include "Engine/Log.h"

namespace {
	void Solve(const TArray<double>& AtA, const TArray<double>& Atb, const double Step, TArray<double>& InOutX) {
		const int32 Curves = Atb.Num();

		TArray<double> Gradient;
		Gradient.SetNumZeroed(Curves);

		for (int32 Pass = 0; Pass < 300; ++Pass) {
			for (int32 Row = 0; Row < Curves; ++Row) {
				double Sum = -Atb[Row];

				for (int32 Column = 0; Column < Curves; ++Column) {
					Sum += AtA[Row * Curves + Column] * InOutX[Column];
				}

				Gradient[Row] = Sum;
			}

			for (int32 Row = 0; Row < Curves; ++Row) {
				InOutX[Row] = FMath::Max(0.0, InOutX[Row] - Gradient[Row] * Step);
			}
		}
	}

	double Sample(const TArray<TPair<double, double>>& Keys, const double Time) {
		if (Keys.Num() == 0) return 0.0;

		if (Time <= Keys[0].Key) return Keys[0].Value;
		if (Time >= Keys.Last().Key) return Keys.Last().Value;

		for (int32 Index = 1; Index < Keys.Num(); ++Index) {
			if (Keys[Index].Key < Time) continue;

			const double Span = Keys[Index].Key - Keys[Index - 1].Key;

			if (Span <= 0.0) return Keys[Index].Value;

			const double Along = (Time - Keys[Index - 1].Key) / Span;

			return FMath::Lerp(Keys[Index - 1].Value, Keys[Index].Value, Along);
		}

		return Keys.Last().Value;
	}

	FString NameOf(const TSharedPtr<FJsonObject>& Curve) {
		FString DisplayName;

		if (const TSharedPtr<FJsonObject>* Name = nullptr; Curve->TryGetObjectField(TEXT("Name"), Name)) {
			(*Name)->TryGetStringField(TEXT("DisplayName"), DisplayName);
		} else {
			Curve->TryGetStringField(TEXT("CurveName"), DisplayName);
		}

		return DisplayName;
	}
}

bool FReflectionLegacyCurves::Read(TMap<FName, TArray<FLegacyCurveDrive>>& OutByCurve) {
	const TSharedPtr<FJsonObject> Response = Cloud::Export::GetCurveMappingBlocking();

	if (!Response.IsValid()) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The Cloud hasn't got the legacy curve mapping"),
			TEXT("It says how this rig's controls are made of an older head's curves, and it is read from the project the Cloud has open. Without it the rig keeps its own curve names.")
		);

		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : Response->GetArrayField(TEXT("entries"))) {
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;

		FString Target;

		if (!Entry.IsValid() || !Entry->TryGetStringField(TEXT("target"), Target)) continue;

		const TArray<TSharedPtr<FJsonValue>>* Constants = nullptr;

		if (!Entry->TryGetArrayField(TEXT("constants"), Constants)) continue;

		for (const TSharedPtr<FJsonValue>& One : *Constants) {
			const TSharedPtr<FJsonObject> Constant = One.IsValid() ? One->AsObject() : nullptr;

			FString Name;
			double Weight = 0.0;

			if (!Constant.IsValid() || !Constant->TryGetStringField(TEXT("name"), Name)) continue;
			if (!Constant->TryGetNumberField(TEXT("weight"), Weight)) continue;

			OutByCurve.FindOrAdd(FName(*Name)).Add({ FName(*Target), static_cast<float>(Weight) });
		}
	}

	return OutByCurve.Num() > 0;
}

bool FReflectionLegacyCurves::Rewrite(TArray<TSharedPtr<FJsonValue>>& FloatCurves, const FString& Named, const bool bKeepControls) {
	if (FloatCurves.Num() == 0) return false;

	TMap<FName, TArray<FLegacyCurveDrive>> ByCurve;

	if (!Read(ByCurve)) return false;

	TMap<FString, TMap<FName, float>> ByControl;

	for (const TPair<FName, TArray<FLegacyCurveDrive>>& One : ByCurve) {
		for (const FLegacyCurveDrive& Drive : One.Value) {
			ByControl.FindOrAdd(Drive.Control.ToString().ToLower()).Add(One.Key, Drive.Weight);
		}
	}

	TArray<TSharedPtr<FJsonObject>> Rows;
	TArray<TSharedPtr<FJsonValue>> Kept;

	TSet<FName> Involved;

	double Flags = 0.0;

	for (const TSharedPtr<FJsonValue>& One : FloatCurves) {
		const TSharedPtr<FJsonObject> Curve = One.IsValid() ? One->AsObject() : nullptr;

		if (!Curve.IsValid()) continue;

		const TMap<FName, float>* Drives = ByControl.Find(NameOf(Curve).ToLower());

		if (Drives == nullptr) {
			Kept.Add(One);

			continue;
		}

		Rows.Add(Curve);

		Curve->TryGetNumberField(TEXT("CurveTypeFlags"), Flags);

		for (const TPair<FName, float>& Drive : *Drives) {
			Involved.Add(Drive.Key);
		}
	}

	if (Rows.Num() == 0) return false;

	TArray<FName> Curves = Involved.Array();
	Curves.Sort([](const FName& Left, const FName& Right) { return Left.Compare(Right) < 0; });

	const int32 Height = Rows.Num();
	const int32 Width = Curves.Num();

	TArray<double> A;
	A.SetNumZeroed(Height * Width);

	TArray<TArray<TPair<double, double>>> Keyed;
	Keyed.SetNum(Height);

	TSet<double> Moments;

	for (int32 Row = 0; Row < Height; ++Row) {
		const TMap<FName, float>& Drives = ByControl[NameOf(Rows[Row]).ToLower()];

		for (int32 Column = 0; Column < Width; ++Column) {
			if (const float* Weight = Drives.Find(Curves[Column])) {
				A[Row * Width + Column] = *Weight;
			}
		}

		if (const TSharedPtr<FJsonObject>* Float = nullptr; Rows[Row]->TryGetObjectField(TEXT("FloatCurve"), Float)) {
			if (const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr; (*Float)->TryGetArrayField(TEXT("Keys"), Keys)) {
				for (const TSharedPtr<FJsonValue>& KeyValue : *Keys) {
					const TSharedPtr<FJsonObject> Key = KeyValue.IsValid() ? KeyValue->AsObject() : nullptr;

					double Time = 0.0;
					double Value = 0.0;

					if (!Key.IsValid() || !Key->TryGetNumberField(TEXT("Time"), Time)) continue;

					Key->TryGetNumberField(TEXT("Value"), Value);

					Keyed[Row].Add(MakeTuple(Time, Value));
					Moments.Add(Time);
				}
			}
		}

		Keyed[Row].Sort([](const TPair<double, double>& Left, const TPair<double, double>& Right) {
			return Left.Key < Right.Key;
		});
	}

	if (Moments.Num() == 0) return false;

	TArray<double> Times = Moments.Array();
	Times.Sort();

	TArray<double> AtA;
	AtA.SetNumZeroed(Width * Width);

	for (int32 Left = 0; Left < Width; ++Left) {
		for (int32 Right = 0; Right < Width; ++Right) {
			double Sum = 0.0;

			for (int32 Row = 0; Row < Height; ++Row) {
				Sum += A[Row * Width + Left] * A[Row * Width + Right];
			}

			AtA[Left * Width + Right] = Sum;
		}
	}

	double Stretch = 0.0;

	for (const double Entry : AtA) {
		Stretch += Entry * Entry;
	}

	Stretch = FMath::Sqrt(Stretch);

	if (Stretch <= 0.0) return false;

	const double Step = 1.0 / Stretch;

	TArray<TArray<double>> Solved;
	Solved.SetNum(Times.Num());

	TArray<double> X;
	X.SetNumZeroed(Width);

	TArray<double> Atb;
	Atb.SetNumZeroed(Width);

	for (int32 Frame = 0; Frame < Times.Num(); ++Frame) {
		for (int32 Column = 0; Column < Width; ++Column) {
			double Sum = 0.0;

			for (int32 Row = 0; Row < Height; ++Row) {
				Sum += A[Row * Width + Column] * Sample(Keyed[Row], Times[Frame]);
			}

			Atb[Column] = Sum;
		}

		Solve(AtA, Atb, Step, X);

		Solved[Frame] = X;
	}

	int32 Still = 0;

	for (int32 Column = 0; Column < Width; ++Column) {
		double Most = 0.0;

		for (const TArray<double>& Frame : Solved) {
			Most = FMath::Max(Most, Frame[Column]);
		}

		if (FMath::IsNearlyZero(Most)) {
			Still++;

			continue;
		}

		TArray<TSharedPtr<FJsonValue>> Keys;

		for (int32 Frame = 0; Frame < Times.Num(); ++Frame) {
			const TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();

			Key->SetStringField(TEXT("InterpMode"), TEXT("RCIM_Linear"));
			Key->SetStringField(TEXT("TangentMode"), TEXT("RCTM_Auto"));
			Key->SetStringField(TEXT("TangentWeightMode"), TEXT("RCTWM_WeightedNone"));
			Key->SetNumberField(TEXT("Time"), Times[Frame]);
			Key->SetNumberField(TEXT("Value"), Solved[Frame][Column]);
			Key->SetNumberField(TEXT("ArriveTangent"), 0.0);
			Key->SetNumberField(TEXT("ArriveTangentWeight"), 0.0);
			Key->SetNumberField(TEXT("LeaveTangent"), 0.0);
			Key->SetNumberField(TEXT("LeaveTangentWeight"), 0.0);

			Keys.Add(MakeShared<FJsonValueObject>(Key));
		}

		const TSharedPtr<FJsonObject> Float = MakeShared<FJsonObject>();
		Float->SetArrayField(TEXT("Keys"), Keys);

		const TSharedPtr<FJsonObject> Name = MakeShared<FJsonObject>();
		Name->SetStringField(TEXT("DisplayName"), Curves[Column].ToString());

		const TSharedPtr<FJsonObject> Curve = MakeShared<FJsonObject>();
		Curve->SetObjectField(TEXT("Name"), Name);
		Curve->SetStringField(TEXT("CurveName"), Curves[Column].ToString());
		Curve->SetNumberField(TEXT("CurveTypeFlags"), Flags);
		Curve->SetObjectField(TEXT("FloatCurve"), Float);

		Kept.Add(MakeShared<FJsonValueObject>(Curve));
	}

	if (bKeepControls) {
		for (const TSharedPtr<FJsonObject>& Row : Rows) {
			Kept.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

	FloatCurves = MoveTemp(Kept);

	UE_LOG(LogReflection, Display,
		TEXT("\"%s\" read %d of this rig's control(s) back into %d of an older head's curve(s) over %d frame(s)%s, and %s"),
		*Named, Height, Width - Still, Times.Num(),
		Still > 0 ? *FString::Printf(TEXT(", %d the animation never moved"), Still) : TEXT(""),
		bKeepControls ? TEXT("kept the controls beside them") : TEXT("wrote them in place of the controls"));

	return true;
}
