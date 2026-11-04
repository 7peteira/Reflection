#pragma once

#include "Engine/Compatibility.h"

#if REFLECTION_CONTROL_RIG

#include "CoreMinimal.h"
#include "Containers/JsonValueExport.h"

class UControlRigBlueprint;
class UPropertySerializer;
class URigVMController;
class URigVMGraph;
class URigVMNode;
class URigVMPin;

struct FRigVMFunction;

struct FRigVMGraphStats {
	int32 Instructions = 0;
	int32 Nodes = 0;

	int32 Links = 0;
	int32 Defaults = 0;
	int32 VariableNodes = 0;

	TArray<FString> MissingFunctions;

	TArray<FString> MismatchedFunctions;
	int32 DroppedPins = 0;

	int32 FailedNodes = 0;
	int32 FailedLinks = 0;
};

class REFLECTION_API FRigVMGraphReconstruction {
public:
	FRigVMGraphReconstruction(UControlRigBlueprint* InBlueprint, UPropertySerializer* InPropertySerializer);

	bool Build(const FUObjectJsonValueExport& VirtualMachine);

	const FRigVMGraphStats& GetStats() const {
		return Stats;
	}

private:
	struct FRegister {
		FString Name;
		TSharedPtr<FJsonValue> Value;

		int32 Owner = INDEX_NONE;
		FString OwnerPin;
	};

	struct FNode {
		int32 Instruction = INDEX_NONE;

		const FRigVMFunction* Function = nullptr;

		FString Name;
		FString DerivedName;

		TArray<TPair<int32, int32>> Operands;

		TArray<FString> Arguments;
		TArray<bool> ArgumentIsOutput;

		URigVMNode* Node = nullptr;
	};

	struct FBlock {
		int32 First = INDEX_NONE;
		int32 Last = INDEX_NONE;

		int32 Owner = INDEX_NONE;
		FString Label;

		bool bLazy = false;
	};

	void ReadMemory(const FUObjectJsonValueExport& VirtualMachine);
	void CollectNodes(const TArray<FUObjectJsonValueExport>& Instructions, const TArray<FString>& FunctionNames);

	void ClaimRegisters();

	FString GetSegmentPath(int32 MemoryType, int32 RegisterOffset) const;
	void CreateNodes();
	void ApplyDefaults();

	void ConsolidateDefaults();

	void UpdateLayout();

	void PromoteLocalVariables();
	void CreateLinks(const TArray<FUObjectJsonValueExport>& Instructions);
	void CreateExecution(const TArray<FUObjectJsonValueExport>& Instructions, const FUObjectJsonValueExport& ByteCode);

	FRegister* FindRegister(int32 MemoryType, int32 RegisterIndex);

	static bool SplitRegisterName(const FString& RegisterName, const FString& PinName, FString& OutNodeName);

	static bool SplitRegisterTail(const FString& RegisterName, FString& OutNodeName, FString& OutPinName);

	bool SetPinDefault(URigVMPin* Pin, const TSharedPtr<FJsonValue>& Value);

	static URigVMPin* FindExecuteOutput(URigVMNode* Node, const FString& Label);
	static URigVMPin* FindExecuteInput(URigVMNode* Node);

	bool Link(int32 SourceNode, const FString& SourcePin, int32 TargetNode, const FString& TargetPin);

	URigVMNode* FindOrAddVariableGetter(const FString& VariableName, int32 ConsumerNode);

	FString GetExternalVariable(int32 RegisterIndex) const;

	UControlRigBlueprint* Blueprint;
	UPropertySerializer* PropertySerializer;

	URigVMController* Controller = nullptr;
	URigVMGraph* Graph = nullptr;

	TArray<FRegister> Work;
	TArray<FRegister> Literals;

	TArray<FString> WorkPropertyPaths;
	TArray<FString> LiteralPropertyPaths;

	TArray<FNode> Nodes;
	TMap<int32, int32> InstructionToNode;

	TMap<FString, URigVMNode*> VariableGetters;
	TSet<FString> NodeNames;

	FRigVMGraphStats Stats;
};

#endif
