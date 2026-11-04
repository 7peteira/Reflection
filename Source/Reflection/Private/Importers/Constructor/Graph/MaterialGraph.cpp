#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/Graph/MaterialGraph.h"

#include "Importers/Constructor/Graph/MaterialNodeSubstitute.h"

#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "SceneTypes.h"
#include "Engine/EngineUtilities.h"
#include "Importers/Constructor/ImportIssues.h"
#include "Utilities/JsonHelpers.h"

#if ENGINE_UE5
#include "Materials/MaterialExpressionTextureBase.h"
#endif

#if UE5_2_BEYOND
#include "Materials/MaterialAttributeDefinitionMap.h"
#else
#include "MaterialShared.h"
#endif

#if UE4_25_BELOW
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionTextureBase.h"
#endif

TSharedPtr<FJsonObject> IMaterialGraph::FindMaterialData(const FString& Type, FUObjectExportContainer* Container) {
	TSharedPtr<FJsonObject> EditorOnlyData;

	for (FUObjectExport* Export : GetContainer()->Exports) {
		FString ExportType = Export->GetType().ToString();

		if (ExportType == Type + "EditorOnlyData") {
			EditorOnlyData = Export->JsonObject;
			continue;
		}

		if (ExportType == Type) {
			EditorOnlyData = Export->JsonObject;
			continue;
		}

		Export->Parent = AssetExport->Object;
		Container->Exports.Add(Export);
	}

	return EditorOnlyData->GetObjectField(TEXT("Properties"));
}

bool IMaterialGraph::HasNullExpressions(const TSharedPtr<FJsonObject>& Properties) {
	const TArray<TSharedPtr<FJsonValue>>* Expressions = nullptr;
	const TSharedPtr<FJsonObject>* ExpressionCollection;

	if (Properties->TryGetObjectField(TEXT("ExpressionCollection"), ExpressionCollection)) {
		(*ExpressionCollection)->TryGetArrayField(TEXT("Expressions"), Expressions);
	} else {
		Properties->TryGetArrayField(TEXT("Expressions"), Expressions);
	}

	if (Expressions == nullptr) {
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Expression : *Expressions) {
		if (!Expression.IsValid() || Expression->IsNull()) {
			return true;
		}
	}

	return false;
}

void IMaterialGraph::ConstructExpressions(FUObjectExportContainer* Container) {
	FMaterialSubstituteContext Context(*this, Container);

	for (FUObjectExport* Export : Container->Exports) {
		if (!Export->JsonObject.IsValid()) {
			continue;
		}

		UObject* Expression = Export->Object;

		if (Expression == nullptr) {
			Expression = CreateEmptyExpression(Export, Context);
		}

		if (Expression == nullptr) {
			continue;
		}

		Export->Object = Expression;
	}

	Context.ResolveDeferred();
}

void IMaterialGraph::PropagateExpressions(FUObjectExportContainer* Container) {
	for (FUObjectExport* Export : Container->Exports) {
		UObject* Parent = Export->Parent;

		TSharedPtr<FJsonObject> ExportJsonObject = Export->JsonObject;
		TSharedPtr<FJsonObject> Properties = Export->GetProperties();

		UMaterialExpression* Expression = Export->Get<UMaterialExpression>();

		if (Expression == nullptr) {
			continue;
		}

		if (IsConvertSubstitute(Expression)) {
			continue;
		}

		if (Properties->HasField(TEXT("SubgraphExpression"))) {
#if ENGINE_UE5
			const TSharedPtr<FJsonObject> SubGraphExpressionObject = Properties->GetObjectField(TEXT("SubgraphExpression"));

			const FName SubGraphExpressionName = GetExportNameOfSubobject(SubGraphExpressionObject->GetStringField(TEXT("ObjectName")));
			const FUObjectExport* SubGraphExport = Container->Find(SubGraphExpressionName);

			Expression->SubgraphExpression = SubGraphExport->Get<UMaterialExpression>();
#endif
		}

		GetObjectSerializer()->DeserializeObjectProperties(Properties, Expression);
		ClampCustomPrimitiveDataIndex(Expression);
		SetExpressionParent(Parent, Expression, Properties);

		AddExpressionToParent(Parent, Expression);
	}
}

