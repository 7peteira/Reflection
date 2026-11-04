#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/Graph/MaterialNodeSubstitute.h"

#include "Importers/Constructor/Graph/MaterialGraph.h"

#include "Materials/MaterialExpressionConstant.h"

#include "Serializers/PropertySerializer.h"

FMaterialNodeSubstituteRegistry& FMaterialNodeSubstituteRegistry::Get() {
	static FMaterialNodeSubstituteRegistry Registry;

	return Registry;
}

void FMaterialNodeSubstituteRegistry::Register(const TSharedRef<FMaterialNodeSubstitute>& Substitute) {
	Substitutes.Add(Substitute);

	Substitutes.StableSort([](const TSharedRef<FMaterialNodeSubstitute>& Left, const TSharedRef<FMaterialNodeSubstitute>& Right) {
		return Left->GetPriority() > Right->GetPriority();
	});
}

FMaterialSubstituteContext::FMaterialSubstituteContext(IMaterialGraph& InGraph, FUObjectExportContainer* InContainer)
	: Graph(InGraph)
	, Container(InContainer) {
}

void FMaterialSubstituteContext::SetExport(FUObjectExport* InExport) {
	Export = InExport;
	Properties = Export != nullptr ? Export->GetProperties() : nullptr;

	EditorX = 0;
	EditorY = 0;

	if (Properties.IsValid()) {
		Properties->TryGetNumberField(TEXT("MaterialExpressionEditorX"), EditorX);
		Properties->TryGetNumberField(TEXT("MaterialExpressionEditorY"), EditorY);
	}
}

FName FMaterialSubstituteContext::GetType() const {
	return Export != nullptr ? Export->GetType() : NAME_None;
}

FName FMaterialSubstituteContext::GetName() const {
	return Export != nullptr ? Export->GetName() : NAME_None;
}

FString FMaterialSubstituteContext::GetBaseName() const {
	return GetName().ToString();
}

UObject* FMaterialSubstituteContext::GetParent() const {
	return Export != nullptr ? Export->Parent : nullptr;
}

UMaterialExpression* FMaterialSubstituteContext::CreateExpression(UClass* Class, const FString& NameSuffix, const int32 X, const int32 Y) {
	UObject* Parent = GetParent();

	if (Parent == nullptr || Class == nullptr) {
		return nullptr;
	}

	UMaterialExpression* Expression = NewObject<UMaterialExpression>(
		Parent,
		Class,
		MakeUniqueObjectName(Parent, Class, *(GetBaseName() + NameSuffix)),
		RF_Transactional
	);

	Expression->MaterialExpressionEditorX = X;
	Expression->MaterialExpressionEditorY = Y;

	AddExpression(Expression);

	return Expression;
}

UMaterialExpressionConstant* FMaterialSubstituteContext::CreateConstant(const float Value, const TCHAR* InputName, const int32 X, const int32 Y) {
	UMaterialExpressionConstant* Constant = CreateExpression<UMaterialExpressionConstant>(FString(TEXT("_")) + InputName, X, Y);

	if (Constant == nullptr) {
		return nullptr;
	}

	Constant->R = Value;
	Constant->Desc = InputName;

	return Constant;
}

void FMaterialSubstituteContext::AddExpression(UMaterialExpression* Expression) {
	UObject* Parent = GetParent();

	if (Parent == nullptr || Expression == nullptr) {
		return;
	}

	IMaterialGraph::SetExpressionParent(Parent, Expression, Properties);
	IMaterialGraph::AddExpressionToParent(Parent, Expression);
}

TSharedPtr<FJsonObject> FMaterialSubstituteContext::FindConnectedInput(const TCHAR* InputName) const {
	const TSharedPtr<FJsonObject>* Input = nullptr;

	if (!Properties.IsValid() || !Properties->TryGetObjectField(InputName, Input)) {
		return nullptr;
	}

	const TSharedPtr<FJsonValue> Expression = (*Input)->TryGetField(TEXT("Expression"));

	if (Expression.IsValid() && !Expression->IsNull()) {
		return *Input;
	}

	FString ExpressionName;

	if ((*Input)->TryGetStringField(TEXT("ExpressionName"), ExpressionName) && !ExpressionName.IsEmpty() && ExpressionName != TEXT("None")) {
		return *Input;
	}

	return nullptr;
}

float FMaterialSubstituteContext::ReadInputConstant(const TCHAR* PropertyName, const float DefaultValue) const {
	double Value = DefaultValue;

	if (Properties.IsValid()) {
		Properties->TryGetNumberField(PropertyName, Value);
	}

	return static_cast<float>(Value);
}

void FMaterialSubstituteContext::ConnectLater(UMaterialExpression* Expression, const int32 InputIndex, const TSharedPtr<FJsonObject>& ExpressionInput) {
	if (Expression == nullptr || !ExpressionInput.IsValid()) {
		return;
	}

	Defer([Expression, InputIndex, ExpressionInput](FMaterialSubstituteContext& Context) {
		FExpressionInput* Input = Expression->GetInput(InputIndex);

		if (Input == nullptr) {
			return;
		}

		UMaterialExpression* Source = Context.GetContainer()->Find<UMaterialExpression>(IMaterialGraph::GetExpressionName(ExpressionInput.Get()));

		if (Source == nullptr) {
			return;
		}

		const FName SlotName = Input->InputName;

		*Input = IMaterialGraph::PopulateExpressionInput(ExpressionInput.Get(), Source);
		Input->InputName = SlotName;

		Context.GetGraph().RemapConvertOutput(*Input);
	});
}

void FMaterialSubstituteContext::Defer(TFunction<void(FMaterialSubstituteContext&)> Work) {
	Deferred.Add(MoveTemp(Work));
}

void FMaterialSubstituteContext::ResolveDeferred() {
	for (int32 Index = 0; Index < Deferred.Num(); Index++) {
		Deferred[Index](*this);
	}

	Deferred.Empty();
}

void FMaterialSubstituteContext::RegisterOutputRoots(UMaterialExpression* Root, const TArray<UObject*>& Roots) {
	if (Root == nullptr || Graph.GetPropertySerializer() == nullptr) {
		return;
	}

	Graph.GetPropertySerializer()->ConvertOutputRoots.Add(Root, Roots);
}
