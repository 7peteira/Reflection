#pragma once

#include "CoreMinimal.h"
#include "Containers/ExportContainer.h"

class IMaterialGraph;
class UMaterialExpression;
class UMaterialExpressionConstant;
class FMaterialSubstituteContext;

class REFLECTION_API FMaterialNodeSubstitute {
public:
	virtual ~FMaterialNodeSubstitute() = default;

	virtual bool Handles(const FName Type) const = 0;

	virtual UMaterialExpression* Create(FMaterialSubstituteContext& Context) = 0;

	virtual int32 GetPriority() const { return 0; }
};

class REFLECTION_API FMaterialSubstituteContext {
public:
	FMaterialSubstituteContext(IMaterialGraph& InGraph, FUObjectExportContainer* InContainer);

	void SetExport(FUObjectExport* InExport);

	FUObjectExport* GetExport() const { return Export; }
	FName GetType() const;
	FName GetName() const;
	FString GetBaseName() const;
	UObject* GetParent() const;
	const TSharedPtr<FJsonObject>& GetProperties() const { return Properties; }

	int32 GetEditorX() const { return EditorX; }
	int32 GetEditorY() const { return EditorY; }

	FUObjectExportContainer* GetContainer() const { return Container; }
	IMaterialGraph& GetGraph() const { return Graph; }

	UMaterialExpression* CreateExpression(UClass* Class, const FString& NameSuffix, const int32 X, const int32 Y);

	template <typename ExpressionType>
	ExpressionType* CreateExpression(const FString& NameSuffix, const int32 X, const int32 Y) {
		return Cast<ExpressionType>(CreateExpression(ExpressionType::StaticClass(), NameSuffix, X, Y));
	}

	UMaterialExpressionConstant* CreateConstant(const float Value, const TCHAR* InputName, const int32 X, const int32 Y);

	void AddExpression(UMaterialExpression* Expression);

	TSharedPtr<FJsonObject> FindConnectedInput(const TCHAR* InputName) const;

	float ReadInputConstant(const TCHAR* PropertyName, const float DefaultValue) const;

	void ConnectLater(UMaterialExpression* Expression, const int32 InputIndex, const TSharedPtr<FJsonObject>& ExpressionInput);

	void Defer(TFunction<void(FMaterialSubstituteContext&)> Work);

	void ResolveDeferred();

	void RegisterOutputRoots(UMaterialExpression* Root, const TArray<UObject*>& Roots);

private:
	IMaterialGraph& Graph;
	FUObjectExportContainer* Container = nullptr;

	FUObjectExport* Export = nullptr;
	TSharedPtr<FJsonObject> Properties;

	int32 EditorX = 0;
	int32 EditorY = 0;

	TArray<TFunction<void(FMaterialSubstituteContext&)>> Deferred;
};

class REFLECTION_API FMaterialNodeSubstituteRegistry {
public:
	static FMaterialNodeSubstituteRegistry& Get();

	void Register(const TSharedRef<FMaterialNodeSubstitute>& Substitute);

	const TArray<TSharedRef<FMaterialNodeSubstitute>>& GetSubstitutes() const { return Substitutes; }

private:
	TArray<TSharedRef<FMaterialNodeSubstitute>> Substitutes;
};

template <typename SubstituteType>
struct TMaterialNodeSubstituteRegistrar {
	TMaterialNodeSubstituteRegistrar() {
		FMaterialNodeSubstituteRegistry::Get().Register(MakeShared<SubstituteType>());
	}
};

#define REGISTER_MATERIAL_NODE_SUBSTITUTE(SubstituteType) \
	static const TMaterialNodeSubstituteRegistrar<SubstituteType> SubstituteType##Registrar

struct FSubstituteInputMapping {
	const TCHAR* NodeInput;
	const TCHAR* TargetInput;
	const TCHAR* NodeConstant;
	float DefaultConstant;
};

static constexpr int32 BinaryInputA = 0;
static constexpr int32 BinaryInputB = 1;
static constexpr int32 SingleInput = 0;

static constexpr int32 SubstituteConstantOffsetX = 220;
static constexpr int32 SubstituteConstantRowHeight = 80;
