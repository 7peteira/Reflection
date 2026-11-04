#pragma once

#include "CoreMinimal.h"
#include "Containers/JsonValueExport.h"
#include "Importers/Types/Blueprint/MacroPattern.h"

class UEdGraph;
class UK2Node_FunctionResult;
class UEdGraphPin;
class UK2Node;
class UK2Node_Variable;
class UK2Node_CallFunction;
struct FUObjectExportContainer;
class UObjectSerializer;

class REFLECTION_API FBytecodeGraph {
public:
	FBytecodeGraph(UEdGraph* InGraph, const TArray<FUObjectJsonValueExport>& InStatements, const TArray<FUObjectJsonValueExport>& InDeclared, FUObjectExportContainer* InContainer)
		: Graph(InGraph)
		, Statements(InStatements)
		, Declared(InDeclared)
		, Container(InContainer)
	{
	}

	void EnterAt(int32 Address, UK2Node* Node, FName Through = NAME_None);

	void SitsAt(int32 Address, UK2Node* Node);

	void SetObjectSerializer(UObjectSerializer* InObjectSerializer) { ObjectSerializer = InObjectSerializer; }

	void HandOver(const FString& Frame, const FString& Parameter, UK2Node* Node);

	void HandOverTrack(const FString& Name, UK2Node* Node, FName Pin);

	void Only(int32 From, int32 To);

	void LeaveOut(int32 From, int32 To);

	void LeaveOutAt(int32 Address);

	void HandsInto(const FString& Owner, FName Member, UEdGraphPin* Pin);

	void Clear();

	int32 DeclareLocals();

	int32 Build();

	const TArray<FString>& GetUnhandled() const { return Unhandled; }

	static UFunction* ResolveFunction(const FUObjectJsonValueExport& Reference);

	static UFunction* FindFunctionOn(const UClass* Class, const FString& Member);

private:
	struct FValue {
		UEdGraphPin* Pin = nullptr;
		FString Literal;

		FString Wanted;

		bool IsSet() const { return Pin != nullptr || !Literal.IsEmpty(); }
	};

	void Give(const FValue& What, UEdGraphPin* To);

	static FString Canonical(const FUObjectJsonValueExport& Expression);

	FString Stamp(const FUObjectJsonValueExport& Expression) const;
	void Gather(const FUObjectJsonValueExport& Expression, TArray<FString>& Saw) const;

	TMap<FString, int32> Version;

	const UClass* ClassNamedBy(const FUObjectJsonValueExport& Expression);

	const UClass* DeclaredClassOf(const FUObjectJsonValueExport& Expression);

	void BringInClass(const FUObjectJsonValueExport& Named);

	UK2Node* FormatText(const FUObjectJsonValueExport& Expression);

	UK2Node* GetDataTableRow(const FUObjectJsonValueExport& Expression);

	bool FillStruct(const FUObjectJsonValueExport& Statement, const FUObjectJsonValueExport& Variable, const FValue& Expression);

	FString ReadStructConst(const FUObjectJsonValueExport& Expression);

	FString SpellValue(const FProperty* Property, const FUObjectJsonValueExport& Expression);

	void Remember(const FString& Same, const FValue& Value);

	void FindMacros();

	void FindConstants();

	void FindAnswers();

	void FindPureCasts();

	bool AnswerHere(const FString& Out, const FValue& Expression);

	class UK2Node_FunctionResult* NewReturn();

	void MakeMacros();

	bool PlaceMacro(const FMacroMatch& Match);

	static UEdGraph* FindStandardMacro(const TCHAR* Named);

	bool Place(const FUObjectJsonValueExport& Statement);

	FValue Read(const FUObjectJsonValueExport& Expression);

	FValue ReadExpression(const FUObjectJsonValueExport& Expression);

	FValue ReadMathExpression(const FUObjectJsonValueExport& Expression);

	bool SpellMath(const FUObjectJsonValueExport& Expression, FString& Out, TArray<TPair<FString, FUObjectJsonValueExport>>& Inputs);

	UK2Node* PlaceCall(const FUObjectJsonValueExport& Expression, UEdGraphPin* Target, const UClass* Against = nullptr);

	FValue ReadVariable(const FUObjectJsonValueExport& Expression);

	UClass* EnsureComponentTemplate(const FString& Name);

	bool HasLocal(const FString& Name) const;

	UEdGraphPin* PointAtDelegate(class UK2Node_BaseMCDelegate* Node, const FUObjectJsonValueExport& Named);

	UEdGraphPin* FindAnswer(const FString& Name) const;