void IMaterialGraph::ClampCustomPrimitiveDataIndex(UMaterialExpression* Expression) {
	if (Expression == nullptr) {
		return;
	}

#if !UE4_22_BELOW
	const FBoolProperty* UsesCustomData = FindFProperty<FBoolProperty>(Expression->GetClass(), TEXT("bUseCustomPrimitiveData"));

	if (UsesCustomData == nullptr || !UsesCustomData->GetPropertyValue_InContainer(Expression)) {
		return;
	}

	const FNumericProperty* IndexProperty = FindFProperty<FNumericProperty>(Expression->GetClass(), TEXT("PrimitiveDataIndex"));

	if (IndexProperty == nullptr) {
		return;
	}

	void* IndexValue = IndexProperty->ContainerPtrToValuePtr<void>(Expression);

	constexpr int64 LastIndex = FCustomPrimitiveData::NumCustomPrimitiveDataFloats - 1;
	const int64 Index = IndexProperty->GetSignedIntPropertyValue(IndexValue);

	if (Index <= LastIndex) {
		return;
	}

	IndexProperty->SetIntPropertyValue(IndexValue, LastIndex);

	GLog->Log(*FString::Printf(
		TEXT("Reflection: \"%s\" reads custom primitive data %lld, which this engine stops at %lld"),
		*Expression->GetName(), Index, LastIndex
	));
#endif
}

bool HasMaterialEditorOnlyData(const UObject* Object) {
	if (Object == nullptr) return false;

	const FObjectProperty* Held = FindFProperty<FObjectProperty>(Object->GetClass(), TEXT("EditorOnlyData"));

	return Held == nullptr || Held->GetObjectPropertyValue_InContainer(Object) != nullptr;
}

namespace {
	bool IsMissingMaterialAttribute(const FGuid& AttributeID) {
		return !AttributeID.IsValid() || FMaterialAttributeDefinitionMap::GetProperty(AttributeID) == MP_MAX;
	}

	struct FGraphConnection {
		FObjectProperty* ExpressionProperty = nullptr;
		FIntProperty* OutputIndexProperty = nullptr;
		void* StructValue = nullptr;

		bool Bind(const UStruct* Struct) {
			ExpressionProperty = FindFProperty<FObjectProperty>(Struct, TEXT("Expression"));
			OutputIndexProperty = FindFProperty<FIntProperty>(Struct, TEXT("OutputIndex"));

			return ExpressionProperty != nullptr && OutputIndexProperty != nullptr;
		}

		UObject* GetExpression() const {
			return ExpressionProperty->GetObjectPropertyValue(ExpressionProperty->ContainerPtrToValuePtr<void>(StructValue));
		}

		int32 GetOutputIndex() const {
			return *OutputIndexProperty->ContainerPtrToValuePtr<int32>(StructValue);
		}

		void SetOutputIndex(const int32 OutputIndex) const {
			*OutputIndexProperty->ContainerPtrToValuePtr<int32>(StructValue) = OutputIndex;
		}

		void Disconnect() const {
			ExpressionProperty->SetObjectPropertyValue(ExpressionProperty->ContainerPtrToValuePtr<void>(StructValue), nullptr);
			SetOutputIndex(0);
		}
	};

	void ForEachConnectionInStruct(const UScriptStruct* Struct, void* StructValue, TFunctionRef<void(const FGraphConnection&)> Visit);

