#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Rig/RigVMGraph.h"

#if REFLECTION_CONTROL_RIG

#include "Engine/ControlRigCompatibility.h"

#include "RigVMCore/RigVMDispatchFactory.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMTemplate.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/Nodes/RigVMTemplateNode.h"
#include "RigVMModel/Nodes/RigVMVariableNode.h"

#include "Engine/Log.h"
#include "Serializers/PropertySerializer.h"

namespace {
	FORCEINLINE decltype(auto) GetRigVMRegistry() {
		return FRigVMRegistry::Get();
	}

	struct FDispatchArgument {
		FName Name;
		ERigVMPinDirection Direction;
	};

	TArray<FDispatchArgument> GetFactoryArgumentInfos(const FRigVMDispatchFactory* Factory) {
		TArray<FDispatchArgument> Built;

#if UE5_7_BEYOND
		FRigVMRegistryReadLock ReadLock(FRigVMRegistry::Get());

		for (const FRigVMTemplateArgumentInfo& Info : Factory->GetArgumentInfos(ReadLock)) {
			Built.Add({ Info.Name, Info.Direction });
		}
#elif UE5_4_BEYOND
		for (const FRigVMTemplateArgumentInfo& Info : Factory->GetArgumentInfos()) {
			Built.Add({ Info.Name, Info.Direction });
		}
#else
		for (const FRigVMTemplateArgument& Argument : Factory->GetArguments()) {
			Built.Add({ Argument.GetName(), Argument.GetDirection() });
		}
#endif

		return Built;
	}

	bool RigSetPinDefault(URigVMController* Controller, const FString& PinPath, const FString& Value) {
#if UE5_4_BEYOND
		return Controller->SetPinDefaultValue(PinPath, Value, true, false, false, false, false);
#else
		return Controller->SetPinDefaultValue(PinPath, Value, true, false, false, false);
#endif
	}

	FName GetFunctionArgumentName(const FRigVMFunction* Function, const int32 OperandIndex, const int32 OperandCount) {
#if UE5_7_BEYOND
		FRigVMRegistryReadLock ReadLock(FRigVMRegistry::Get());
		return Function->GetArgumentNameForOperandIndex(OperandIndex, OperandCount, ReadLock);
#else
		return Function->GetArgumentNameForOperandIndex(OperandIndex, OperandCount);
#endif
	}

	constexpr int32 OpCodeCopy = 68;
	constexpr int32 OpCodeExecute = 101;
	constexpr int32 OpCodeRunInstructions = 102;

	constexpr int32 NoRegisterOffset = 65535;

	constexpr int32 MemoryTypeWork = 0;
	constexpr int32 MemoryTypeLiteral = 1;
	constexpr int32 MemoryTypeExternal = 2;

	int32 ReadEntryInstruction(const FString& Entry) {
		FString Value;

		if (!Entry.Split(TEXT("InstructionIndex="), nullptr, &Value)) {
			return 0;
		}

		Value.Split(TEXT(")"), &Value, nullptr);

		return FCString::Atoi(*Value);
	}

	FString ToDefaultValue(const TSharedPtr<FJsonValue>& Value, const URigVMPin* Pin) {
		if (!Value.IsValid()) {
			return FString();
		}

		switch (Value->Type) {
			case EJson::Boolean: {
				return Value->AsBool() ? TEXT("True") : TEXT("False");
			}

			case EJson::Number: {
				const double Number = Value->AsNumber();

				if (Pin != nullptr && (Pin->GetCPPType() == TEXT("int32") || Pin->GetCPPType() == TEXT("uint8"))) {
					return FString::FromInt(static_cast<int32>(Number));
				}

				return FString::SanitizeFloat(Number);
			}

			case EJson::String: {
				FString String = Value->AsString();

				if (String.Contains(TEXT("::"))) {
					FString Right;

					if (String.Split(TEXT("::"), nullptr, &Right) && !Right.IsEmpty()) {
						String = Right;
					}
				}

				if (Pin != nullptr) {
					if (const UEnum* Enum = Cast<UEnum>(Pin->GetCPPTypeObject())) {
						const int64 EnumValue = Enum->GetValueByNameString(String);

						if (EnumValue != INDEX_NONE) {
							return Enum->GetNameStringByValue(EnumValue);
						}
					}
				}

				return String;
			}

			default: {
				return FString();
			}
		}
	}
}

FRigVMGraphReconstruction::FRigVMGraphReconstruction(UControlRigBlueprint* InBlueprint, UPropertySerializer* InPropertySerializer)
	: Blueprint(InBlueprint)
	, PropertySerializer(InPropertySerializer)
{
}

bool FRigVMGraphReconstruction::Build(const FUObjectJsonValueExport& VirtualMachine) {
	if (Blueprint == nullptr || !VirtualMachine.JsonObject.IsValid()) {
		return false;
	}

	Graph = Blueprint->GetDefaultModel();
	Controller = Blueprint->GetOrCreateController(Graph);

	if (Graph == nullptr || Controller == nullptr) {
		return false;
	}

	const FUObjectJsonValueExport ByteCode = VirtualMachine.GetObject(TEXT("ByteCodeStorage"));

	if (!ByteCode.JsonObject.IsValid() || !ByteCode.Has(TEXT("Instructions"))) {
		return false;
	}

	const TArray<FUObjectJsonValueExport> Instructions = ByteCode.GetArray(TEXT("Instructions"));

	TArray<FString> FunctionNames;

	if (VirtualMachine.Has(TEXT("FunctionNamesStorage"))) {
		for (const TSharedPtr<FJsonValue>& FunctionName : VirtualMachine.JsonObject->GetArrayField(TEXT("FunctionNamesStorage"))) {
			FunctionNames.Add(FunctionName->AsString());
		}
	}

	Stats.Instructions = Instructions.Num();

	ReadMemory(VirtualMachine);

	Controller->RemoveNodes(Graph->GetNodes(), false, false);

	Blueprint->SetAutoVMRecompile(false);

	CollectNodes(Instructions, FunctionNames);
	CreateNodes();
	ApplyDefaults();
	CreateLinks(Instructions);
	CreateExecution(Instructions, ByteCode);

	for (const TPair<FString, URigVMNode*>& Getter : VariableGetters) {
		if (Getter.Value == nullptr) continue;

		int32 GetterLinks = 0;

		for (const URigVMPin* Pin : Getter.Value->GetPins()) {
			GetterLinks += Pin->GetTargetLinks(true).Num() + Pin->GetSourceLinks(true).Num();
		}

		if (GetterLinks == 0) {
			Controller->RemoveNode(Getter.Value, false, false);

			Stats.VariableNodes--;
		}
	}

	ConsolidateDefaults();
	UpdateLayout();
	PromoteLocalVariables();

	Blueprint->SetAutoVMRecompile(true);

	return Stats.Nodes > 0;
}

void FRigVMGraphReconstruction::ReadMemory(const FUObjectJsonValueExport& VirtualMachine) {
	auto ReadStorage = [](const FUObjectJsonValueExport& Storage, TArray<FRegister>& OutRegisters, TArray<FString>& OutPropertyPaths) {
		if (!Storage.JsonObject.IsValid()) {
			return;
		}

		if (Storage.Has(TEXT("PropertyPathDescriptions"))) {
			for (const FUObjectJsonValueExport& PropertyPath : Storage.GetArray(TEXT("PropertyPathDescriptions"))) {
				OutPropertyPaths.Add(PropertyPath.Has(TEXT("SegmentPath")) ? PropertyPath.GetString(TEXT("SegmentPath")) : FString());
			}
		}

		if (!Storage.Has(TEXT("Properties"))) {
			return;
		}

		for (const FUObjectJsonValueExport& Property : Storage.GetArray(TEXT("Properties"))) {
			FRegister Register;

			if (Property.Has(TEXT("Name"))) {
				Register.Name = Property.GetString(TEXT("Name"));
			}

			if (Property.Has(TEXT("Tag"))) {
				Register.Value = Property.JsonObject->TryGetField(TEXT("Tag"));
			}

			OutRegisters.Add(Register);
		}
	};

	ReadStorage(VirtualMachine.GetObject(TEXT("DefaultWorkMemoryStorage")), Work, WorkPropertyPaths);
	ReadStorage(VirtualMachine.GetObject(TEXT("LiteralMemoryStorage")), Literals, LiteralPropertyPaths);
}