	bool HoldsLocals() const;

	bool IsMade(const FString& Name) const;

	bool KeepsTemporary(const FString& Name) const;

	UEdGraphPin* Temporary(const FString& Name, const FUObjectJsonValueExport& Property);

	bool EnsureLocal(const FString& Name, const FUObjectJsonValueExport& Property);

	void PointAtLocal(UK2Node_Variable* Node, const FString& Name);

	void Connect(UEdGraphPin* From, UEdGraphPin* To);

	static void ApplyLiteral(UEdGraphPin* Pin, const FString& Literal);

	void LinkExecution(UEdGraphPin* From, UEdGraphPin* To);

	void Arrange();

	static UEdGraphPin* WayIn(UEdGraphNode* Node);

	static UEdGraphPin* MacroPin(UK2Node* Node, FName Named, EEdGraphPinDirection Direction);

	void EnterNode(UK2Node* Node);

	void ChainExecution(UK2Node* Node);

	static TSubclassOf<UK2Node_CallFunction> NodeClassFor(const UFunction* Function);

	template <typename T>
	T* AddNode();

	template <typename T>
	T* AddNodeOfClass(const TSubclassOf<T>& Class);

	UEdGraph* Graph = nullptr;
	TArray<FUObjectJsonValueExport> Statements;

	TArray<FUObjectJsonValueExport> Declared;

	FUObjectExportContainer* Container = nullptr;

	UObjectSerializer* ObjectSerializer = nullptr;

	TMap<FString, UEdGraphPin*> Locals;

	TMap<FString, UEdGraphNode*> Picked;

	TSet<FString> Handouts;

	TMap<int32, UEdGraphPin*> Arrivals;

	UEdGraphPin* Flow = nullptr;

	UEdGraphPin* Start = nullptr;

	TMap<int32, TArray<TPair<TWeakObjectPtr<UK2Node>, FName>>> Starts;

	TMap<int32, TArray<TWeakObjectPtr<UK2Node>>> Sitting;

	TSet<int32> Ignored;

	TArray<TPair<int32, int32>> Stretch;

	TArray<TPair<int32, int32>> Elsewhere;

	TSet<int32> ElsewhereAt;

	TMap<FString, UEdGraphPin*> Decided;

	int32 AnsweredAt = INDEX_NONE;
	TWeakObjectPtr<UK2Node_FunctionResult> AnsweredWith;

	TArray<TPair<UEdGraphPin*, FString>> Later;

	TMap<FString, TPair<TWeakObjectPtr<UK2Node>, FString>> Handed;

	TMap<FString, TPair<TWeakObjectPtr<UK2Node>, FName>> Tracks;

	TMap<int32, UEdGraphPin*> Entries;

	TArray<TPair<UEdGraphPin*, int32>> Jumps;

	TArray<FString> Unhandled;

	TArray<FString> Contested;

	const bool bTidy = !IsRunningCommandlet() || FParse::Param(FCommandLine::Get(), TEXT("tidy"));

	struct FShared {
		UEdGraphPin* Pin = nullptr;
		int32 Made = 0;
	};

	struct FTakenApart {
		UK2Node* Node = nullptr;
		int32 Made = 0;
	};

	static constexpr int32 SharingReach = 4;

	TMap<FString, FShared> Reused;

	TMap<FString, FTakenApart> Apart;

	int32 Placing = 0;

	TMap<FString, TPair<class UK2Node_SetFieldsInStruct*, int32>> SettingMembers;

	TMap<int32, FMacroMatch> Macros;

	TMap<int32, class UK2Node_MacroInstance*> Written;

	TSet<int32> Skipped;

	TSet<FString> Owned;

	bool bLookedForMacros = false;

	TSet<FString> Constants;

	TSet<FString> Running;

	TSet<FString> TestedScratch;

	TSet<FString> HeldScratch;

	TMap<FString, TWeakObjectPtr<class UK2Node_TemporaryVariable>> Temporaries;

	TMap<FString, FString> Carried;

	bool bLookedForConstants = false;

	TMap<FString, int32> Writes;

	TMap<FString, FString> Answered;

	bool bLookedForAnswers = false;

	TSet<FString> PureCasts;

	TSet<FString> ImpureCasts;

	bool bLookedForPureCasts = false;

	FString Writing;

	TArray<TPair<const FMacroMatch*, UK2Node*>> Open;

	int32 Placed = 0;

	int32 Chained = 0;
	int32 Orphaned = 0;

	int32 LastChainedAddress = -1;
};
