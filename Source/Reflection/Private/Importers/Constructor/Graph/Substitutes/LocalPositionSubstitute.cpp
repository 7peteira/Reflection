#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/Graph/MaterialNodeSubstitute.h"

#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialFunctionInterface.h"

#include "Engine/Compatibility.h"
#include "Engine/Package.h"

static const TCHAR* LocalPositionFunctionPath = TEXT("/Engine/Functions/Engine_MaterialFunctions02/WorldPositionOffset/LocalPosition.LocalPosition");
static const TCHAR* LocalPositionExcludingOffsetsOutput = TEXT("Local Position (Excluding Offsets)");

static int32 FindOutputIndexByName(const UMaterialExpression* Expression, const FString& Name) {
	for (int32 Index = 0; Index < Expression->Outputs.Num(); Index++) {
		if (OutputNameToString(Expression->Outputs[Index].OutputName) == Name) {
			return Index;
		}
	}

	return INDEX_NONE;
}

class FLocalPositionSubstitute final : public FMaterialNodeSubstitute {
public:
	virtual bool Handles(const FName Type) const override {
		return Type == FName(TEXT("MaterialExpressionLocalPosition"));
	}

	virtual UMaterialExpression* Create(FMaterialSubstituteContext& Context) override {
		UMaterialFunctionInterface* Function = LoadObjectByPath<UMaterialFunctionInterface>(LocalPositionFunctionPath);

		if (Function == nullptr) {
			return nullptr;
		}

		UObject* Parent = Context.GetParent();
		const TSharedPtr<FJsonObject> Properties = Context.GetProperties();

		UMaterialExpressionMaterialFunctionCall* Call = NewObject<UMaterialExpressionMaterialFunctionCall>(
			Parent,
			UMaterialExpressionMaterialFunctionCall::StaticClass(),
			MakeUniqueObjectName(Parent, UMaterialExpressionMaterialFunctionCall::StaticClass(), *(Context.GetBaseName() + TEXT("_Function"))),
			RF_Transactional
		);

		Call->MaterialFunction = Function;
		Call->UpdateFromFunctionResource();

		Call->MaterialExpressionEditorX = Context.GetEditorX();
		Call->MaterialExpressionEditorY = Context.GetEditorY();

		FString LocalOrigin;

		if (Properties.IsValid() && Properties->TryGetStringField(TEXT("LocalOrigin"), LocalOrigin) && !LocalOrigin.Contains(TEXT("Instance"))) {
			GLog->Log(*FString::Printf(TEXT("Reflection: \"%s\" wanted local position origin %s, which the engine function has no output for"), *Context.GetBaseName(), *LocalOrigin));
		}

		FString IncludedOffsets;

		const bool bExcludeOffsets = Properties.IsValid()
			&& Properties->TryGetStringField(TEXT("IncludedOffsets"), IncludedOffsets)
			&& IncludedOffsets.Contains(TEXT("ExcludeOffsets"));

		if (!bExcludeOffsets) {
			return Call;
		}

		const int32 ExcludingOffsetsOutput = FindOutputIndexByName(Call, LocalPositionExcludingOffsetsOutput);

		if (ExcludingOffsetsOutput == INDEX_NONE) {
			GLog->Log(*FString::Printf(TEXT("Reflection: the engine's LocalPosition function has no \"%s\" output, so \"%s\" reads the offsets included one"), LocalPositionExcludingOffsetsOutput, *Context.GetBaseName()));

			return Call;
		}

		Context.AddExpression(Call);

		Call->MaterialExpressionEditorX = Context.GetEditorX() - 200;

		UMaterialExpressionReroute* Reroute = NewObject<UMaterialExpressionReroute>(
			Parent,
			UMaterialExpressionReroute::StaticClass(),
			Context.GetName(),
			RF_Transactional
		);

		Reroute->Input.Expression = Call;
		Reroute->Input.OutputIndex = ExcludingOffsetsOutput;
		Reroute->Desc = LocalPositionExcludingOffsetsOutput;

		return Reroute;
	}
};

REGISTER_MATERIAL_NODE_SUBSTITUTE(FLocalPositionSubstitute);