static TSharedPtr<FJsonValue> ResolveJsonSegment(const TSharedPtr<FJsonValue>& Value, const FString& SegmentPath) {
	TSharedPtr<FJsonValue> Current = Value;

	TArray<FString> Parts;
	SegmentPath.ParseIntoArray(Parts, TEXT("."), true);

	for (const FString& Part : Parts) {
		if (!Current.IsValid()) {
			return nullptr;
		}

		if (Current->Type == EJson::Object) {
			Current = Current->AsObject()->TryGetField(Part);
		} else if (Current->Type == EJson::Array && Part.IsNumeric()) {
			const TArray<TSharedPtr<FJsonValue>>& Elements = Current->AsArray();
			const int32 Index = FCString::Atoi(*Part);

			Current = Elements.IsValidIndex(Index) ? Elements[Index] : nullptr;
		} else {
			return nullptr;
		}
	}

	return Current;
}

FString FRigVMGraphReconstruction::GetSegmentPath(const int32 MemoryType, const int32 RegisterOffset) const {
	if (RegisterOffset == NoRegisterOffset) {
		return FString();
	}

	const TArray<FString>& PropertyPaths = MemoryType == MemoryTypeLiteral ? LiteralPropertyPaths : WorkPropertyPaths;

	return PropertyPaths.IsValidIndex(RegisterOffset) ? PropertyPaths[RegisterOffset] : FString();
}

FRigVMGraphReconstruction::FRegister* FRigVMGraphReconstruction::FindRegister(const int32 MemoryType, const int32 RegisterIndex) {
	if (MemoryType == MemoryTypeWork) {
		return Work.IsValidIndex(RegisterIndex) ? &Work[RegisterIndex] : nullptr;
	}

	if (MemoryType == MemoryTypeLiteral) {
		return Literals.IsValidIndex(RegisterIndex) ? &Literals[RegisterIndex] : nullptr;
	}

	return nullptr;
}

static FString StripGraphPrefix(const FString& InBody) {
	FString Body = InBody;

	int32 Separator = INDEX_NONE;

	for (int32 Index = Body.Len() - 3; Index >= 0; --Index) {
		if (Body[Index] == TEXT('_') && Body[Index + 1] == TEXT('_') && Body[Index + 2] == TEXT('_')) {
			Separator = Index;

			break;
		}
	}

	if (Separator != INDEX_NONE) {
		Body = Body.RightChop(Separator + 3);

		while (Body.StartsWith(TEXT("_"))) {
			Body = Body.RightChop(1);
		}
	}

	return Body;
}

static FString SanitizePinName(const FString& PinName) {
	FString Sanitized = PinName;

	for (int32 Index = 0; Index < Sanitized.Len(); ++Index) {
		const TCHAR Character = Sanitized[Index];

		if (!FChar::IsAlpha(Character) && !(Index > 0 && FChar::IsDigit(Character)) && Character != TEXT('_')) {
			Sanitized[Index] = TEXT('_');
		}
	}

	return Sanitized;
}

static FString StripRegisterSuffix(const FString& RegisterName) {
	FString Body = RegisterName;

	if (Body.EndsWith(TEXT("__Const"))) {
		LeftInline(Body, Body.Len() - 7);
	} else if (Body.EndsWith(TEXT("__IO"))) {
		LeftInline(Body, Body.Len() - 4);
	}

	return Body;
}

bool FRigVMGraphReconstruction::SplitRegisterName(const FString& RegisterName, const FString& PinName, FString& OutNodeName) {
	if (PinName.IsEmpty()) {
		return false;
	}

	FString Body = StripRegisterSuffix(RegisterName);

	const FString Suffix = TEXT("_") + SanitizePinName(PinName);

	if (!Body.EndsWith(Suffix, ESearchCase::CaseSensitive)) {
		return false;
	}

	LeftInline(Body, Body.Len() - Suffix.Len());

	Body = StripGraphPrefix(Body);

	if (Body.IsEmpty()) {
		return false;
	}

	OutNodeName = Body;

	return true;
}

