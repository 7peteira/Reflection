#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/Graph/MaterialNodeSubstitute.h"

#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"

#include "Utilities/JsonHelpers.h"

static constexpr int32 ConvertColumnWidth = 160;
static constexpr int32 ConvertRowHeight = 80;

static int32 GetConvertComponentCount(const FString& ConvertType) {
	if (ConvertType.EndsWith(TEXT("Vector4"))) return 4;
	if (ConvertType.EndsWith(TEXT("Vector3"))) return 3;
	if (ConvertType.EndsWith(TEXT("Vector2"))) return 2;

	return 1;
}

static float GetLinearColorComponent(const FLinearColor& Color, const int32 ComponentIndex) {
	switch (ComponentIndex) {
		case 1: return Color.G;
		case 2: return Color.B;
		case 3: return Color.A;
		default: return Color.R;
	}
}

class FConvertSubstitute final : public FMaterialNodeSubstitute {
public:
	virtual bool Handles(const FName Type) const override {
		return Type == FName(TEXT("MaterialExpressionConvert"));
	}

	virtual UMaterialExpression* Create(FMaterialSubstituteContext& Context) override {
		const TSharedPtr<FJsonObject> Properties = Context.GetProperties();

		if (Context.GetParent() == nullptr || !Properties.IsValid()) {
			return nullptr;
		}

		TArray<TSharedPtr<FJsonValue>> ConvertInputs;
		TArray<TSharedPtr<FJsonValue>> ConvertOutputs;
		TArray<TSharedPtr<FJsonValue>> ConvertMappings;

		const TArray<TSharedPtr<FJsonValue>>* ArrayField;
		if (Properties->TryGetArrayField(TEXT("ConvertInputs"), ArrayField)) ConvertInputs = *ArrayField;
		if (Properties->TryGetArrayField(TEXT("ConvertOutputs"), ArrayField)) ConvertOutputs = *ArrayField;
		if (Properties->TryGetArrayField(TEXT("ConvertMappings"), ArrayField)) ConvertMappings = *ArrayField;

		if (ConvertOutputs.Num() == 0) {
			return nullptr;
		}

		FString NodeName = TEXT("Convert");
		Properties->TryGetStringField(TEXT("NodeName"), NodeName);

		const int32 BaseX = Context.GetEditorX();
		const int32 BaseY = Context.GetEditorY();

		TArray<UMaterialExpression*> Roots;
		int32 RowY = BaseY;

		for (int32 OutputIndex = 0; OutputIndex < ConvertOutputs.Num(); OutputIndex++) {
			FString OutputType = TEXT("Scalar");
			FLinearColor OutputDefault = FLinearColor::Black;

			if (const TSharedPtr<FJsonObject> Output = ConvertOutputs[OutputIndex]->AsObject()) {
				Output->TryGetStringField(TEXT("Type"), OutputType);

				const TSharedPtr<FJsonObject>* DefaultValue;
				if (Output->TryGetObjectField(TEXT("DefaultValue"), DefaultValue)) {
					OutputDefault = ObjectToLinearColor(DefaultValue->Get());
				}
			}

			const int32 ComponentCount = GetConvertComponentCount(OutputType);
			TArray<UMaterialExpression*> Components;

			for (int32 ComponentIndex = 0; ComponentIndex < ComponentCount; ComponentIndex++) {
				const int32 NodeY = RowY + ComponentIndex * ConvertRowHeight;
				const FString NameSuffix = FString::Printf(TEXT("_Out%d_C%d"), OutputIndex, ComponentIndex);

				TSharedPtr<FJsonObject> Mapping;

				for (const TSharedPtr<FJsonValue>& MappingValue : ConvertMappings) {
					const TSharedPtr<FJsonObject> MappingObject = MappingValue->AsObject();
					if (!MappingObject.IsValid()) continue;

					int32 MappedOutput = 0;
					int32 MappedOutputComponent = 0;
					MappingObject->TryGetNumberField(TEXT("OutputIndex"), MappedOutput);
					MappingObject->TryGetNumberField(TEXT("OutputComponentIndex"), MappedOutputComponent);

					if (MappedOutput == OutputIndex && MappedOutputComponent == ComponentIndex) {
						Mapping = MappingObject;
						break;
					}
				}

				if (!Mapping.IsValid()) {
					UMaterialExpressionConstant* Constant = Context.CreateExpression<UMaterialExpressionConstant>(NameSuffix, BaseX, NodeY);
					Constant->R = GetLinearColorComponent(OutputDefault, ComponentIndex);

					Components.Add(Constant);
					continue;
				}

				int32 InputIndex = 0;
				int32 InputComponentIndex = 0;
				Mapping->TryGetNumberField(TEXT("InputIndex"), InputIndex);
				Mapping->TryGetNumberField(TEXT("InputComponentIndex"), InputComponentIndex);

				TSharedPtr<FJsonObject> ExpressionInput;
				FLinearColor InputDefault = FLinearColor::Black;

				if (ConvertInputs.IsValidIndex(InputIndex)) {
					if (const TSharedPtr<FJsonObject> Input = ConvertInputs[InputIndex]->AsObject()) {
						const TSharedPtr<FJsonObject>* DefaultValue;
						if (Input->TryGetObjectField(TEXT("DefaultValue"), DefaultValue)) {
							InputDefault = ObjectToLinearColor(DefaultValue->Get());
						}

						const TSharedPtr<FJsonObject>* InputObject;
						if (Input->TryGetObjectField(TEXT("ExpressionInput"), InputObject)) {
							if (InputObject->Get()->HasField(TEXT("Expression")) || InputObject->Get()->HasField(TEXT("ExpressionName"))) {
								ExpressionInput = *InputObject;
							}
						}
					}
				}

				if (!ExpressionInput.IsValid()) {
					UMaterialExpressionConstant* Constant = Context.CreateExpression<UMaterialExpressionConstant>(NameSuffix, BaseX, NodeY);
					Constant->R = GetLinearColorComponent(InputDefault, InputComponentIndex);

					Components.Add(Constant);
					continue;
				}

				UMaterialExpressionComponentMask* Mask = Context.CreateExpression<UMaterialExpressionComponentMask>(NameSuffix, BaseX, NodeY);
				Mask->R = InputComponentIndex == 0 ? 1 : 0;
				Mask->G = InputComponentIndex == 1 ? 1 : 0;
				Mask->B = InputComponentIndex == 2 ? 1 : 0;
				Mask->A = InputComponentIndex == 3 ? 1 : 0;

				Context.ConnectLater(Mask, SingleInput, ExpressionInput);
				Components.Add(Mask);
			}

			UMaterialExpression* Root = Components[0];

			for (int32 ComponentIndex = 1; ComponentIndex < Components.Num(); ComponentIndex++) {
				UMaterialExpressionAppendVector* Append = Context.CreateExpression<UMaterialExpressionAppendVector>(
					FString::Printf(TEXT("_Out%d_Append%d"), OutputIndex, ComponentIndex),
					BaseX + ComponentIndex * ConvertColumnWidth,
					RowY
				);

				Append->A.Connect(0, Root);
				Append->B.Connect(0, Components[ComponentIndex]);

				Root = Append;
			}

			Root->Desc = NodeName;

			Roots.Add(Root);
			RowY += (ComponentCount + 1) * ConvertRowHeight;
		}

		TArray<UObject*> RootObjects;
		RootObjects.Reserve(Roots.Num());

		for (UMaterialExpression* Root : Roots) {
			RootObjects.Add(Root);
		}

		Context.RegisterOutputRoots(Roots[0], RootObjects);

		GLog->Log(*FString::Printf(TEXT("Reflection: Rebuilt Convert node \"%s\" (%s) out of masks and appends"), *NodeName, *Context.GetBaseName()));

		return Roots[0];
	}
};

REGISTER_MATERIAL_NODE_SUBSTITUTE(FConvertSubstitute);
