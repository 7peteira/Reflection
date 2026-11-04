#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/Graph/MaterialNodeSubstitute.h"

#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionSubtract.h"

static constexpr int32 InverseLerpColumnWidth = 190;
static constexpr int32 InverseLerpRowHeight = 110;

static constexpr float InverseLerpConstA = 0.0f;
static constexpr float InverseLerpConstB = 1.0f;
static constexpr float InverseLerpConstValue = 0.0f;

class FInverseLerpSubstitute final : public FMaterialNodeSubstitute {
public:
	virtual bool Handles(const FName Type) const override {
		return Type == FName(TEXT("MaterialExpressionInverseLinearInterpolate"));
	}

	virtual UMaterialExpression* Create(FMaterialSubstituteContext& Context) override {
		const TSharedPtr<FJsonObject> Properties = Context.GetProperties();

		const int32 EditorX = Context.GetEditorX();
		const int32 EditorY = Context.GetEditorY();

		UMaterialExpressionSubtract* Numerator = Context.CreateExpression<UMaterialExpressionSubtract>(
			TEXT("_ValueMinusA"),
			EditorX - InverseLerpColumnWidth * 2,
			EditorY
		);

		UMaterialExpressionSubtract* Denominator = Context.CreateExpression<UMaterialExpressionSubtract>(
			TEXT("_BMinusA"),
			EditorX - InverseLerpColumnWidth * 2,
			EditorY + InverseLerpRowHeight
		);

		auto ConnectInput = [&](UMaterialExpression* Target, const int32 InputIndex, const TCHAR* NodeInput, const TCHAR* NodeConstant, const float DefaultConstant, float& SlotConstant) {
			if (const TSharedPtr<FJsonObject> ExpressionInput = Context.FindConnectedInput(NodeInput)) {
				Context.ConnectLater(Target, InputIndex, ExpressionInput);

				return;
			}

			SlotConstant = Context.ReadInputConstant(NodeConstant, DefaultConstant);
		};

		ConnectInput(Numerator, BinaryInputA, TEXT("Value"), TEXT("ConstValue"), InverseLerpConstValue, Numerator->ConstA);
		ConnectInput(Numerator, BinaryInputB, TEXT("A"), TEXT("ConstA"), InverseLerpConstA, Numerator->ConstB);

		ConnectInput(Denominator, BinaryInputA, TEXT("B"), TEXT("ConstB"), InverseLerpConstB, Denominator->ConstA);
		ConnectInput(Denominator, BinaryInputB, TEXT("A"), TEXT("ConstA"), InverseLerpConstA, Denominator->ConstB);

		UMaterialExpressionDivide* Divide = Context.CreateExpression<UMaterialExpressionDivide>(
			TEXT("_Divide"),
			EditorX - InverseLerpColumnWidth,
			EditorY
		);

		Divide->A.Connect(0, Numerator);
		Divide->B.Connect(0, Denominator);

		UMaterialExpression* Result = Divide;

		bool bClampResult = false;

		if (Properties.IsValid()) {
			Properties->TryGetBoolField(TEXT("bClampResult"), bClampResult);
		}

		if (bClampResult) {
			UMaterialExpressionClamp* Clamp = Context.CreateExpression<UMaterialExpressionClamp>(
				TEXT("_Clamp"),
				EditorX - InverseLerpColumnWidth / 2,
				EditorY
			);

			Clamp->ClampMode = CMODE_Clamp;
			Clamp->MinDefault = 0.0f;
			Clamp->MaxDefault = 1.0f;

			Clamp->Input.Connect(0, Divide);

			Result = Clamp;
		}

		UMaterialExpressionReroute* Reroute = NewObject<UMaterialExpressionReroute>(
			Context.GetParent(),
			UMaterialExpressionReroute::StaticClass(),
			Context.GetName(),
			RF_Transactional
		);

		Reroute->Input.Connect(0, Result);
		Reroute->Desc = TEXT("InvLerp");

		GLog->Log(*FString::Printf(TEXT("Reflection: Rebuilt MaterialExpressionInverseLinearInterpolate \"%s\" out of subtracts and a divide"), *Context.GetBaseName()));

		return Reroute;
	}
};

REGISTER_MATERIAL_NODE_SUBSTITUTE(FInverseLerpSubstitute);