bool FRigVMGraphReconstruction::SplitRegisterTail(const FString& RegisterName, FString& OutNodeName, FString& OutPinName) {
	const FString Body = StripGraphPrefix(StripRegisterSuffix(RegisterName));

	FString NodeName;
	FString PinName;

	if (!Body.Split(TEXT("_"), &NodeName, &PinName, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) {
		return false;
	}

	if (NodeName.IsEmpty() || PinName.IsEmpty()) {
		return false;
	}

	OutNodeName = NodeName;
	OutPinName = PinName;

	return true;
}

void FRigVMGraphReconstruction::CollectNodes(const TArray<FUObjectJsonValueExport>& Instructions, const TArray<FString>& FunctionNames) {
	const auto& Registry = GetRigVMRegistry();

	TMap<int32, int32> WorkRegisterUses;

	for (const FUObjectJsonValueExport& Instruction : Instructions) {
		if (Instruction.GetInteger(TEXT("OpCode"), -1) != OpCodeExecute) continue;
		if (!Instruction.Has(TEXT("Arguments"))) continue;

		for (const FUObjectJsonValueExport& Argument : Instruction.GetArray(TEXT("Arguments"))) {
			if (Argument.GetInteger(TEXT("MemoryType"), INDEX_NONE) != MemoryTypeWork) continue;

			WorkRegisterUses.FindOrAdd(Argument.GetInteger(TEXT("RegisterIndex"), INDEX_NONE))++;
		}
	}

	for (int32 Index = 0; Index < Instructions.Num(); ++Index) {
		const FUObjectJsonValueExport& Instruction = Instructions[Index];

		if (Instruction.GetInteger(TEXT("OpCode"), -1) != OpCodeExecute) {
			continue;
		}

		const int32 FunctionIndex = Instruction.GetInteger(TEXT("FunctionIndex"), INDEX_NONE);

		if (!FunctionNames.IsValidIndex(FunctionIndex)) {
			continue;
		}

		const FString& FunctionName = FunctionNames[FunctionIndex];

		if (FunctionName.StartsWith(TEXT("FRigVMFunction_Sequence::"))) {
			continue;
		}

		const FRigVMFunction* Function = Registry.FindFunction(*FunctionName);

		if (Function == nullptr) {
			Stats.MissingFunctions.AddUnique(FunctionName);

			continue;
		}

		FNode Node;
		Node.Instruction = Index;
		Node.Function = Function;

		const TArray<FUObjectJsonValueExport> Arguments = Instruction.Has(TEXT("Arguments"))
			? Instruction.GetArray(TEXT("Arguments"))
			: TArray<FUObjectJsonValueExport>();

		for (const FUObjectJsonValueExport& Argument : Arguments) {
			Node.Operands.Add(TPair<int32, int32>(
				Argument.GetInteger(TEXT("MemoryType"), INDEX_NONE),
				Argument.GetInteger(TEXT("RegisterIndex"), INDEX_NONE)
			));
		}

		const TArray<FRigVMFunctionArgument>& FunctionArguments = Function->GetArguments();

		auto IsOutputArgument = [Function](const FString& ArgumentName) -> bool {
			if (Function->Struct != nullptr) {
				if (const FProperty* Property = Function->Struct->FindPropertyByName(*ArgumentName)) {
					return Property->HasMetaData(TEXT("Output"));
				}

				return false;
			}

			if (Function->Factory != nullptr) {
				for (const FDispatchArgument& Info : GetFactoryArgumentInfos(Function->Factory)) {
					if (Info.Name.ToString() == ArgumentName) {
						return Info.Direction == ERigVMPinDirection::Output || Info.Direction == ERigVMPinDirection::IO;
					}
				}
			}

			return false;
		};

		if (FunctionArguments.Num() == Node.Operands.Num()) {
			for (const FRigVMFunctionArgument& Argument : FunctionArguments) {
				Node.Arguments.Add(Argument.Name);
				Node.ArgumentIsOutput.Add(IsOutputArgument(Argument.Name));
			}
		} else if (Function->Factory != nullptr) {
			const int32 OperandCount = Node.Operands.Num();

			if (FunctionName.Contains(TEXT("RigVMDispatch_ArrayMake"))) {
				for (int32 OperandIndex = 0; OperandIndex < OperandCount - 1; ++OperandIndex) {
					Node.Arguments.Add(FString::Printf(TEXT("Values.%d"), OperandIndex));
					Node.ArgumentIsOutput.Add(false);
				}

				Node.Arguments.Add(TEXT("Array"));
				Node.ArgumentIsOutput.Add(true);
			} else if (FunctionName.Contains(TEXT("RigVMDispatch_Select"))) {
				Node.Arguments.Add(TEXT("Index"));
				Node.ArgumentIsOutput.Add(false);

				for (int32 OperandIndex = 1; OperandIndex < OperandCount - 1; ++OperandIndex) {
					Node.Arguments.Add(FString::Printf(TEXT("Values.%d"), OperandIndex - 1));
					Node.ArgumentIsOutput.Add(false);
				}

				Node.Arguments.Add(TEXT("Result"));
				Node.ArgumentIsOutput.Add(true);
			} else {
				for (int32 OperandIndex = 0; OperandIndex < OperandCount; ++OperandIndex) {
					const FString ArgumentName = GetFunctionArgumentName(Function, OperandIndex, OperandCount).ToString();

					Node.Arguments.Add(ArgumentName);
					Node.ArgumentIsOutput.Add(IsOutputArgument(ArgumentName));
				}
			}
		} else {
			Stats.MismatchedFunctions.AddUnique(FunctionName);

			const int32 Extra = Node.Operands.Num() - FunctionArguments.Num();

			TArray<int32> Alignment;
			int32 BestScore = -1;

			auto ScoreAlignment = [this, &Node, &FunctionArguments](const int32 SkipAt) -> TPair<int32, TArray<int32>> {
				TArray<int32> Mapping;
				Mapping.Reserve(Node.Operands.Num());

				int32 Score = 0;
				int32 ArgumentIndex = 0;

				for (int32 OperandIndex = 0; OperandIndex < Node.Operands.Num(); ++OperandIndex) {
					if (OperandIndex == SkipAt || !FunctionArguments.IsValidIndex(ArgumentIndex)) {
						Mapping.Add(INDEX_NONE);

						continue;
					}

					Mapping.Add(ArgumentIndex);

					const FRegister* Register = FindRegister(Node.Operands[OperandIndex].Key, Node.Operands[OperandIndex].Value);

					FString OwnerName;

					if (Register != nullptr && SplitRegisterName(Register->Name, FunctionArguments[ArgumentIndex].Name, OwnerName)) {
						Score++;
					}

					ArgumentIndex++;
				}

				return TPair<int32, TArray<int32>>(Score, Mapping);
			};

			if (Extra == 1) {
				for (int32 SkipAt = 0; SkipAt < Node.Operands.Num(); ++SkipAt) {
					TPair<int32, TArray<int32>> Scored = ScoreAlignment(SkipAt);

					if (Scored.Key > BestScore) {
						BestScore = Scored.Key;
						Alignment = Scored.Value;
					}
				}
			}

			for (int32 OperandIndex = 0; OperandIndex < Node.Operands.Num(); ++OperandIndex) {
				int32 ArgumentIndex = INDEX_NONE;

				if (BestScore > 0 && Alignment.IsValidIndex(OperandIndex)) {
					ArgumentIndex = Alignment[OperandIndex];
				} else {
					const FRegister* Register = FindRegister(Node.Operands[OperandIndex].Key, Node.Operands[OperandIndex].Value);

					FString RegisterNode;
					FString RegisterPin;

					if (Register != nullptr && SplitRegisterTail(Register->Name, RegisterNode, RegisterPin)) {
						ArgumentIndex = FunctionArguments.IndexOfByPredicate([&RegisterPin](const FRigVMFunctionArgument& Candidate) {
							return RegisterPin.Equals(Candidate.Name, ESearchCase::CaseSensitive);
						});
					}
				}

				if (ArgumentIndex == INDEX_NONE) {
					Stats.DroppedPins++;

					Node.Arguments.Add(FString());
					Node.ArgumentIsOutput.Add(false);

					continue;
				}

				Node.Arguments.Add(FunctionArguments[ArgumentIndex].Name);
				Node.ArgumentIsOutput.Add(IsOutputArgument(FunctionArguments[ArgumentIndex].Name));
			}
		}

		FString NodeName;

		for (int32 Pass = 0; Pass < 3 && NodeName.IsEmpty(); ++Pass) {
			for (int32 OperandIndex = 0; OperandIndex < Node.Operands.Num(); ++OperandIndex) {
				const int32 MemoryType = Node.Operands[OperandIndex].Key;
				const bool bIsLiteral = MemoryType == MemoryTypeLiteral;

				if (Pass == 1 && MemoryType == MemoryTypeWork && !Node.ArgumentIsOutput[OperandIndex]) {
					if (const int32* Uses = WorkRegisterUses.Find(Node.Operands[OperandIndex].Value)) {
						if (*Uses > 1) continue;
					}
				}

				const bool bIsOutput = Node.ArgumentIsOutput[OperandIndex];

				if (Pass == 0 && (bIsLiteral || !bIsOutput)) continue;
				if (Pass == 1 && bIsLiteral) continue;
				if (Pass == 2 && !bIsLiteral) continue;

				const FRegister* Register = FindRegister(MemoryType, Node.Operands[OperandIndex].Value);
				if (Register == nullptr) continue;

				if (SplitRegisterName(Register->Name, Node.Arguments[OperandIndex], NodeName)) {
					break;
				}
			}
		}

		if (NodeName.IsEmpty()) {
			FString StructName = FunctionName;
			StructName.Split(TEXT("::"), &StructName, nullptr);

			NodeName = FString::Printf(TEXT("%s_%d"), *StructName, Index);
		}

		FString UniqueName = NodeName;

		for (int32 Suffix = 1; NodeNames.Contains(UniqueName); ++Suffix) {
			UniqueName = FString::Printf(TEXT("%s_%d"), *NodeName, Suffix);
		}

		NodeNames.Add(UniqueName);

		Node.Name = UniqueName;
		Node.DerivedName = NodeName;

		const int32 NodeIndex = Nodes.Add(Node);
		InstructionToNode.Add(Index, NodeIndex);
	}

	ClaimRegisters();
}

void FRigVMGraphReconstruction::ClaimRegisters() {
	TMap<int32, TSet<int32>> RegisterUsers;

	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex) {
		for (const TPair<int32, int32>& Operand : Nodes[NodeIndex].Operands) {
			if (Operand.Key != MemoryTypeWork) continue;

			RegisterUsers.FindOrAdd(Operand.Value).Add(NodeIndex);
		}
	}

	for (int32 Pass = 0; Pass < 2; ++Pass) {
		for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex) {
			FNode& Node = Nodes[NodeIndex];

			for (int32 OperandIndex = 0; OperandIndex < Node.Operands.Num(); ++OperandIndex) {
				if (!Node.Arguments.IsValidIndex(OperandIndex)) continue;

				const bool bIsOutput = Node.ArgumentIsOutput[OperandIndex];

				if (Pass == 0 && !bIsOutput) continue;
				if (Pass == 1 && bIsOutput) continue;

				if (Pass == 1 && Node.Operands[OperandIndex].Key == MemoryTypeWork) {
					if (const TSet<int32>* Users = RegisterUsers.Find(Node.Operands[OperandIndex].Value)) {
						if (Users->Num() > 1) continue;
					}
				}

				FRegister* Register = FindRegister(Node.Operands[OperandIndex].Key, Node.Operands[OperandIndex].Value);

				if (Register == nullptr || Register->Owner != INDEX_NONE) continue;

				FString OwnerName;

				if (SplitRegisterName(Register->Name, Node.Arguments[OperandIndex], OwnerName) && OwnerName == Node.DerivedName) {
					Register->Owner = NodeIndex;
					Register->OwnerPin = Node.Arguments[OperandIndex];
				}
			}
		}
	}

	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex) {
		FNode& Node = Nodes[NodeIndex];

		for (int32 OperandIndex = 0; OperandIndex < Node.Operands.Num(); ++OperandIndex) {
			if (!Node.Arguments.IsValidIndex(OperandIndex)) continue;
			if (!Node.ArgumentIsOutput[OperandIndex]) continue;
			if (Node.Arguments[OperandIndex].IsEmpty()) continue;
			if (Node.Operands[OperandIndex].Key != MemoryTypeWork) continue;

			FRegister* Register = FindRegister(MemoryTypeWork, Node.Operands[OperandIndex].Value);

			if (Register == nullptr || Register->Owner != INDEX_NONE) continue;

			Register->Owner = NodeIndex;
			Register->OwnerPin = Node.Arguments[OperandIndex];
		}
	}
}