	void ForEachConnectionInProperties(const UStruct* Struct, void* Container, TFunctionRef<void(const FGraphConnection&)> Visit) {
		for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt) {
			FProperty* Property = *PropertyIt;

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property)) {
				for (int32 Index = 0; Index < StructProperty->ArrayDim; Index++) {
					ForEachConnectionInStruct(StructProperty->Struct, StructProperty->ContainerPtrToValuePtr<void>(Container, Index), Visit);
				}

				continue;
			}

			FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
			FStructProperty* InnerProperty = ArrayProperty != nullptr ? CastField<FStructProperty>(ArrayProperty->Inner) : nullptr;

			if (InnerProperty == nullptr) {
				continue;
			}

			FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Container));

			for (int32 Index = 0; Index < ArrayHelper.Num(); Index++) {
				ForEachConnectionInStruct(InnerProperty->Struct, ArrayHelper.GetRawPtr(Index), Visit);
			}
		}
	}

	void ForEachConnectionInStruct(const UScriptStruct* Struct, void* StructValue, TFunctionRef<void(const FGraphConnection&)> Visit) {
		FGraphConnection Connection;

		if (Connection.Bind(Struct)) {
			Connection.StructValue = StructValue;

			Visit(Connection);

			return;
		}

		ForEachConnectionInProperties(Struct, StructValue, Visit);
	}

	void ForEachConnection(UObject* Object, TFunctionRef<void(const FGraphConnection&)> Visit) {
		if (Object == nullptr) {
			return;
		}

		ForEachConnectionInProperties(Object->GetClass(), Object, Visit);
	}

	void ForEachExpression(UObject* Parent, TFunctionRef<void(UMaterialExpression*)> Visit) {
#if UE5_1_BEYOND
		if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent); MaterialFunction != nullptr && HasMaterialEditorOnlyData(MaterialFunction)) {
			for (UMaterialExpression* Expression : MaterialFunction->GetExpressionCollection().Expressions) Visit(Expression);
		}

		if (UMaterial* Material = Cast<UMaterial>(Parent); Material != nullptr && HasMaterialEditorOnlyData(Material)) {
			for (UMaterialExpression* Expression : Material->GetEditorOnlyData()->ExpressionCollection.Expressions) Visit(Expression);
		}
#else
		if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent)) {
			for (UMaterialExpression* Expression : MaterialFunction->FunctionExpressions) Visit(Expression);
		}

		if (UMaterial* Material = Cast<UMaterial>(Parent)) {
			for (UMaterialExpression* Expression : Material->Expressions) Visit(Expression);
		}
#endif
	}

	void PruneSetMaterialAttributes(UMaterialExpressionSetMaterialAttributes* Set) {
		for (int32 Index = Set->AttributeSetTypes.Num() - 1; Index >= 0; Index--) {
			if (!IsMissingMaterialAttribute(Set->AttributeSetTypes[Index])) {
				continue;
			}

			FString PinName = Set->AttributeSetTypes[Index].ToString(EGuidFormats::Digits);

			if (Set->Inputs.IsValidIndex(Index + 1)) {
				if (!Set->Inputs[Index + 1].InputName.IsNone()) {
					PinName = Set->Inputs[Index + 1].InputName.ToString();
				}

				Set->Inputs.RemoveAt(Index + 1);
			}

			Set->AttributeSetTypes.RemoveAt(Index);

			GLog->Log(*FString::Printf(
				TEXT("Reflection: \"%s\" sets \"%s\", which this engine has no material attribute for, so the pin was removed"),
				*Set->GetName(), *PinName
			));
		}
	}

	bool PruneGetMaterialAttributes(UMaterialExpressionGetMaterialAttributes* Get, TArray<int32>& OutRemap) {
		TArray<int32> Missing;

		for (int32 Index = 0; Index < Get->AttributeGetTypes.Num(); Index++) {
			if (IsMissingMaterialAttribute(Get->AttributeGetTypes[Index])) {
				Missing.Add(Index);
			}
		}

		if (Missing.Num() == 0) {
			return false;
		}

		OutRemap.Init(INDEX_NONE, Get->AttributeGetTypes.Num() + 1);

		int32 NewIndex = 0;

		for (int32 OutputIndex = 0; OutputIndex < OutRemap.Num(); OutputIndex++) {
			if (OutputIndex > 0 && Missing.Contains(OutputIndex - 1)) {
				continue;
			}

			OutRemap[OutputIndex] = NewIndex++;
		}

		for (int32 Index = Missing.Num() - 1; Index >= 0; Index--) {
			const int32 Attribute = Missing[Index];

			FString PinName = Get->AttributeGetTypes[Attribute].ToString(EGuidFormats::Digits);

			if (Get->Outputs.IsValidIndex(Attribute + 1)) {
				const FString OutputName = OutputNameToString(Get->Outputs[Attribute + 1].OutputName);

				if (!OutputName.IsEmpty()) {
					PinName = OutputName;
				}

				Get->Outputs.RemoveAt(Attribute + 1);
			}

			Get->AttributeGetTypes.RemoveAt(Attribute);

			GLog->Log(*FString::Printf(
				TEXT("Reflection: \"%s\" gets \"%s\", which this engine has no material attribute for, so the pin was removed"),
				*Get->GetName(), *PinName
			));
		}

		return true;
	}
}

