#pragma once

#include "Engine/Compatibility.h"

#if REFLECTION_RIGVM

#if UE5_7_BEYOND
#include "ControlRigBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#endif

#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"

#if REFLECTION_CONTROL_RIG
#include "Factories/Factory.h"
#include "Rigs/RigHierarchy.h"
#endif

inline URigVMGraph* RigModelOf(UControlRigBlueprint* Blueprint) {
	if (Blueprint == nullptr) return nullptr;

#if ENGINE_UE5
	return Blueprint->GetDefaultModel();
#else
	return Blueprint->Model;
#endif
}

inline URigVMController* RigControllerOf(UControlRigBlueprint* Blueprint, URigVMGraph* Graph) {
	if (Blueprint == nullptr) return nullptr;

#if ENGINE_UE5
	return Blueprint->GetOrCreateController(Graph);
#else
	return Blueprint->Controller;
#endif
}

inline URigVMNode* RigAddNode(URigVMController* Controller, const FString& StructPath, const FVector2D& Position, const FString& Name) {
	if (Controller == nullptr) return nullptr;

#if ENGINE_UE5
	return Controller->AddUnitNodeFromStructPath(StructPath, TEXT("Execute"), Position, Name, false, false);
#else
	return Controller->AddStructNodeFromStructPath(StructPath, TEXT("Execute"), Position, Name, false);
#endif
}

inline bool RigLink(URigVMController* Controller, const FString& Output, const FString& Input) {
	if (Controller == nullptr || Output.IsEmpty() || Input.IsEmpty()) return false;

#if ENGINE_UE5
	return Controller->AddLink(Output, Input, false, false);
#else
	return Controller->AddLink(Output, Input, false);
#endif
}

inline bool RigSetDefault(URigVMController* Controller, const FString& Pin, const FString& Value) {
	if (Controller == nullptr || Pin.IsEmpty()) return false;

#if ENGINE_UE5
	return Controller->SetPinDefaultValue(Pin, Value, true, false, false);
#else
	return Controller->SetPinDefaultValue(Pin, Value, true, false, false);
#endif
}

inline URigVMPin* RigExecutePin(URigVMNode* Node) {
	if (Node == nullptr) return nullptr;

	for (URigVMPin* Pin : Node->GetPins()) {
		if (Pin != nullptr && Pin->IsExecuteContext()) return Pin;
	}

	return nullptr;
}

#if REFLECTION_CONTROL_RIG
inline UFactory* RigBlueprintFactory(UClass* ParentClass) {
	UClass* FactoryClass = FindClassByType(TEXT("ControlRigBlueprintFactory"));

	if (FactoryClass == nullptr) return nullptr;

	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	if (const FClassProperty* Parent = CastField<FClassProperty>(FactoryClass->FindPropertyByName(TEXT("ParentClass")))) {
		Parent->SetObjectPropertyValue(Parent->ContainerPtrToValuePtr<void>(Factory), ParentClass);
	}

	return Factory;
}

inline URigHierarchy* RigHierarchyOf(UControlRigBlueprint* Blueprint) {
	if (Blueprint == nullptr) return nullptr;

#if UE5_4_BEYOND
	return Blueprint->GetHierarchy();
#else
	return Blueprint->Hierarchy;
#endif
}
#endif

namespace RigUnits {
	static const FString BeginExecution = TEXT("/Script/ControlRig.RigUnit_BeginExecution");
	static const FString GetCurveValue = TEXT("/Script/ControlRig.RigUnit_GetCurveValue");
	static const FString SetCurveValue = TEXT("/Script/ControlRig.RigUnit_SetCurveValue");

#if ENGINE_UE5
	static const FString Multiply = TEXT("/Script/RigVM.RigVMFunction_MathFloatMul");
	static const FString Add = TEXT("/Script/RigVM.RigVMFunction_MathFloatAdd");
	static const FString Clamp = TEXT("/Script/RigVM.RigVMFunction_MathFloatClamp");
#else
	static const FString Multiply = TEXT("/Script/ControlRig.RigUnit_MathFloatMul");
	static const FString Add = TEXT("/Script/ControlRig.RigUnit_MathFloatAdd");
	static const FString Clamp = TEXT("/Script/ControlRig.RigUnit_MathFloatClamp");
#endif
}

#endif