void FRigVMGraphReconstruction::CreateNodes() {
	const auto& Registry = GetRigVMRegistry();

	for (int32 Index = 0; Index < Nodes.Num(); ++Index) {
		FNode& Node = Nodes[Index];

		const FVector2D Position(400.0f * static_cast<float>(Index / 12), 260.0f * static_cast<float>(Index % 12));

		if (Node.Function->Struct != nullptr) {
			FString MethodName = Node.Function->Name;
			MethodName.Split(TEXT("::"), nullptr, &MethodName);

			Node.Node = Controller->AddUnitNode(
				Node.Function->Struct,
				MethodName.IsEmpty() ? FName(TEXT("Execute")) : FName(*MethodName),
				Position,
				Node.Name,
				false,
				false
			);
		} else if (Node.Function->Factory != nullptr) {
			Node.Node = Controller->AddTemplateNode(
				Node.Function->Factory->GetTemplateNotation(),
				Position,
				Node.Name,
				false,
				false
			);
		}

		if (Node.Node == nullptr) {
			Stats.FailedNodes++;

			continue;
		}

		Stats.Nodes++;

		if (Node.Function->Factory != nullptr) {
			const TArray<FRigVMFunctionArgument>& Arguments = Node.Function->GetArguments();
			const TArray<TRigVMTypeIndex>& TypeIndices = Node.Function->GetArgumentTypeIndices();

			for (int32 ArgumentIndex = 0; ArgumentIndex < Arguments.Num(); ++ArgumentIndex) {
				if (!TypeIndices.IsValidIndex(ArgumentIndex)) continue;

				URigVMPin* Pin = Node.Node->FindPin(Arguments[ArgumentIndex].Name);

				if (Pin == nullptr || !Pin->IsWildCard()) continue;

				Controller->ResolveWildCardPin(Pin, Registry.GetType(TypeIndices[ArgumentIndex]), false, false);
			}
		}

		TMap<FString, int32> ArraySizes;

		for (const FString& Argument : Node.Arguments) {
			FString ArrayPinName;
			FString ElementIndex;

			if (!Argument.Split(TEXT("."), &ArrayPinName, &ElementIndex, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) continue;
			if (!ElementIndex.IsNumeric()) continue;

			int32& Size = ArraySizes.FindOrAdd(ArrayPinName);
			Size = FMath::Max(Size, FCString::Atoi(*ElementIndex) + 1);
		}

		for (const TPair<FString, int32>& ArraySize : ArraySizes) {
			if (const URigVMPin* ArrayPin = Node.Node->FindPin(ArraySize.Key)) {
				Controller->SetArrayPinSize(ArrayPin->GetPinPath(), ArraySize.Value, FString(), false, false);
			}
		}
	}
}

bool FRigVMGraphReconstruction::SetPinDefault(URigVMPin* Pin, const TSharedPtr<FJsonValue>& Value) {
	if (Pin == nullptr || !Value.IsValid() || Value->IsNull()) {
		return false;
	}

	if (Value->Type == EJson::Array) {
		const TArray<TSharedPtr<FJsonValue>>& Elements = Value->AsArray();

		if (!Controller->SetArrayPinSize(Pin->GetPinPath(), Elements.Num(), FString(), false, false)) {
			return false;
		}

		bool bAny = false;

		for (int32 Index = 0; Index < Elements.Num(); ++Index) {
			if (URigVMPin* ElementPin = Pin->FindSubPin(FString::FromInt(Index))) {
				bAny |= SetPinDefault(ElementPin, Elements[Index]);
			}
		}

		return bAny;
	}

	if (Value->Type == EJson::Object) {
		const TSharedPtr<FJsonObject> Object = Value->AsObject();

		if (Pin->GetSubPins().Num() == 0) {
			UScriptStruct* Struct = Cast<UScriptStruct>(Pin->GetCPPTypeObject());

			if (Struct == nullptr) {
				return false;
			}

			TArray<uint8> Memory;
			Memory.SetNumZeroed(Struct->GetStructureSize());

			Struct->InitializeStruct(Memory.GetData());

			PropertySerializer->DeserializeStruct(Struct, Object.ToSharedRef(), Memory.GetData());

			FString Exported;
			Struct->ExportText(Exported, Memory.GetData(), nullptr, nullptr, PPF_None, nullptr);

			Struct->DestroyStruct(Memory.GetData());

			if (Exported.IsEmpty() || Exported == TEXT("()")) {
				return false;
			}

			return RigSetPinDefault(Controller, Pin->GetPinPath(), Exported);
		}

		bool bAny = false;

		for (URigVMPin* SubPin : Pin->GetSubPins()) {
			const TSharedPtr<FJsonValue> Field = Object->TryGetField(SubPin->GetName());

			if (Field.IsValid()) {
				bAny |= SetPinDefault(SubPin, Field);
			}
		}

		return bAny;
	}

	const FString DefaultValue = ToDefaultValue(Value, Pin);

	if (DefaultValue.IsEmpty()) {
		return false;
	}

	return RigSetPinDefault(Controller, Pin->GetPinPath(), DefaultValue);
}

void FRigVMGraphReconstruction::UpdateLayout() {
	const TArray<URigVMNode*> GraphNodes = Graph->GetNodes();

	TMap<URigVMNode*, TArray<URigVMNode*>> Sources;
	TMap<URigVMNode*, int32> PendingSources;

	for (URigVMNode* Node : GraphNodes) {
		PendingSources.Add(Node, 0);
	}

	for (const URigVMLink* GraphLink : Graph->GetLinks()) {
		if (GraphLink->GetSourcePin() == nullptr || GraphLink->GetTargetPin() == nullptr) continue;

		URigVMNode* Source = GraphLink->GetSourcePin()->GetNode();
		URigVMNode* Target = GraphLink->GetTargetPin()->GetNode();

		if (Source == nullptr || Target == nullptr || Source == Target) continue;

		Sources.FindOrAdd(Target).AddUnique(Source);
	}

	for (const TPair<URigVMNode*, TArray<URigVMNode*>>& Pair : Sources) {
		PendingSources.FindOrAdd(Pair.Key) = Pair.Value.Num();
	}

	TMap<URigVMNode*, int32> Columns;

	TArray<URigVMNode*> Ready;

	for (URigVMNode* Node : GraphNodes) {
		if (PendingSources.FindChecked(Node) == 0) {
			Ready.Add(Node);
			Columns.Add(Node, 0);
		}
	}

	TMap<URigVMNode*, TArray<URigVMNode*>> Targets;

	for (const TPair<URigVMNode*, TArray<URigVMNode*>>& Pair : Sources) {
		for (URigVMNode* Source : Pair.Value) {
			Targets.FindOrAdd(Source).Add(Pair.Key);
		}
	}

	for (int32 Index = 0; Index < Ready.Num(); ++Index) {
		URigVMNode* Node = Ready[Index];

		if (const TArray<URigVMNode*>* NodeTargets = Targets.Find(Node)) {
			for (URigVMNode* Target : *NodeTargets) {
				Columns.FindOrAdd(Target) = FMath::Max(Columns.FindOrAdd(Target), Columns.FindChecked(Node) + 1);

				if (--PendingSources.FindChecked(Target) == 0) {
					Ready.Add(Target);
				}
			}
		}
	}

	for (int32 Index = Ready.Num() - 1; Index >= 0; --Index) {
		URigVMNode* Node = Ready[Index];

		if (Node->IsMutable()) continue;

		const TArray<URigVMNode*>* NodeTargets = Targets.Find(Node);

		if (NodeTargets == nullptr || NodeTargets->Num() == 0) continue;

		int32 Earliest = MAX_int32;

		for (URigVMNode* Target : *NodeTargets) {
			Earliest = FMath::Min(Earliest, Columns.FindOrAdd(Target));
		}

		if (Earliest != MAX_int32) {
			int32& Column = Columns.FindChecked(Node);
			Column = FMath::Max(Column, Earliest - 1);
		}
	}

	TMap<int32, TArray<URigVMNode*>> ByColumn;
	int32 LastColumn = 0;

	for (URigVMNode* Node : GraphNodes) {
		const int32 Column = Columns.Contains(Node) ? Columns.FindChecked(Node) : 0;

		ByColumn.FindOrAdd(Column).Add(Node);
		LastColumn = FMath::Max(LastColumn, Column);
	}

	TMap<URigVMNode*, float> Rows;

	for (int32 Column = 0; Column <= LastColumn; ++Column) {
		TArray<URigVMNode*>* ColumnNodes = ByColumn.Find(Column);
		if (ColumnNodes == nullptr) continue;

		ColumnNodes->StableSort([&Sources, &Rows](URigVMNode& A, URigVMNode& B) {
			auto Average = [&Sources, &Rows](URigVMNode& Node) -> float {
				const TArray<URigVMNode*>* NodeSources = Sources.Find(&Node);

				if (NodeSources == nullptr || NodeSources->Num() == 0) return 0.0f;

				float Sum = 0.0f;
				int32 Count = 0;

				for (URigVMNode* Source : *NodeSources) {
					if (const float* Row = Rows.Find(Source)) {
						Sum += *Row;
						Count++;
					}
				}

				return Count > 0 ? Sum / static_cast<float>(Count) : 0.0f;
			};

			return Average(A) < Average(B);
		});

		float Row = 0.0f;

		for (URigVMNode* Node : *ColumnNodes) {
			Controller->SetNodePosition(Node, FVector2D(480.0f * static_cast<float>(Column), Row), false, false, false);

			Rows.Add(Node, Row);

			Row += 120.0f + 24.0f * static_cast<float>(Node->GetPins().Num());
		}
	}
}

void FRigVMGraphReconstruction::PromoteLocalVariables() {
	struct FLoose {
		FString VariableName;
		URigVMPin* Pin = nullptr;
	};

	TArray<FLoose> LooseEnds;

	TMap<int32, int32> FirstToucher;

	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex) {
		FNode& Node = Nodes[NodeIndex];

		if (Node.Node == nullptr) continue;

		for (int32 OperandIndex = 0; OperandIndex < Node.Operands.Num(); ++OperandIndex) {
			if (!Node.Arguments.IsValidIndex(OperandIndex)) continue;
			if (Node.Arguments[OperandIndex].IsEmpty()) continue;
			if (Node.Operands[OperandIndex].Key != MemoryTypeWork) continue;

			const int32 RegisterIndex = Node.Operands[OperandIndex].Value;

			const FRegister* Register = FindRegister(MemoryTypeWork, RegisterIndex);

			if (Register == nullptr || !Register->Name.StartsWith(TEXT("LocalVariable"))) continue;

			const int32* Existing = FirstToucher.Find(RegisterIndex);

			if (Existing != nullptr && Nodes[*Existing].Instruction <= Node.Instruction) continue;

			URigVMPin* Pin = Node.Node->FindPin(Node.Arguments[OperandIndex]);

			if (Pin == nullptr || Pin->GetDirection() != ERigVMPinDirection::IO) continue;
			if (Pin->GetSourceLinks(true).Num() > 0) continue;

			FString VariableName = Register->Name.RightChop(13);

			while (VariableName.StartsWith(TEXT("_"))) {
				VariableName = VariableName.RightChop(1);
			}

			if (VariableName.IsEmpty()) continue;

			FirstToucher.Add(RegisterIndex, NodeIndex);

			LooseEnds.Add({ VariableName, Pin });
		}
	}

	for (const FLoose& Loose : LooseEnds) {
		if (Loose.Pin->GetSourceLinks(true).Num() > 0) continue;

		const TArray<FRigVMGraphVariableDescription> Existing = Blueprint->GetMemberVariables();

		const bool bExists = Existing.ContainsByPredicate([&Loose](const FRigVMGraphVariableDescription& Description) {
			return Description.Name.ToString() == Loose.VariableName;
		});

		if (!bExists) {
			FString TypePath = Loose.Pin->GetCPPType();

			if (const UObject* TypeObject = Loose.Pin->GetCPPTypeObject()) {
				TypePath = TypeObject->GetPathName();

				if (Loose.Pin->IsArray()) {
					TypePath = FString::Printf(TEXT("TArray<%s>"), *TypePath);
				}
			}

			if (Blueprint->AddMemberVariable(FName(*Loose.VariableName), TypePath, false, false, Loose.Pin->GetDefaultValue()).IsNone()) {
				continue;
			}
		}

		const FVector2D Position = Loose.Pin->GetNode()->GetPosition() + FVector2D(-480.0f, 40.0f);

		URigVMNode* Getter = Controller->AddVariableNode(
			FName(*Loose.VariableName),
			Loose.Pin->GetCPPType(),
			Loose.Pin->GetCPPTypeObject(),
			true,
			FString(),
			Position,
			FString(),
			false,
			false
		);

		if (Getter == nullptr) continue;

		URigVMPin* ValuePin = Getter->FindPin(TEXT("Value"));

		if (ValuePin != nullptr && Controller->AddLink(ValuePin->GetPinPath(), Loose.Pin->GetPinPath(), false, false)) {
			Stats.Links++;
			Stats.VariableNodes++;
		} else {
			Controller->RemoveNode(Getter, false, false);
		}
	}
}