void IMaterialGraph::PruneMissingMaterialAttributes(UObject* Parent, UObject* MaterialData) {
	TMap<UObject*, TArray<int32>> OutputRemaps;

	TSet<UObject*> Objects;

	Objects.Add(Parent);
	Objects.Add(MaterialData);
	Objects.Remove(nullptr);

	ForEachExpression(Parent, [&OutputRemaps, &Objects](UMaterialExpression* Expression) {
		if (Expression == nullptr) {
			return;
		}

		Objects.Add(Expression);

		if (UMaterialExpressionSetMaterialAttributes* Set = Cast<UMaterialExpressionSetMaterialAttributes>(Expression)) {
			PruneSetMaterialAttributes(Set);

			return;
		}

		if (UMaterialExpressionGetMaterialAttributes* Get = Cast<UMaterialExpressionGetMaterialAttributes>(Expression)) {
			TArray<int32> Remap;

			if (PruneGetMaterialAttributes(Get, Remap)) {
				OutputRemaps.Add(Get, MoveTemp(Remap));
			}
		}
	});

	if (OutputRemaps.Num() == 0) {
		return;
	}

	for (UObject* Object : Objects) {
		ForEachConnection(Object, [&OutputRemaps](const FGraphConnection& Connection) {
			const TArray<int32>* Remap = OutputRemaps.Find(Connection.GetExpression());

			if (Remap == nullptr || !Remap->IsValidIndex(Connection.GetOutputIndex())) {
				return;
			}

			const int32 OutputIndex = (*Remap)[Connection.GetOutputIndex()];

			if (OutputIndex == INDEX_NONE) {
				Connection.Disconnect();

				return;
			}

			Connection.SetOutputIndex(OutputIndex);
		});
	}
}

void IMaterialGraph::SetExpressionParent(UObject* Parent, UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Json) {
	if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent)) {
		Expression->Function = MaterialFunction;
	} else if (UMaterial* Material = Cast<UMaterial>(Parent)) {
		Expression->Material = Material;
	}

	if (Cast<UMaterialExpressionTextureBase>(Expression)) {
		const TSharedPtr<FJsonObject>* TexturePtr;

		if (Json->TryGetObjectField(TEXT("Texture"), TexturePtr)) {
			Expression->UpdateParameterGuid(true, false);
		}
	}
}

void IMaterialGraph::AddExpressionToParent(UObject* Parent, UMaterialExpression* Expression) {
	if (UMaterialExpressionComment* Comment = Cast<UMaterialExpressionComment>(Expression)) {
#if UE5_1_BEYOND
		if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent)) MaterialFunction->GetExpressionCollection().AddComment(Comment);
		if (UMaterial* Material = Cast<UMaterial>(Parent)) Material->GetExpressionCollection().AddComment(Comment);
#else
		if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent)) MaterialFunction->FunctionEditorComments.Add(Comment);
		if (UMaterial* Material = Cast<UMaterial>(Parent)) Material->EditorComments.Add(Comment);
#endif
	} else {
#if UE5_1_BEYOND
		if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent)) {
			MaterialFunction->GetExpressionCollection().AddExpression(Expression);
		}

		if (UMaterial* Material = Cast<UMaterial>(Parent)) {
			Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(Expression);
			Expression->UpdateMaterialExpressionGuid(true, false);
			Material->AddExpressionParameter(Expression, Material->EditorParameters);
		}
