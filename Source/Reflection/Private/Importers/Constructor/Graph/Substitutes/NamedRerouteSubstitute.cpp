#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/Graph/MaterialNodeSubstitute.h"

#include "Materials/MaterialExpressionReroute.h"

#include "Utilities/JsonHelpers.h"

static const TCHAR* NamedRerouteDeclarationType = TEXT("MaterialExpressionNamedRerouteDeclaration");
static const TCHAR* NamedRerouteUsageType = TEXT("MaterialExpressionNamedRerouteUsage");

static FString GetNamedRerouteName(const TSharedPtr<FJsonObject>& Properties) {
	FString Name;

	if (Properties.IsValid()) {
		Properties->TryGetStringField(TEXT("Name"), Name);
	}

	return Name;
}

static UMaterialExpression* FindNamedRerouteDeclarationByGuid(FUObjectExportContainer* Container, const TSharedPtr<FJsonObject>& UsageProperties) {
	FString DeclarationGuid;

	if (!UsageProperties->TryGetStringField(TEXT("DeclarationGuid"), DeclarationGuid) || DeclarationGuid.IsEmpty()) {
		return nullptr;
	}

	for (FUObjectExport* Export : Container->Exports) {
		if (Export->GetType() != FName(NamedRerouteDeclarationType)) {
			continue;
		}

		const TSharedPtr<FJsonObject> Properties = Export->GetProperties();
		FString VariableGuid;

		if (Properties.IsValid() && Properties->TryGetStringField(TEXT("VariableGuid"), VariableGuid) && VariableGuid == DeclarationGuid) {
			return Export->Get<UMaterialExpression>();
		}
	}

	return nullptr;
}

class FNamedRerouteSubstitute final : public FMaterialNodeSubstitute {
public:
	virtual bool Handles(const FName Type) const override {
		return Type == FName(NamedRerouteDeclarationType) || Type == FName(NamedRerouteUsageType);
	}

	virtual UMaterialExpression* Create(FMaterialSubstituteContext& Context) override {
		const FName Type = Context.GetType();
		const TSharedPtr<FJsonObject> Properties = Context.GetProperties();

		UMaterialExpressionReroute* Reroute = NewObject<UMaterialExpressionReroute>(
			Context.GetParent(),
			UMaterialExpressionReroute::StaticClass(),
			Context.GetName(),
			RF_Transactional
		);

		if (Type == FName(NamedRerouteDeclarationType)) {
			const FString Name = GetNamedRerouteName(Properties);

			if (!Name.IsEmpty()) {
				Reroute->Desc = Name;
			}
		} else {
			Context.Defer([Reroute, Properties](FMaterialSubstituteContext& DeferredContext) {
				ResolveUsage(DeferredContext, Reroute, Properties);
			});
		}

		GLog->Log(*FString::Printf(TEXT("Reflection: Rebuilt %s \"%s\" as a Reroute"), *Type.ToString(), *Context.GetBaseName()));

		return Reroute;
	}

private:
	static void ResolveUsage(FMaterialSubstituteContext& Context, UMaterialExpressionReroute* Usage, const TSharedPtr<FJsonObject>& Properties) {
		if (Usage == nullptr || !Properties.IsValid()) {
			return;
		}

		FUObjectExportContainer* Container = Context.GetContainer();
		UMaterialExpression* Declaration = nullptr;

		const TSharedPtr<FJsonObject>* DeclarationObject;

		if (Properties->TryGetObjectField(TEXT("Declaration"), DeclarationObject)) {
			FString ObjectName;

			if ((*DeclarationObject)->TryGetStringField(TEXT("ObjectName"), ObjectName)) {
				Declaration = Container->Find<UMaterialExpression>(GetExportNameOfSubobject(ObjectName));
			}
		}

		if (Declaration == nullptr) {
			Declaration = FindNamedRerouteDeclarationByGuid(Container, Properties);
		}

		if (Declaration == nullptr) {
			GLog->Log(*FString::Printf(TEXT("Reflection: Named reroute usage \"%s\" names a declaration this graph does not have"), *Usage->GetName()));

			return;
		}

		Usage->Input.Expression = Declaration;
		Usage->Input.OutputIndex = 0;

		Usage->Desc = Declaration->Desc;
	}
};

REGISTER_MATERIAL_NODE_SUBSTITUTE(FNamedRerouteSubstitute);