void FRigVMGraphReconstruction::ConsolidateDefaults() {
	for (const FNode& Node : Nodes) {
		if (Node.Node == nullptr) continue;

		for (URigVMPin* Pin : Node.Node->GetPins()) {
			if (Pin->GetDirection() == ERigVMPinDirection::Output || Pin->IsExecuteContext()) continue;
			if (Pin->GetSubPins().Num() == 0) continue;

			const FString Composed = Pin->GetDefaultValue();

			if (Composed.IsEmpty() || Composed == TEXT("()")) continue;

			RigSetPinDefault(Controller, Pin->GetPinPath(), Composed);
		}
	}
}

void FRigVMGraphReconstruction::ApplyDefaults() {
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex) {
		FNode& Node = Nodes[NodeIndex];

		if (Node.Node == nullptr || Node.Function == nullptr) continue;

		for (int32 OperandIndex = 0; OperandIndex < Node.Operands.Num(); ++OperandIndex) {
			if (!Node.Arguments.IsValidIndex(OperandIndex)) continue;

			const int32 MemoryType = Node.Operands[OperandIndex].Key;

			if (MemoryType != MemoryTypeLiteral && MemoryType != MemoryTypeWork) continue;

			const FRegister* Register = FindRegister(MemoryType, Node.Operands[OperandIndex].Value);

			if (Register == nullptr || !Register->Value.IsValid()) continue;
			if (MemoryType == MemoryTypeWork && Register->Owner != NodeIndex) continue;

			URigVMPin* Pin = Node.Node->FindPin(Node.Arguments[OperandIndex]);

			if (Pin == nullptr || Pin->GetDirection() == ERigVMPinDirection::Output) continue;

			if (SetPinDefault(Pin, Register->Value)) {
				Stats.Defaults++;
			}
		}
	}
}