#else
		if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent)) {
			MaterialFunction->FunctionExpressions.Add(Expression);
		}

		if (UMaterial* Material = Cast<UMaterial>(Parent)) {
			Material->Expressions.Add(Expression);
			Expression->UpdateMaterialExpressionGuid(true, false);
			Material->AddExpressionParameter(Expression, Material->EditorParameters);
		}
#endif
	}
}

UMaterialExpression* IMaterialGraph::CreateEmptyExpression(FUObjectExport* Export, FMaterialSubstituteContext& Context) {
	const FName Type = Export->GetType();
	const FName Name = Export->GetName();

	UClass* Class = FindClassByType(Type.ToString());

	UObject* Parent = Export->Parent;

	if (!Class) {
#if UE5_1_BEYOND
		TArray<FString> Redirects = TArray {
			FLinkerLoad::FindNewPathNameForClass("/Script/InterchangeImport." + Type.ToString(), false),
			FLinkerLoad::FindNewPathNameForClass("/Script/Landscape." + Type.ToString(), false)
		};

		for (FString RedirectedPath : Redirects) {
			if (!RedirectedPath.IsEmpty() && !Class)
				Class = FindObject<UClass>(nullptr, *RedirectedPath);
		}
#endif

		if (!Class) {
			Class = FindClassByType(Type.ToString().Replace(TEXT("MaterialExpressionPhysicalMaterialOutput"), TEXT("MaterialExpressionLandscapePhysicalMaterialOutput")));
		}
	}

	if (!Class) {
		Context.SetExport(Export);

		return CreateSubstituteExpression(Context);
	}

#if ENGINE_UE4
	if (Type == "MaterialExpressionPinBase") {
		return NewObject<UMaterialExpression>(
			Parent,
			UMaterialExpressionReroute::StaticClass(),
			Name
		);
	}
#endif

	if (!Class->IsChildOf<UMaterialExpression>()) return nullptr;

	return NewObject<UMaterialExpression>
	(
		Parent,
		Class,
		Name,
		RF_Transactional
	);
}

UMaterialExpression* IMaterialGraph::CreateSubstituteExpression(FMaterialSubstituteContext& Context) {
	const FName Type = Context.GetType();

	for (const TSharedRef<FMaterialNodeSubstitute>& Substitute : FMaterialNodeSubstituteRegistry::Get().GetSubstitutes()) {
		if (!Substitute->Handles(Type)) {
			continue;
		}

		if (UMaterialExpression* Expression = Substitute->Create(Context)) {
			return Expression;
		}
	}

	GLog->Log(*FString::Printf(TEXT("Reflection: nothing stood in for \"%s\" and nothing reported it"), *Type.ToString()));

	return nullptr;
}

void IMaterialGraph::RemapConvertOutput(FExpressionInput& Input) const {
	const TArray<UObject*>* Roots = GetPropertySerializer()->ConvertOutputRoots.Find(Input.Expression);

	if (Roots == nullptr || !Roots->IsValidIndex(Input.OutputIndex)) {
		return;
	}

	Input.Expression = Cast<UMaterialExpression>((*Roots)[Input.OutputIndex]);
	Input.OutputIndex = 0;
}

bool IMaterialGraph::IsConvertSubstitute(UMaterialExpression* Expression) const {
	return Expression != nullptr && GetPropertySerializer()->ConvertOutputRoots.Contains(Expression);
}

void IMaterialGraph::ReportMaterialDataMissing() const {
	FImportIssues::Report(
		EImportIssue::Data,
		TEXT("The export carries no material data"),
		TEXT("Nothing was there to build a graph from - see the requirements for Materials on GitHub.")
	);
}

void IMaterialGraph::ReportNullExpressions() const {
	FImportIssues::Report(
		EImportIssue::Data,
		TEXT("The export's expression list has holes in it"),
		TEXT("Some of the nodes this material is built from were not in the export, so there is no graph here to rebuild.")
	);
}