URigVMPin* FRigVMGraphReconstruction::FindExecuteOutput(URigVMNode* Node, const FString& Label) {
	if (Node == nullptr) {
		return nullptr;
	}

	if (!Label.IsEmpty()) {
		if (URigVMPin* Pin = Node->FindPin(Label)) {
			if (Pin->IsExecuteContext()) {
				return Pin;
			}
		}
	}

	if (URigVMPin* Completed = Node->FindPin(TEXT("Completed"))) {
		if (Completed->IsExecuteContext()) {
			return Completed;
		}
	}

	URigVMPin* Fallback = nullptr;

	for (URigVMPin* Pin : Node->GetPins()) {
		if (!Pin->IsExecuteContext()) continue;

		if (Pin->GetDirection() == ERigVMPinDirection::Output) {
			return Pin;
		}

		if (Pin->GetDirection() == ERigVMPinDirection::IO && Fallback == nullptr) {
			Fallback = Pin;
		}
	}

	return Fallback;
}

URigVMPin* FRigVMGraphReconstruction::FindExecuteInput(URigVMNode* Node) {
	if (Node == nullptr) {
		return nullptr;
	}

	for (URigVMPin* Pin : Node->GetPins()) {
		if (!Pin->IsExecuteContext()) continue;

		if (Pin->GetDirection() == ERigVMPinDirection::Input || Pin->GetDirection() == ERigVMPinDirection::IO) {
			return Pin;
		}
	}

	return nullptr;
}

bool FRigVMGraphReconstruction::Link(const int32 SourceNode, const FString& SourcePin, const int32 TargetNode, const FString& TargetPin) {
	if (!Nodes.IsValidIndex(SourceNode) || !Nodes.IsValidIndex(TargetNode)) {
		return false;
	}

	URigVMNode* Source = Nodes[SourceNode].Node;
	URigVMNode* Target = Nodes[TargetNode].Node;

	if (Source == nullptr || Target == nullptr || Source == Target) {
		return false;
	}

	URigVMPin* OutputPin = Source->FindPin(SourcePin);
	URigVMPin* InputPin = Target->FindPin(TargetPin);

	if (OutputPin == nullptr || InputPin == nullptr) {
		return false;
	}

	for (const URigVMLink* ExistingLink : InputPin->GetSourceLinks()) {
		if (ExistingLink->GetSourcePin() == OutputPin) {
			return false;
		}
	}

	if (Controller->AddLink(OutputPin->GetPinPath(), InputPin->GetPinPath(), false, false)) {
		Stats.Links++;

		return true;
	}

	if (Controller->AddLink(InputPin->GetPinPath(), OutputPin->GetPinPath(), false, false)) {
		Stats.Links++;

		return true;
	}

	auto RetryUnresolved = [this, OutputPin, InputPin](URigVMNode* TemplateNode, const URigVMPin* TemplatePin, const URigVMPin* OtherPin) -> bool {
		if (Cast<URigVMTemplateNode>(TemplateNode) == nullptr) return false;
		if (TemplatePin->GetCPPType() == OtherPin->GetCPPType()) return false;

		if (!Controller->UnresolveTemplateNodes(TArray<URigVMNode*>{ TemplateNode }, false)) return false;

		if (Controller->AddLink(OutputPin->GetPinPath(), InputPin->GetPinPath(), false, false)) {
			Stats.Links++;

			return true;
		}

		return false;
	};

	if (RetryUnresolved(InputPin->GetNode(), InputPin, OutputPin)) return true;
	if (RetryUnresolved(OutputPin->GetNode(), OutputPin, InputPin)) return true;

	UE_LOG(LogReflection, Verbose, TEXT("\"%s\" wouldn't link to \"%s\""), *OutputPin->GetPinPath(), *InputPin->GetPinPath());

	Stats.FailedLinks++;

	return false;
}

FString FRigVMGraphReconstruction::GetExternalVariable(const int32 RegisterIndex) const {
	const TArray<FRigVMGraphVariableDescription> Variables = Blueprint->GetMemberVariables();

	if (!Variables.IsValidIndex(RegisterIndex)) {
		return FString();
	}

	return Variables[RegisterIndex].Name.ToString();
}

URigVMNode* FRigVMGraphReconstruction::FindOrAddVariableGetter(const FString& VariableName, const int32 ConsumerNode) {
	if (VariableName.IsEmpty()) {
		return nullptr;
	}

	const FString CacheKey = FString::Printf(TEXT("%s|%d"), *VariableName, ConsumerNode);

	if (URigVMNode** Existing = VariableGetters.Find(CacheKey)) {
		return *Existing;
	}

	const TArray<FRigVMGraphVariableDescription> Variables = Blueprint->GetMemberVariables();
	const FRigVMGraphVariableDescription* Variable = Variables.FindByPredicate([&VariableName](const FRigVMGraphVariableDescription& Description) {
		return Description.Name.ToString() == VariableName;
	});

	if (Variable == nullptr) {
		return nullptr;
	}

	URigVMNode* Getter = Controller->AddVariableNode(
		Variable->Name,
		Variable->CPPType,
		Variable->CPPTypeObject,
		true,
		FString(),
		FVector2D(-400.0f, 260.0f * static_cast<float>(VariableGetters.Num())),
		FString(),
		false,
		false
	);

	if (Getter != nullptr) {
		Stats.VariableNodes++;

		VariableGetters.Add(CacheKey, Getter);
	}

	return Getter;
}