void IMaterialGraph::ReportCreatedStubs() const {
	FImportIssues::Report(
		EImportIssue::Data,
		TEXT("Built from stubs"),
		TEXT("The export carries no material data, so the parameters were stubbed out and the graph is empty.")
	);
}

FExpressionInput IMaterialGraph::PopulateExpressionInput(const FJsonObject* JsonProperties, UMaterialExpression* Expression, const FString& Type) {
	FExpressionInput Input;
	Input.Expression = Expression;

	int OutputIndex;
	if (JsonProperties->TryGetNumberField(TEXT("OutputIndex"), OutputIndex)) Input.OutputIndex = OutputIndex;
	FString InputName;
	if (JsonProperties->TryGetStringField(TEXT("InputName"), InputName)) Input.InputName = StringToName(InputName);
	int Mask;
	if (JsonProperties->TryGetNumberField(TEXT("Mask"), Mask)) Input.Mask = Mask;
	int MaskR;
	if (JsonProperties->TryGetNumberField(TEXT("MaskR"), MaskR)) Input.MaskR = MaskR;
	int MaskG;
	if (JsonProperties->TryGetNumberField(TEXT("MaskG"), MaskG)) Input.MaskG = MaskG;
	int MaskB;
	if (JsonProperties->TryGetNumberField(TEXT("MaskB"), MaskB)) Input.MaskB = MaskB;
	int MaskA;
	if (JsonProperties->TryGetNumberField(TEXT("MaskA"), MaskA)) Input.MaskA = MaskA;

	if (Type == "Color") {
		if (FColorMaterialInput* ColorInput = reinterpret_cast<FColorMaterialInput*>(&Input)) {
			bool UseConstant;
			if (JsonProperties->TryGetBoolField(TEXT("UseConstant"), UseConstant)) ColorInput->UseConstant = UseConstant;
			const TSharedPtr<FJsonObject>* Constant;
			if (JsonProperties->TryGetObjectField(TEXT("Constant"), Constant)) ColorInput->Constant = ObjectToLinearColor(Constant->Get()).ToFColor(true);
			Input = FExpressionInput(*ColorInput);
		}
	} else if (Type == "Scalar") {
		if (FScalarMaterialInput* ScalarInput = reinterpret_cast<FScalarMaterialInput*>(&Input)) {
			bool UseConstant;
			if (JsonProperties->TryGetBoolField(TEXT("UseConstant"), UseConstant)) ScalarInput->UseConstant = UseConstant;
#if ENGINE_UE5
			float Constant;
#else
			double Constant;
#endif
			if (JsonProperties->TryGetNumberField(TEXT("Constant"), Constant)) ScalarInput->Constant = Constant;
			Input = FExpressionInput(*ScalarInput);
		}
	} else if (Type == "Vector") {
		if (FVectorMaterialInput* VectorInput = reinterpret_cast<FVectorMaterialInput*>(&Input)) {
			bool UseConstant;
			if (JsonProperties->TryGetBoolField(TEXT("UseConstant"), UseConstant)) VectorInput->UseConstant = UseConstant;
			const TSharedPtr<FJsonObject>* Constant;
			if (JsonProperties->TryGetObjectField(TEXT("Constant"), Constant)) VectorInput->Constant = ObjectToVector3F(Constant->Get());
			Input = FExpressionInput(*VectorInput);
		}
	}

	return Input;
}

FName IMaterialGraph::GetExpressionName(const FJsonObject* JsonProperties, const FString& OverrideParameterName) {
	const TSharedPtr<FJsonValue> ExpressionField = JsonProperties->TryGetField(OverrideParameterName);

	if (ExpressionField == nullptr || ExpressionField->IsNull()) {
		return StringToName(JsonProperties->GetStringField(TEXT("ExpressionName")));
	}

	const TSharedPtr<FJsonObject> ExpressionObject = ExpressionField->AsObject();
	FString ObjectName;

	if (ExpressionObject->TryGetStringField(TEXT("ObjectName"), ObjectName)) {
		return GetExportNameOfSubobject(ObjectName);
	}

	return NAME_None;
}