void FRigVMGraphReconstruction::CreateLinks(const TArray<FUObjectJsonValueExport>& Instructions) {
	for (bool bChanged = true; bChanged;) {
		bChanged = false;

		for (const FUObjectJsonValueExport& Instruction : Instructions) {
			if (Instruction.GetInteger(TEXT("OpCode"), -1) != OpCodeCopy) continue;

			const FUObjectJsonValueExport Source = Instruction.GetObject(TEXT("Source"));
			const FUObjectJsonValueExport Target = Instruction.GetObject(TEXT("Target"));

			if (!Source.JsonObject.IsValid() || !Target.JsonObject.IsValid()) continue;

			if (Source.GetInteger(TEXT("RegisterOffset"), NoRegisterOffset) != NoRegisterOffset) continue;
			if (Target.GetInteger(TEXT("RegisterOffset"), NoRegisterOffset) != NoRegisterOffset) continue;

			if (Source.GetInteger(TEXT("MemoryType"), INDEX_NONE) != MemoryTypeWork) continue;
			if (Target.GetInteger(TEXT("MemoryType"), INDEX_NONE) != MemoryTypeWork) continue;

			FRegister* SourceRegister = FindRegister(MemoryTypeWork, Source.GetInteger(TEXT("RegisterIndex"), INDEX_NONE));
			FRegister* TargetRegister = FindRegister(MemoryTypeWork, Target.GetInteger(TEXT("RegisterIndex"), INDEX_NONE));

			if (SourceRegister == nullptr || TargetRegister == nullptr) continue;

			if (SourceRegister->Owner != INDEX_NONE && TargetRegister->Owner == INDEX_NONE) {
				TargetRegister->Owner = SourceRegister->Owner;
				TargetRegister->OwnerPin = SourceRegister->OwnerPin;

				bChanged = true;
			} else if (TargetRegister->Owner != INDEX_NONE && SourceRegister->Owner == INDEX_NONE) {
				SourceRegister->Owner = TargetRegister->Owner;
				SourceRegister->OwnerPin = TargetRegister->OwnerPin;

				bChanged = true;
			}
		}
	}

	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex) {
		FNode& Node = Nodes[NodeIndex];

		if (Node.Node == nullptr) continue;

		for (int32 OperandIndex = 0; OperandIndex < Node.Operands.Num(); ++OperandIndex) {
			if (!Node.Arguments.IsValidIndex(OperandIndex)) continue;

			const int32 MemoryType = Node.Operands[OperandIndex].Key;
			const int32 RegisterIndex = Node.Operands[OperandIndex].Value;

			const FString PinName = Node.Arguments[OperandIndex];

			if (MemoryType == MemoryTypeExternal) {
				URigVMNode* Getter = FindOrAddVariableGetter(GetExternalVariable(RegisterIndex), NodeIndex);

				if (Getter == nullptr) continue;

				URigVMPin* ValuePin = Getter->FindPin(TEXT("Value"));
				URigVMPin* TargetPin = Node.Node->FindPin(PinName);

				if (ValuePin != nullptr && TargetPin != nullptr) {
					if (Controller->AddLink(ValuePin->GetPinPath(), TargetPin->GetPinPath(), false, false)) {
						Stats.Links++;
					} else {
						Stats.FailedLinks++;
					}
				}

				continue;
			}

		}
	}

	struct FToucher {
		int32 Instruction = INDEX_NONE;
		int32 Node = INDEX_NONE;

		FString Pin;
	};

	TMap<int32, TArray<FToucher>> Touchers;

	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex) {
		FNode& Node = Nodes[NodeIndex];

		if (Node.Node == nullptr) continue;

		for (int32 OperandIndex = 0; OperandIndex < Node.Operands.Num(); ++OperandIndex) {
			if (!Node.Arguments.IsValidIndex(OperandIndex)) continue;
			if (Node.Arguments[OperandIndex].IsEmpty()) continue;
			if (Node.Operands[OperandIndex].Key != MemoryTypeWork) continue;

			Touchers.FindOrAdd(Node.Operands[OperandIndex].Value).Add({ Node.Instruction, NodeIndex, Node.Arguments[OperandIndex] });
		}
	}

	for (TPair<int32, TArray<FToucher>>& Pair : Touchers) {
		Pair.Value.Sort([](const FToucher& A, const FToucher& B) {
			return A.Instruction < B.Instruction;
		});

		const FRegister* Register = FindRegister(MemoryTypeWork, Pair.Key);

		int32 LastWriterNode = INDEX_NONE;
		FString LastWriterPin;

		for (const FToucher& Toucher : Pair.Value) {
			URigVMNode* TouchingNode = Nodes[Toucher.Node].Node;
			if (TouchingNode == nullptr) continue;

			const URigVMPin* Pin = TouchingNode->FindPin(Toucher.Pin);
			if (Pin == nullptr) continue;

			const ERigVMPinDirection Direction = Pin->GetDirection();
			const bool bReads = Direction == ERigVMPinDirection::Input || Direction == ERigVMPinDirection::IO;
			const bool bWrites = Direction == ERigVMPinDirection::Output || Direction == ERigVMPinDirection::IO;

			if (bReads) {
				if (LastWriterNode != INDEX_NONE && LastWriterNode != Toucher.Node) {
					Link(LastWriterNode, LastWriterPin, Toucher.Node, Toucher.Pin);
				} else if (LastWriterNode == INDEX_NONE && Register != nullptr && Register->Owner != INDEX_NONE && Register->Owner != Toucher.Node) {
					Link(Register->Owner, Register->OwnerPin, Toucher.Node, Toucher.Pin);
				}
			}

			if (bWrites) {
				LastWriterNode = Toucher.Node;
				LastWriterPin = Toucher.Pin;
			}
		}
	}

	for (int32 Index = 0; Index < Instructions.Num(); ++Index) {
		const FUObjectJsonValueExport& Instruction = Instructions[Index];

		if (Instruction.GetInteger(TEXT("OpCode"), -1) != OpCodeCopy) continue;

		const FUObjectJsonValueExport Source = Instruction.GetObject(TEXT("Source"));
		const FUObjectJsonValueExport Target = Instruction.GetObject(TEXT("Target"));

		if (!Source.JsonObject.IsValid() || !Target.JsonObject.IsValid()) continue;

		const int32 SourceMemory = Source.GetInteger(TEXT("MemoryType"), INDEX_NONE);
		const int32 TargetMemory = Target.GetInteger(TEXT("MemoryType"), INDEX_NONE);

		const int32 SourceIndex = Source.GetInteger(TEXT("RegisterIndex"), INDEX_NONE);
		const int32 TargetIndex = Target.GetInteger(TEXT("RegisterIndex"), INDEX_NONE);

		const FRegister* SourceRegister = FindRegister(SourceMemory, SourceIndex);
		const FRegister* TargetRegister = FindRegister(TargetMemory, TargetIndex);

		const FString SourceSegment = GetSegmentPath(SourceMemory, Source.GetInteger(TEXT("RegisterOffset"), NoRegisterOffset));
		const FString TargetSegment = GetSegmentPath(TargetMemory, Target.GetInteger(TEXT("RegisterOffset"), NoRegisterOffset));

		auto WithSegment = [](const FString& PinName, const FString& Segment) {
			return Segment.IsEmpty() ? PinName : PinName + TEXT(".") + Segment;
		};

		if (TargetMemory == MemoryTypeExternal) {
			if (SourceRegister == nullptr || SourceRegister->Owner == INDEX_NONE) continue;

			const FString VariableName = GetExternalVariable(TargetIndex);
			const TArray<FRigVMGraphVariableDescription> Variables = Blueprint->GetMemberVariables();

			const FRigVMGraphVariableDescription* Variable = Variables.FindByPredicate([&VariableName](const FRigVMGraphVariableDescription& Description) {
				return Description.Name.ToString() == VariableName;
			});

			if (Variable == nullptr) continue;

			URigVMNode* Setter = Controller->AddVariableNode(
				Variable->Name,
				Variable->CPPType,
				Variable->CPPTypeObject,
				false,
				FString(),
				FVector2D(400.0f * static_cast<float>(Index / 12), 260.0f * static_cast<float>(Index % 12)),
				FString(),
				false,
				false
			);

			if (Setter == nullptr) continue;

			Stats.VariableNodes++;

			FNode SetterNode;
			SetterNode.Instruction = Index;
			SetterNode.Name = Setter->GetName();
			SetterNode.Node = Setter;

			const int32 SetterIndex = Nodes.Add(SetterNode);
			InstructionToNode.Add(Index, SetterIndex);

			Link(SourceRegister->Owner, WithSegment(SourceRegister->OwnerPin, SourceSegment), SetterIndex, TEXT("Value"));

			continue;
		}

		if (SourceMemory == MemoryTypeExternal) {
			if (TargetRegister == nullptr || TargetRegister->Owner == INDEX_NONE) continue;

			URigVMNode* Getter = FindOrAddVariableGetter(GetExternalVariable(SourceIndex), TargetRegister->Owner);

			if (Getter == nullptr) continue;

			URigVMPin* ValuePin = Getter->FindPin(TEXT("Value"));
			URigVMPin* TargetPin = Nodes[TargetRegister->Owner].Node != nullptr
				? Nodes[TargetRegister->Owner].Node->FindPin(WithSegment(TargetRegister->OwnerPin, TargetSegment))
				: nullptr;

			if (ValuePin != nullptr && TargetPin != nullptr) {
				if (Controller->AddLink(ValuePin->GetPinPath(), TargetPin->GetPinPath(), false, false)) {
					Stats.Links++;
				} else {
					Stats.FailedLinks++;
				}
			}

			continue;
		}

		if (SourceMemory == MemoryTypeLiteral) {
			if (TargetRegister == nullptr || TargetRegister->Owner == INDEX_NONE) continue;
			if (SourceRegister == nullptr || !SourceRegister->Value.IsValid()) continue;

			URigVMNode* TargetNode = Nodes[TargetRegister->Owner].Node;

			if (TargetNode == nullptr) continue;

			TSharedPtr<FJsonValue> SourceValue = SourceRegister->Value;

			if (!SourceSegment.IsEmpty()) {
				SourceValue = ResolveJsonSegment(SourceValue, SourceSegment);
			}

			if (!SourceValue.IsValid()) continue;

			if (SetPinDefault(TargetNode->FindPin(WithSegment(TargetRegister->OwnerPin, TargetSegment)), SourceValue)) {
				Stats.Defaults++;
			}

			continue;
		}

		if (SourceRegister == nullptr || TargetRegister == nullptr) continue;
		if (SourceRegister->Owner == INDEX_NONE || TargetRegister->Owner == INDEX_NONE) continue;

		Link(
			SourceRegister->Owner, WithSegment(SourceRegister->OwnerPin, SourceSegment),
			TargetRegister->Owner, WithSegment(TargetRegister->OwnerPin, TargetSegment)
		);
	}
}

void FRigVMGraphReconstruction::CreateExecution(const TArray<FUObjectJsonValueExport>& Instructions, const FUObjectJsonValueExport& ByteCode) {
	TArray<FBlock> Blocks;

	if (ByteCode.Has(TEXT("BranchInfos"))) {
		for (const FUObjectJsonValueExport& BranchInfo : ByteCode.GetArray(TEXT("BranchInfos"))) {
			FBlock Block;
			Block.First = BranchInfo.GetInteger(TEXT("FirstInstruction"), INDEX_NONE);
			Block.Last = BranchInfo.GetInteger(TEXT("LastInstruction"), INDEX_NONE);
			Block.Label = BranchInfo.Has(TEXT("Label")) ? BranchInfo.GetString(TEXT("Label")) : FString();

			Block.bLazy = BranchInfo.GetInteger(TEXT("ArgumentIndex"), INDEX_NONE) >= 0;

			const int32 Instruction = BranchInfo.GetInteger(TEXT("InstructionIndex"), INDEX_NONE);

			for (int32 Index = Instruction; Index >= 0; --Index) {
				if (InstructionToNode.Contains(Index)) {
					Block.Owner = Index;

					break;
				}
			}

			if (Block.First <= Block.Last && Block.First != INDEX_NONE) {
				Blocks.Add(Block);
			}
		}
	}

	for (const FUObjectJsonValueExport& Instruction : Instructions) {
		if (Instruction.GetInteger(TEXT("OpCode"), -1) != OpCodeRunInstructions) continue;

		FBlock Block;
		Block.First = Instruction.GetInteger(TEXT("StartInstruction"), INDEX_NONE);
		Block.Last = Instruction.GetInteger(TEXT("EndInstruction"), INDEX_NONE);
		Block.bLazy = true;

		if (Block.First <= Block.Last && Block.First != INDEX_NONE) {
			Blocks.Add(Block);
		}
	}

	auto FindBlock = [&Blocks](const int32 Instruction) -> const FBlock* {
		const FBlock* Innermost = nullptr;

		for (const FBlock& Block : Blocks) {
			if (Instruction < Block.First || Instruction > Block.Last) continue;

			if (Innermost == nullptr || (Block.Last - Block.First) < (Innermost->Last - Innermost->First)) {
				Innermost = &Block;
			}
		}

		return Innermost;
	};

	TMap<const FBlock*, int32> Previous;
	TMap<FString, int32> RootPrevious;

	TArray<int32> SortedInstructions;
	InstructionToNode.GetKeys(SortedInstructions);
	SortedInstructions.Sort();

	TArray<int32> EntryInstructions;

	if (ByteCode.Has(TEXT("Entries"))) {
		for (const TSharedPtr<FJsonValue>& Entry : ByteCode.JsonObject->GetArrayField(TEXT("Entries"))) {
			EntryInstructions.Add(ReadEntryInstruction(Entry->AsString()));
		}
	}

	for (const int32 Instruction : SortedInstructions) {
		const int32 NodeIndex = InstructionToNode.FindChecked(Instruction);

		URigVMNode* Node = Nodes[NodeIndex].Node;

		if (Node == nullptr) continue;

		const bool bTakesExecution = FindExecuteInput(Node) != nullptr;
		const bool bPassesExecution = FindExecuteOutput(Node, FString()) != nullptr;

		if (!bTakesExecution && !bPassesExecution) continue;

		const FBlock* Block = FindBlock(Instruction);

		if (Block != nullptr && Block->bLazy) continue;

		int32 PreviousNode = INDEX_NONE;

		if (Block != nullptr) {
			if (const int32* Found = Previous.Find(Block)) {
				PreviousNode = *Found;
			}
		} else {
			if (EntryInstructions.Contains(Instruction)) {
				RootPrevious.Remove(TEXT("Root"));
			} else if (const int32* Found = RootPrevious.Find(TEXT("Root"))) {
				PreviousNode = *Found;
			}
		}

		if (!bTakesExecution) {
		} else if (PreviousNode == INDEX_NONE && Block != nullptr && Block->Owner != INDEX_NONE) {
			if (const int32* OwnerNode = InstructionToNode.Find(Block->Owner)) {
				URigVMPin* OutputPin = FindExecuteOutput(Nodes[*OwnerNode].Node, Block->Label);
				URigVMPin* InputPin = FindExecuteInput(Node);

				if (OutputPin != nullptr && InputPin != nullptr) {
					if (Controller->AddLink(OutputPin->GetPinPath(), InputPin->GetPinPath(), false, false)) {
						Stats.Links++;
					} else {
						Stats.FailedLinks++;
					}
				}
			}
		} else if (PreviousNode != INDEX_NONE) {
			URigVMPin* OutputPin = FindExecuteOutput(Nodes[PreviousNode].Node, FString());
			URigVMPin* InputPin = FindExecuteInput(Node);

			if (OutputPin != nullptr && InputPin != nullptr) {
				if (Controller->AddLink(OutputPin->GetPinPath(), InputPin->GetPinPath(), false, false)) {
					Stats.Links++;
				} else {
					Stats.FailedLinks++;
				}
			}
		}

		if (!bPassesExecution) continue;

		if (Block != nullptr) {
			Previous.Add(Block, NodeIndex);
		} else {
			RootPrevious.Add(TEXT("Root"), NodeIndex);
		}
	}
}

#endif
