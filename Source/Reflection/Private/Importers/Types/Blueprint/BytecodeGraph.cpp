#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Blueprint/BytecodeGraph.h"
#include "Importers/Types/Blueprint/BlueprintUtilities.h"

#include "EdGraphSchema_K2.h"
#include "Engine/Compatibility.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "K2Node_AddComponent.h"
#include "Importers/Types/Blueprint/GraphTidy.h"
#include "Importers/Types/Blueprint/MacroPattern.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FormatText.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_GetClassDefaults.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/ActorComponent.h"
#include "Containers/ExportContainer.h"
#include "Serializers/ObjectSerializer.h"
#include "Importers/Constructor/Asset.h"
#include "Importers/Constructor/ImportIssues.h"
#include "Utilities/AssetPaths.h"
#include "Engine/Package.h"
#include "UObject/CoreRedirects.h"
#include "UObject/StructOnScope.h"
#include "Importers/Types/Blueprint/BlueprintGraphs.h"
#include "Importers/Types/Blueprint/BlueprintVariables.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_CallMaterialParameterCollectionFunction.h"
#include "K2Node_CallDataTableFunction.h"
#include "K2Node_GetDataTableRow.h"
#include "Kismet/DataTableFunctionLibrary.h"
#include "K2Node_CallArrayFunction.h"
#include "Kismet/KismetMathLibrary.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeMap.h"
#include "K2Node_GetArrayItem.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_MathExpression.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_SetFieldsInStruct.h"
#include "Kismet/KismetTextLibrary.h"
#include "K2Node_Select.h"
#include "K2Node_Timeline.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Serialization/JsonSerializer.h"

namespace {
	UScriptStruct* FindStructAnywhere(const FString& Named) {
		if (Named.IsEmpty()) return nullptr;

#if UE5_1_BEYOND
		return FindFirstObject<UScriptStruct>(*Named);
#else
		return FindObject<UScriptStruct>(ANY_PACKAGE, *Named);
#endif
	}
}

DECLARE_LOG_CATEGORY_CLASS(LogReflectionBytecode, All, All);

namespace {
	void SplitReference(const FUObjectJsonValueExport& Reference, FString& OutOwner, FString& OutMember) {
		FString Name = Reference.Has(TEXT("ObjectName")) ? Reference.GetString(TEXT("ObjectName")) : FString();

		if (Name.Contains(TEXT("'"))) {
			Name.Split(TEXT("'"), nullptr, &Name);
			Name.Split(TEXT("'"), &Name, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		}

		if (!Name.Split(TEXT(":"), &OutOwner, &OutMember)) {
			OutOwner = Name;
			OutMember = FString();
		}
	}

	bool IsCompilerLocal(const FString& Name) {
		return Name.StartsWith(TEXT("CallFunc_")) || Name.StartsWith(TEXT("K2Node_"));
	}

	FUObjectJsonValueExport DescribedBy(const FUObjectJsonValueExport& Property, const TCHAR* Field, FUObjectExportContainer* Container) {
		const FUObjectJsonValueExport Held = Property.GetObject(Field);

		if (Held.Has(TEXT("Type")) || Container == nullptr) return Held;

		if (const FUObjectExport* Named = Container->GetExportByObjectPath(Held.JsonObject); Named != nullptr && Named->IsJsonValid()) {
			return FUObjectJsonValueExport(Named->JsonObject);
		}

		return Held;
	}

	bool TypeOfProperty(const FUObjectJsonValueExport& Property, FEdGraphPinType& OutType, FUObjectExportContainer* Container) {
		if (!Property.Has(TEXT("Type"))) return false;

		const FString Kind = Property.GetString(TEXT("Type"));

		if (Kind == TEXT("ArrayProperty") || Kind == TEXT("SetProperty")) {
			const TCHAR* Names = Kind == TEXT("ArrayProperty") ? TEXT("Inner") : TEXT("ElementProp");

			if (!TypeOfProperty(DescribedBy(Property, Names, Container), OutType, Container)) return false;

			OutType.ContainerType = Kind == TEXT("ArrayProperty") ? EPinContainerType::Array : EPinContainerType::Set;

			return true;
		}

		if (Kind == TEXT("MapProperty")) {
			FEdGraphPinType Valued;

			if (!TypeOfProperty(DescribedBy(Property, TEXT("KeyProp"), Container), OutType, Container)) return false;
			if (!TypeOfProperty(DescribedBy(Property, TEXT("ValueProp"), Container), Valued, Container)) return false;

			OutType.ContainerType = EPinContainerType::Map;
			OutType.PinValueType = FEdGraphTerminalType::FromPinType(Valued);

			return true;
		}

		if (Kind == TEXT("IntProperty")) OutType.PinCategory = UEdGraphSchema_K2::PC_Int;
		else if (Kind == TEXT("Int64Property")) OutType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		else if (Kind == TEXT("BoolProperty")) OutType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		else if (Kind == TEXT("FloatProperty") || Kind == TEXT("DoubleProperty")) {
#if ENGINE_UE5
			OutType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
#else
			OutType.PinCategory = UEdGraphSchema_K2::PC_Float;
#endif
		}
		else if (Kind == TEXT("ByteProperty")) {
			OutType.PinCategory = UEdGraphSchema_K2::PC_Byte;

			if (Property.Has(TEXT("Enum"))) {
				FString Owner, Member;
				SplitReference(Property.GetObject(TEXT("Enum")), Owner, Member);

				if (!Owner.IsEmpty()) OutType.PinSubCategoryObject = FindEnumByType(Owner);
			}
		}
		else if (Kind == TEXT("NameProperty")) OutType.PinCategory = UEdGraphSchema_K2::PC_Name;
		else if (Kind == TEXT("StrProperty")) OutType.PinCategory = UEdGraphSchema_K2::PC_String;
		else if (Kind == TEXT("TextProperty")) OutType.PinCategory = UEdGraphSchema_K2::PC_Text;
		else if (Kind == TEXT("EnumProperty")) {
			FString Owner, Member;
			SplitReference(Property.GetObject(TEXT("Enum")), Owner, Member);

			OutType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			OutType.PinSubCategoryObject = FindEnumByType(Owner);
		}
		else if (Kind == TEXT("StructProperty")) {
			FString Owner, Member;
			SplitReference(Property.GetObject(TEXT("Struct")), Owner, Member);

			UScriptStruct* Struct = FindStructAnywhere(Owner);

			if (Struct == nullptr) return false;

			OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutType.PinSubCategoryObject = Struct;
		}
		else if (Kind == TEXT("ObjectProperty")) {
			FString Owner, Member;
			SplitReference(Property.GetObject(TEXT("PropertyClass")), Owner, Member);

			UClass* Class = const_cast<UClass*>(FindClassByType(Owner));

			if (Class == nullptr) {
				return Property.JsonObject.IsValid() && FBlueprintVariables::GetPinType(Property.JsonObject, OutType, Container);
			}

			OutType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutType.PinSubCategoryObject = Class;
		}
		else {
			return Property.JsonObject.IsValid() && FBlueprintVariables::GetPinType(Property.JsonObject, OutType, Container);
		}

		return true;
	}

}

namespace {
	UEdGraphPin* HandsBack(UK2Node* Node) {
		if (Node == nullptr) return nullptr;

		if (UEdGraphPin* Usual = Node->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output)) return Usual;

		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin == nullptr || Pin->Direction != EGPD_Output) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;

			return Pin;
		}

		return nullptr;
	}

	FUObjectJsonValueExport Inner(const FUObjectJsonValueExport& Expression) {
		if (!Expression.JsonObject.IsValid()) return FUObjectJsonValueExport();

		for (const TCHAR* Field : { TEXT("Target"), TEXT("Expression"), TEXT("Value"), TEXT("InnerExpression") }) {
			if (!Expression.Has(Field)) continue;

			const FUObjectJsonValueExport Held = Expression.GetObject(Field);

			if (Held.Has(TEXT("Token"))) return Held;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Expression.JsonObject->Values) {
			if (!Field.Value.IsValid() || Field.Value->Type != EJson::Object) continue;

			const FUObjectJsonValueExport Held(Field.Value->AsObject());

			if (Held.Has(TEXT("Token"))) return Held;
		}

		return FUObjectJsonValueExport();
	}

	FUObjectJsonValueExport Names(const FUObjectJsonValueExport& Expression) {
		if (!Expression.JsonObject.IsValid()) return FUObjectJsonValueExport();

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Expression.JsonObject->Values) {
			if (!Field.Value.IsValid() || Field.Value->Type != EJson::Object) continue;

			const FUObjectJsonValueExport Held(Field.Value->AsObject());

			if (Held.Has(TEXT("ObjectName"))) return Held;
		}

		return FUObjectJsonValueExport();
	}

	FString Calls(const FUObjectJsonValueExport& Expression) {
		if (!Expression.Has(TEXT("Function"))) return FString();

		if (Expression.JsonObject.IsValid() && Expression.JsonObject->HasTypedField<EJson::String>(TEXT("Function"))) {
			return Expression.GetString(TEXT("Function"));
		}

		FString Owner, Member;
		SplitReference(Expression.GetObject(TEXT("Function")), Owner, Member);

		return Member;
	}
}

template <typename T>
T* FBytecodeGraph::AddNode() {
	T* Node = NewObject<T>(Graph, T::StaticClass(), NAME_None, RF_Transactional);

	Graph->AddNode(Node, false, false);

	Node->CreateNewGuid();
	Node->PostPlacedNewNode();

	return Node;
}

template <typename T>
T* FBytecodeGraph::AddNodeOfClass(const TSubclassOf<T>& Class) {
	T* Node = NewObject<T>(Graph, Class != nullptr ? *Class : T::StaticClass(), NAME_None, RF_Transactional);

	Graph->AddNode(Node, false, false);

	Node->CreateNewGuid();
	Node->PostPlacedNewNode();

	return Node;
}

TSubclassOf<UK2Node_CallFunction> FBytecodeGraph::NodeClassFor(const UFunction* Function) {
	if (Function == nullptr) return UK2Node_CallFunction::StaticClass();

	if (Function->HasMetaData(FBlueprintMetadata::MD_CommutativeAssociativeBinaryOperator) && Function->HasAnyFunctionFlags(FUNC_BlueprintPure)) {
		return UK2Node_CommutativeAssociativeBinaryOperator::StaticClass();
	}

	if (Function->HasMetaData(FBlueprintMetadata::MD_MaterialParameterCollectionFunction)) {
		return UK2Node_CallMaterialParameterCollectionFunction::StaticClass();
	}

	if (Function->HasMetaData(FBlueprintMetadata::MD_DataTablePin)) {
		return UK2Node_CallDataTableFunction::StaticClass();
	}

	if (Function->HasMetaData(FBlueprintMetadata::MD_ArrayParam)) {
		return UK2Node_CallArrayFunction::StaticClass();
	}

	return UK2Node_CallFunction::StaticClass();
}

UFunction* FBytecodeGraph::FindFunctionOn(const UClass* Class, const FString& Member) {
	if (Class == nullptr || Member.IsEmpty()) return nullptr;

	if (UFunction* Found = Class->FindFunctionByName(*Member)) return Found;

	FCoreRedirectObjectName Called(*Member, *Class->GetName(), NAME_None);
	FCoreRedirectObjectName Now = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Function, Called);

	for (const UClass* Up = Class->GetSuperClass(); Now == Called && Up != nullptr; Up = Up->GetSuperClass()) {
		const FCoreRedirectObjectName Declared(*Member, *Up->GetName(), NAME_None);
		const FCoreRedirectObjectName Moved = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Function, Declared);

		if (Moved != Declared) {
			Called = Declared;
			Now = Moved;
		}
	}

	if (Now == Called) {
		static const TMap<FString, FString> Retired = {
			{ TEXT("KismetSystemLibrary.SetFloatPropertyByName"), TEXT("SetDoublePropertyByName") },
			{ TEXT("FortGameplayAbility.SetCameraMode"), TEXT("PushCameraMode") },
		};

		for (const UClass* Up = Class; Up != nullptr; Up = Up->GetSuperClass()) {
			if (const FString* Successor = Retired.Find(Up->GetName() + TEXT(".") + Member)) {
				if (UFunction* Found = Class->FindFunctionByName(**Successor)) return Found;
			}
		}

		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::IncludeInterfaces); It; ++It) {
			if (It->GetMetaData(FBlueprintMetadata::MD_DisplayName).Replace(TEXT(" "), TEXT("")) == Member) return *It;
		}

		return nullptr;
	}

	const UClass* On = Now.OuterName != Called.OuterName ? FindClassByType(Now.OuterName.ToString()) : Class;

	return On != nullptr ? On->FindFunctionByName(Now.ObjectName) : nullptr;
}

namespace {
	enum class EWritten : uint8 { Anything, Truth, Byte, Whole, Real, Named, Words, Said, Object, Struct };

	struct FWritten {
		EWritten Kind = EWritten::Anything;

		const UScriptStruct* Struct = nullptr;
		int32 Members = INDEX_NONE;
	};

	int32 MembersOf(const UStruct* Struct) {
		int32 Counted = 0;

		for (TFieldIterator<FProperty> It(Struct); It; ++It) Counted += FMath::Max(It->ArrayDim, 1);

		return Counted;
	}

	FWritten WhatWasWritten(const FUObjectJsonValueExport& Expression) {
		const FString Token = MacroReading::TokenOf(Expression);

		FWritten Written;

		if (Token == TEXT("EX_True") || Token == TEXT("EX_False")) {
			Written.Kind = EWritten::Truth;
		} else if (Token == TEXT("EX_ByteConst") || Token == TEXT("EX_IntConstByte")) {
			Written.Kind = EWritten::Byte;
		} else if (Token == TEXT("EX_IntConst") || Token == TEXT("EX_IntZero") || Token == TEXT("EX_IntOne")
			|| Token == TEXT("EX_Int64Const") || Token == TEXT("EX_UInt64Const") || Token == TEXT("EX_BitFieldConst")) {
			Written.Kind = EWritten::Whole;
		} else if (Token == TEXT("EX_FloatConst") || Token == TEXT("EX_DoubleConst")) {
			Written.Kind = EWritten::Real;
		} else if (Token == TEXT("EX_NameConst")) {
			Written.Kind = EWritten::Named;
		} else if (Token == TEXT("EX_StringConst") || Token == TEXT("EX_UnicodeStringConst")) {
			Written.Kind = EWritten::Words;
		} else if (Token == TEXT("EX_TextConst")) {
			Written.Kind = EWritten::Said;
		} else if (Token == TEXT("EX_ObjectConst") || Token == TEXT("EX_NoObject") || Token == TEXT("EX_NoInterface")
			|| Token == TEXT("EX_SoftObjectConst")) {
			Written.Kind = EWritten::Object;
		} else if (Token == TEXT("EX_VectorConst") || Token == TEXT("EX_Vector3fConst")) {
			Written.Kind = EWritten::Struct;
			Written.Struct = TBaseStructure<FVector>::Get();
			Written.Members = 3;
		} else if (Token == TEXT("EX_RotationConst")) {
			Written.Kind = EWritten::Struct;
			Written.Struct = TBaseStructure<FRotator>::Get();
			Written.Members = 3;
		} else if (Token == TEXT("EX_TransformConst")) {
			Written.Kind = EWritten::Struct;
			Written.Struct = TBaseStructure<FTransform>::Get();
			Written.Members = 3;
		} else if (Token == TEXT("EX_StructConst")) {
			FString Owner, Member;
			SplitReference(Expression.GetObject(TEXT("Struct")), Owner, Member);

			Written.Kind = EWritten::Struct;
			Written.Struct = FindStructByType(Member.IsEmpty() ? Owner : Member);
			Written.Members = Expression.Has(TEXT("Properties")) ? Expression.GetArray(TEXT("Properties")).Num() : 0;
		}

		return Written;
	}

	bool CouldHold(const FProperty* Property, const FWritten& Written) {
		if (Property == nullptr) return false;

		switch (Written.Kind) {
		case EWritten::Truth: return Property->IsA<FBoolProperty>();
		case EWritten::Byte: return Property->IsA<FByteProperty>() || Property->IsA<FEnumProperty>() || Property->IsA<FBoolProperty>();
		case EWritten::Whole: {
			const FNumericProperty* Counts = CastField<FNumericProperty>(Property);

			return (Counts != nullptr && !Counts->IsFloatingPoint()) || Property->IsA<FEnumProperty>() || Property->IsA<FBoolProperty>();
		}
		case EWritten::Real: return Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>();
		case EWritten::Named: return Property->IsA<FNameProperty>();
		case EWritten::Words: return Property->IsA<FStrProperty>();
		case EWritten::Said: return Property->IsA<FTextProperty>();
		case EWritten::Object: return Property->IsA<FObjectPropertyBase>() || Property->IsA<FInterfaceProperty>();
		case EWritten::Struct: {
			const FStructProperty* Holds = CastField<FStructProperty>(Property);

			if (Holds == nullptr || Holds->Struct == nullptr) return false;

			if (Written.Struct != nullptr && Holds->Struct == Written.Struct) return true;

			return Written.Members != INDEX_NONE && MembersOf(Holds->Struct) == Written.Members;
		}
		default: return true;
		}
	}
}

FString FBytecodeGraph::SpellValue(const FProperty* Property, const FUObjectJsonValueExport& Expression) {
	const FString Token = MacroReading::TokenOf(Expression);

	if (Token == TEXT("EX_StructConst")) return ReadStructConst(Expression);

	if (Token == TEXT("EX_ArrayConst")) {
		const TArray<FUObjectJsonValueExport> Values = Expression.Has(TEXT("Values"))
			? Expression.GetArray(TEXT("Values"))
			: TArray<FUObjectJsonValueExport>();

		const FArrayProperty* Holds = CastField<FArrayProperty>(Property);

		TArray<FString> Each;
		Each.Reserve(Values.Num());

		for (const FUObjectJsonValueExport& Held : Values) {
			FString Said = SpellValue(Holds != nullptr ? Holds->Inner : nullptr, Held);

			if (Said.IsEmpty()) Said = TEXT("()");

			Each.Add(Said);
		}

		return TEXT("(") + FString::Join(Each, TEXT(",")) + TEXT(")");
	}

	if (Token == TEXT("EX_VectorConst") || Token == TEXT("EX_Vector3fConst")) {
		const FUObjectJsonValueExport Held = Expression.GetObject(TEXT("Value"));

		return FString::Printf(TEXT("(X=%f,Y=%f,Z=%f)"), Held.GetNumber(TEXT("X")), Held.GetNumber(TEXT("Y")), Held.GetNumber(TEXT("Z")));
	}

	if (Token == TEXT("EX_RotationConst")) {
		const FUObjectJsonValueExport Held = Expression.GetObject(TEXT("Value"));

		return FString::Printf(TEXT("(Pitch=%f,Yaw=%f,Roll=%f)"), Held.GetNumber(TEXT("Pitch")), Held.GetNumber(TEXT("Yaw")), Held.GetNumber(TEXT("Roll")));
	}

	if (Token == TEXT("EX_TransformConst")) {
		const FUObjectJsonValueExport Held = Expression.GetObject(TEXT("Value"));

		const FUObjectJsonValueExport Where = Held.GetObject(TEXT("Translation"));
		const FUObjectJsonValueExport Turn = Held.GetObject(TEXT("Rotation"));
		const FUObjectJsonValueExport Size = Held.GetObject(TEXT("Scale3D"));

		return FString::Printf(TEXT("(Rotation=(X=%f,Y=%f,Z=%f,W=%f),Translation=(X=%f,Y=%f,Z=%f),Scale3D=(X=%f,Y=%f,Z=%f))"),
			Turn.GetNumber(TEXT("X")), Turn.GetNumber(TEXT("Y")), Turn.GetNumber(TEXT("Z")), Turn.GetNumber(TEXT("W")),
			Where.GetNumber(TEXT("X")), Where.GetNumber(TEXT("Y")), Where.GetNumber(TEXT("Z")),
			Size.GetNumber(TEXT("X")), Size.GetNumber(TEXT("Y")), Size.GetNumber(TEXT("Z")));
	}

	FString Said = ReadExpression(Expression).Literal;

	if (Said.IsEmpty()) return FString();

	if (Property != nullptr && Property->IsA<FBoolProperty>() && Said.IsNumeric()) {
		Said = FCString::Atod(*Said) != 0.0 ? TEXT("true") : TEXT("false");
	}

	if (Property != nullptr && Said.IsNumeric()) {
		const UEnum* Enum = nullptr;

		if (const FByteProperty* Byte = CastField<FByteProperty>(Property)) {
			Enum = Byte->Enum;
		} else if (const FEnumProperty* Enumerated = CastField<FEnumProperty>(Property)) {
			Enum = Enumerated->GetEnum();
		}

		const int64 Whole = static_cast<int64>(FMath::RoundToDouble(FCString::Atod(*Said)));

		if (Enum != nullptr) {
			const FString Named = Enum->GetNameStringByValue(Whole);

			if (!Named.IsEmpty()) return Named;
		}

		const FNumericProperty* Counts = CastField<FNumericProperty>(Property);

		if (Enum != nullptr || (Counts != nullptr && !Counts->IsFloatingPoint()) || Property->IsA<FEnumProperty>()) {
			return LexToString(Whole);
		}
	}

	if (Property != nullptr && (Property->IsA<FStrProperty>() || Property->IsA<FNameProperty>())) {
		return TEXT("\"") + Said.ReplaceCharWithEscapedChar() + TEXT("\"");
	}

	return Said;
}

FString FBytecodeGraph::ReadStructConst(const FUObjectJsonValueExport& Expression) {
	if (!Expression.Has(TEXT("Struct"))) return FString();

	FString Owner, Member;
	SplitReference(Expression.GetObject(TEXT("Struct")), Owner, Member);

	const FString Called = Member.IsEmpty() ? Owner : Member;
	const UScriptStruct* Struct = FindStructByType(Called);

	if (Struct == nullptr) {
		Unhandled.AddUnique(FString::Printf(TEXT("a \"%s\" written out in full, which this build does not carry"), *Called));

		return FString();
	}

	const TArray<FUObjectJsonValueExport> Listed = Expression.Has(TEXT("Properties"))
		? Expression.GetArray(TEXT("Properties"))
		: TArray<FUObjectJsonValueExport>();

	struct FPlace {
		FProperty* Property = nullptr;
		int32 Index = 0;
	};

	TArray<FPlace> Places;

	for (TFieldIterator<FProperty> It(Struct); It; ++It) {
		for (int32 Element = 0; Element < FMath::Max(It->ArrayDim, 1); Element++) {
			FPlace Place;

			Place.Property = *It;
			Place.Index = Element;

			Places.Add(Place);
		}
	}

	TArray<FWritten> Says;
	Says.Reserve(Listed.Num());

	for (const FUObjectJsonValueExport& Held : Listed) Says.Add(WhatWasWritten(Held));

	bool bInOrder = Places.Num() == Listed.Num();

	for (int32 At = 0; bInOrder && At < Listed.Num(); At++) {
		bInOrder = CouldHold(Places[At].Property, Says[At]);
	}

	TArray<int32> Goes;

	if (bInOrder) {
		for (int32 At = 0; At < Listed.Num(); At++) Goes.Add(At);
	} else {
		TArray<bool> Taken;
		Taken.Init(false, Places.Num());

		for (int32 At = 0; At < Listed.Num(); At++) {
			int32 Found = INDEX_NONE;

			for (int32 Place = 0; Place < Places.Num(); Place++) {
				if (Taken[Place] || !CouldHold(Places[Place].Property, Says[At])) continue;

				Found = Place;

				break;
			}

			if (Found == INDEX_NONE) {
				Unhandled.AddUnique(FString::Printf(
					TEXT("a \"%s\" the game wrote %d member(s) of, where this build has one of %d that none of them fit"),
					*Struct->GetName(), Listed.Num(), Places.Num()));

				return FString();
			}

			Taken[Found] = true;

			Goes.Add(Found);
		}

		UE_LOG(LogReflectionBytecode, Warning, TEXT("\"%s\" was written with %d member(s) where this build has %d, so its values were placed by what they are rather than counted off"),
			*Struct->GetName(), Listed.Num(), Places.Num());
	}

	if (Struct->StructFlags & STRUCT_ImportTextItemNative) {
		FStructOnScope Value(Struct);

		for (int32 At = 0; At < Listed.Num(); At++) {
			const FPlace& Place = Places[Goes[At]];

			const FString Held = SpellValue(Place.Property, Listed[At]);
			if (Held.IsEmpty()) continue;

#if UE5_1_BEYOND
			Place.Property->ImportText_Direct(*Held, Place.Property->ContainerPtrToValuePtr<void>(Value.GetStructMemory(), Place.Index), nullptr, PPF_Delimited);
#else
			Place.Property->ImportText(*Held, Place.Property->ContainerPtrToValuePtr<void>(Value.GetStructMemory(), Place.Index), PPF_Delimited, nullptr);
#endif
		}

		FString AsText;
		Struct->ExportText(AsText, Value.GetStructMemory(), nullptr, nullptr, PPF_None, nullptr);

		return AsText;
	}

	TArray<FString> Spelled;

	for (int32 At = 0; At < Listed.Num(); At++) {
		const FPlace& Place = Places[Goes[At]];

		const FString Held = SpellValue(Place.Property, Listed[At]);

		if (Held.IsEmpty()) continue;

		const FString Name = Place.Property->ArrayDim > 1
			? FString::Printf(TEXT("%s[%d]"), *Place.Property->GetName(), Place.Index)
			: Place.Property->GetName();

		Spelled.Add(Name + TEXT("=") + Held);
	}

	return TEXT("(") + FString::Join(Spelled, TEXT(",")) + TEXT(")");
}

FBytecodeGraph::FValue FBytecodeGraph::ReadMathExpression(const FUObjectJsonValueExport& Expression) {
	FValue Value;

	FString Spelled;
	TArray<TPair<FString, FUObjectJsonValueExport>> Inputs;

	if (!SpellMath(Expression, Spelled, Inputs)) return Value;

	UK2Node_MathExpression* Node = AddNode<UK2Node_MathExpression>();

	Node->OnRenameNode(Spelled);

	UEdGraphPin* Out = nullptr;

	for (UEdGraphPin* Pin : Node->Pins) {
		if (Pin != nullptr && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) {
			Out = Pin;

			break;
		}
	}

	if (Out == nullptr) {
		Graph->RemoveNode(Node);

		return Value;
	}

	for (const TPair<FString, FUObjectJsonValueExport>& Input : Inputs) {
		if (UEdGraphPin* Pin = Node->FindPin(*Input.Key, EGPD_Input)) {
			Give(Read(Input.Value), Pin);
		}
	}

	Placed++;

	Value.Pin = Out;

	return Value;
}

bool FBytecodeGraph::SpellMath(const FUObjectJsonValueExport& Expression, FString& Out, TArray<TPair<FString, FUObjectJsonValueExport>>& Inputs) {
	const FString Token = MacroReading::TokenOf(Expression);

	if (Token == TEXT("EX_DoubleConst") || Token == TEXT("EX_FloatConst") || Token == TEXT("EX_IntConst")) {
		if (double Number = 0.0; Expression.JsonObject.IsValid() && Expression.JsonObject->TryGetNumberField(TEXT("Value"), Number)) {
			Out = FString::SanitizeFloat(Number);
		} else {
			Out = Expression.Has(TEXT("Value")) ? Expression.GetString(TEXT("Value")) : FString();
		}

		return !Out.IsEmpty();
	}

	if (Token == TEXT("EX_InstanceVariable")) {
		Out = MacroReading::NamedProperty(Expression.GetObject(TEXT("Variable")));

		return !Out.IsEmpty() && !Out.Contains(TEXT(" "));
	}

	if (Token == TEXT("EX_LocalVariable")) {
		FString Name = MacroReading::NamedProperty(Expression.GetObject(TEXT("Variable")));

		Name.RemoveFromStart(TEXT("CallFunc_"));
		Name.RemoveFromEnd(TEXT("_ReturnValue"));

		if (Name.IsEmpty() || Name.Contains(TEXT(" "))) return false;

		if (!Inputs.ContainsByPredicate([&Name](const TPair<FString, FUObjectJsonValueExport>& One) { return One.Key == Name; })) {
			Inputs.Add(MakeTuple(Name, Expression));
		}

		Out = Name;

		return true;
	}

	if (Token == TEXT("EX_CallMath") || Token == TEXT("EX_FinalFunction")) {
		const UFunction* Function = ResolveFunction(Expression.GetObject(TEXT("Function")));

		if (Function == nullptr || !Function->HasMetaData(FBlueprintMetadata::MD_CompactNodeTitle)) return false;

		const FString Symbol = Function->GetMetaData(FBlueprintMetadata::MD_CompactNodeTitle);

		if (Symbol.IsEmpty() || FChar::IsAlnum(Symbol[0])) return false;

		const TArray<FUObjectJsonValueExport> Arguments = Expression.Has(TEXT("Parameters"))
			? Expression.GetArray(TEXT("Parameters"))
			: TArray<FUObjectJsonValueExport>();

		if (Arguments.Num() != 2) return false;

		FString Left, Right;

		if (!SpellMath(Arguments[0], Left, Inputs) || !SpellMath(Arguments[1], Right, Inputs)) return false;

		Out = FString::Printf(TEXT("(%s %s %s)"), *Left, *Symbol, *Right);

		return true;
	}

	return false;
}

UFunction* FBytecodeGraph::ResolveFunction(const FUObjectJsonValueExport& Reference) {
	FString Owner, Member;
	SplitReference(Reference, Owner, Member);

	if (Owner.IsEmpty() || Member.IsEmpty()) return nullptr;

	const UClass* Class = FindClassByType(Owner);

	if (Class == nullptr) return nullptr;

	return FindFunctionOn(Class, Member);
}

UK2Node* FBytecodeGraph::FormatText(const FUObjectJsonValueExport& Expression) {
	const TArray<FUObjectJsonValueExport> Arguments = Expression.Has(TEXT("Parameters"))
		? Expression.GetArray(TEXT("Parameters"))
		: TArray<FUObjectJsonValueExport>();

	if (Arguments.Num() < 2) return nullptr;

	const FValue Pattern = Read(Arguments[0]);
	const FValue Filled = Read(Arguments[1]);

	UK2Node_MakeArray* Gathered = Filled.Pin != nullptr ? Cast<UK2Node_MakeArray>(Filled.Pin->GetOwningNode()) : nullptr;

	if (Gathered == nullptr) return nullptr;

	TArray<TPair<FName, UEdGraphPin*>> Given;
	TArray<UEdGraphNode*> Spent;

	for (UEdGraphPin* Pin : Gathered->Pins) {
		if (Pin == nullptr || Pin->Direction != EGPD_Input || Pin->LinkedTo.Num() != 1) continue;

		UK2Node_MakeStruct* Made = Cast<UK2Node_MakeStruct>(Pin->LinkedTo[0]->GetOwningNode());

		if (Made == nullptr || Made->StructType == nullptr) return nullptr;
		if (Made->StructType->GetFName() != TEXT("FormatArgumentData")) return nullptr;

		const UEdGraphPin* Named = Made->FindPin(TEXT("ArgumentName"), EGPD_Input);

		if (Named == nullptr || Named->DefaultValue.IsEmpty()) return nullptr;

		const UEdGraphPin* Kind = Made->FindPin(TEXT("ArgumentValueType"), EGPD_Input);
		const FString Says = Kind != nullptr ? Kind->DefaultValue : FString();

		const TCHAR* Member = TEXT("ArgumentValue");

		if (Says == TEXT("Int")) Member = TEXT("ArgumentValueInt");
		else if (Says == TEXT("Float")) Member = TEXT("ArgumentValueFloat");
		else if (Says == TEXT("Double")) Member = TEXT("ArgumentValueDouble");
		else if (Says == TEXT("Gender")) Member = TEXT("ArgumentValueGender");

		UEdGraphPin* Holds = Made->FindPin(Member, EGPD_Input);
		UEdGraphPin* From = Holds != nullptr && Holds->LinkedTo.Num() == 1 ? Holds->LinkedTo[0] : nullptr;

		if (From != nullptr) {
			if (UK2Node_CallFunction* Turning = Cast<UK2Node_CallFunction>(From->GetOwningNode())) {
				const FString Called = Turning->FunctionReference.GetMemberName().ToString();

				const bool bDrawn = Called == TEXT("Conv_IntToText") || Called == TEXT("Conv_FloatToText");

				if (!bDrawn && Called.StartsWith(TEXT("Conv_")) && (Called.EndsWith(TEXT("ToText")) || Called.EndsWith(TEXT("ToInt64")))) {
					UEdGraphPin* Into = nullptr;

					for (UEdGraphPin* Taking : Turning->Pins) {
						if (Taking == nullptr || Taking->Direction != EGPD_Input) continue;
						if (Taking->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
						if (Taking->LinkedTo.Num() != 1) continue;

						Into = Taking->LinkedTo[0];

						break;
					}

					if (Into != nullptr) {
						From = Into;

						Spent.Add(Turning);
					}
				}
			}
		}

		Given.Add(TPair<FName, UEdGraphPin*>(FName(*Named->DefaultValue), From));

		Spent.Add(Made);
	}

	if (Given.Num() == 0) return nullptr;

	UK2Node_FormatText* Node = AddNode<UK2Node_FormatText>();

	Node->AllocateDefaultPins();

	UEdGraphPin* Words = Node->GetFormatPin();

	if (Words == nullptr) return nullptr;

	if (Pattern.Pin != nullptr) {
		Connect(Pattern.Pin, Words);
	} else if (!Pattern.Literal.IsEmpty()) {
		ApplyLiteral(Words, Pattern.Literal);
	}

	Node->PinDefaultValueChanged(Words);

	for (const TPair<FName, UEdGraphPin*>& One : Given) {
		if (One.Value == nullptr) continue;

		if (UEdGraphPin* Takes = Node->FindArgumentPin(One.Key)) {
			Connect(One.Value, Takes);
		}
	}

	for (UEdGraphNode* Over : Spent) Graph->RemoveNode(Over);

	Graph->RemoveNode(Gathered);

	Placed++;

	return Node;
}

UK2Node* FBytecodeGraph::GetDataTableRow(const FUObjectJsonValueExport& Expression) {
	const TArray<FUObjectJsonValueExport> Arguments = Expression.Has(TEXT("Parameters"))
		? Expression.GetArray(TEXT("Parameters"))
		: TArray<FUObjectJsonValueExport>();

	if (Arguments.Num() < 3) return nullptr;

	UK2Node_GetDataTableRow* Node = AddNode<UK2Node_GetDataTableRow>();

	Node->AllocateDefaultPins();

	UEdGraphPin* Table = Node->GetDataTablePin();
	UEdGraphPin* Row = Node->GetRowNamePin();
	UEdGraphPin* Result = Node->GetResultPin();

	if (Table == nullptr || Row == nullptr || Result == nullptr) {
		Graph->RemoveNode(Node);

		return nullptr;
	}

	if (const FValue Reading = Read(Arguments[0]); Reading.Pin != nullptr) {
		Connect(Reading.Pin, Table);
	} else if (!Reading.Literal.IsEmpty()) {
		ApplyLiteral(Table, Reading.Literal);

		Node->PinDefaultValueChanged(Table);
	}

	if (const FValue Named = Read(Arguments[1]); Named.Pin != nullptr) {
		Connect(Named.Pin, Row);
	} else if (!Named.Literal.IsEmpty()) {
		ApplyLiteral(Row, Named.Literal);
	}

	if (Arguments[2].Has(TEXT("Variable"))) {
		if (const FString Into = MacroReading::NamedProperty(Arguments[2].GetObject(TEXT("Variable"))); !Into.IsEmpty()) {
			Locals.Add(Into, Result);
		}
	}

	ChainExecution(Node);

	Placed++;

	return Node;
}

namespace {
	FString PlainlyCalled(FString Called) {
		Called.RemoveFromEnd(TEXT("_Delegate"), ESearchCase::IgnoreCase);
		Called.RemoveFromEnd(TEXT("Event"), ESearchCase::IgnoreCase);

		return Called;
	}

	FName DispatcherOn(const UClass* Owner, const FString& Called) {
		if (Owner == nullptr) return FName(*Called);

		if (FindFProperty<FMulticastDelegateProperty>(Owner, FName(*Called)) != nullptr) return FName(*Called);

		const FString Plain = PlainlyCalled(Called);

		FName Answers = NAME_None;
		int32 Found = 0;

		for (TFieldIterator<FMulticastDelegateProperty> It(Owner); It; ++It) {
			bool bSame = PlainlyCalled(It->GetName()).Equals(Plain, ESearchCase::IgnoreCase);

#if WITH_EDITORONLY_DATA
			if (!bSame) bSame = PlainlyCalled(It->GetMetaData(TEXT("DisplayName"))).Equals(Plain, ESearchCase::IgnoreCase);
#endif

			if (!bSame) continue;

			Answers = It->GetFName();
			Found++;
		}

		if (Found != 1) return FName(*Called);

		UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\" is what %s calls \"%s\" here"), *Answers.ToString(), *Owner->GetName(), *Called);

		return Answers;
	}
}

UEdGraphPin* FBytecodeGraph::PointAtDelegate(UK2Node_BaseMCDelegate* Node, const FUObjectJsonValueExport& Named) {
	if (Node == nullptr) return nullptr;

	if (Named.Has(TEXT("ObjectExpression"))) {
		const FValue On = Read(Named.GetObject(TEXT("ObjectExpression")));

		FUObjectJsonValueExport Points;

		if (Named.Has(TEXT("RValuePointer"))) {
			Points = Named.GetObject(TEXT("RValuePointer"));
		} else if (Named.Has(TEXT("ContextExpression"))) {
			const FUObjectJsonValueExport Inner = Named.GetObject(TEXT("ContextExpression"));

			if (Inner.Has(TEXT("Variable"))) Points = Inner.GetObject(TEXT("Variable"));
		}

		const FString Called = MacroReading::NamedProperty(Points);

		if (!Called.IsEmpty()) {
			UClass* Owner = nullptr;

			if (Points.Has(TEXT("ResolvedOwner"))) {
				FString Spelled = Points.GetObject(TEXT("ResolvedOwner")).GetString(TEXT("ObjectName"));

				if (Spelled.Contains(TEXT("'"))) {
					Spelled.Split(TEXT("'"), nullptr, &Spelled);
					Spelled.Split(TEXT("'"), &Spelled, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				}

				Owner = FindClassByType(Spelled);
			}

			if (Owner == nullptr) {
				FString Spelled = Points.GetString(TEXT("ObjectName"));

				if (Spelled.Split(TEXT("'"), nullptr, &Spelled)) {
					Spelled.RemoveFromEnd(TEXT("'"));

					if (FString Held; Spelled.Split(TEXT(":"), &Held, nullptr)) {
						Held.Split(TEXT("."), nullptr, &Held, ESearchCase::CaseSensitive, ESearchDir::FromEnd);

						Owner = FindClassByType(Held);
					}
				}
			}

			if (Owner == nullptr && Points.Has(TEXT("Owner"))) {
				FString OwnerName;
				FString Member;
				SplitReference(Points.GetObject(TEXT("Owner")), OwnerName, Member);

				if (!OwnerName.IsEmpty()) {
					Owner = const_cast<UClass*>(FindClassByType(OwnerName));

					if (Owner == nullptr) {
						BringInClass(Points.GetObject(TEXT("Owner")));

						Owner = const_cast<UClass*>(FindClassByType(OwnerName));
					}
				}
			}

			if (Owner != nullptr) Node->DelegateReference.SetExternalMember(DispatcherOn(Owner, Called), Owner);
			else Node->DelegateReference.SetSelfMember(FName(*Called));
		}

		return On.Pin;
	}

	if (Named.Has(TEXT("Variable"))) {
		if (const FString Called = MacroReading::NamedProperty(Named.GetObject(TEXT("Variable"))); !Called.IsEmpty()) {
			Node->DelegateReference.SetSelfMember(FName(*Called));
		}
	}

	return nullptr;
}

UEdGraphPin* FBytecodeGraph::FindAnswer(const FString& Name) const {
	if (Graph == nullptr || Name.IsEmpty()) return nullptr;

	for (UEdGraphNode* Node : Graph->Nodes) {
		if (!Node->IsA<UK2Node_FunctionResult>()) continue;

		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin == nullptr || Pin->Direction != EGPD_Input) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;

			if (Pin->PinName.ToString() == Name) return Pin;
		}
	}

	return nullptr;
}

bool FBytecodeGraph::HoldsLocals() const {
	return Graph != nullptr && Graph->GetSchema() != nullptr && Graph->GetSchema()->GetGraphType(Graph) == GT_Function;
}

bool FBytecodeGraph::IsMade(const FString& Name) const {
	if (Owned.Contains(Name)) return true;

	if (Running.Contains(Name)) return false;

	if (IsCompilerLocal(Name)) return true;

	if (KeepsTemporary(Name)) return false;

	if (Constants.Contains(Name)) return true;

	if (Owned.Contains(Name)) return true;

	if (Name.StartsWith(TEXT("Temp_")) && !HoldsLocals()) return true;

	return false;
}

bool FBytecodeGraph::HasLocal(const FString& Name) const {
	UBlueprint* Blueprint = Graph != nullptr ? Graph->GetTypedOuter<UBlueprint>() : nullptr;

	return Blueprint != nullptr && FBlueprintEditorUtils::FindLocalVariableGuidByName(Blueprint, Graph, *Name).IsValid();
}

bool FBytecodeGraph::KeepsTemporary(const FString& Name) const {
	if (HoldsLocals() || !Name.StartsWith(TEXT("Temp_"))) return false;

	if (Owned.Contains(Name)) return false;

	return Running.Contains(Name) || TestedScratch.Contains(Name) || HeldScratch.Contains(Name);
}

UEdGraphPin* FBytecodeGraph::Temporary(const FString& Name, const FUObjectJsonValueExport& Property) {
	if (const TWeakObjectPtr<UK2Node_TemporaryVariable>* Made = Temporaries.Find(Name); Made != nullptr && Made->IsValid()) {
		return (*Made)->GetVariablePin();
	}

	FEdGraphPinType Type;

	if (!TypeOfProperty(Property, Type, Container)) return nullptr;

	UK2Node_TemporaryVariable* Node = AddNode<UK2Node_TemporaryVariable>();

	Node->VariableType = Type;

	const FString Holding = FString(TEXT("Temp_")) + Type.PinCategory.ToString() + TEXT("_");

	if (Name.StartsWith(Holding)) {
		const FString Rest = Name.RightChop(Holding.Len());

		if (const int32 Ends = Rest.Find(TEXT("_Variable"), ESearchCase::CaseSensitive, ESearchDir::FromEnd); Ends > 0) {
			Node->NodeComment = Rest.Left(Ends);
		}
	}

	Node->AllocateDefaultPins();

	Temporaries.Add(Name, Node);

	return Node->GetVariablePin();
}

bool FBytecodeGraph::EnsureLocal(const FString& Name, const FUObjectJsonValueExport& Property) {
	UBlueprint* Blueprint = Graph != nullptr ? Graph->GetTypedOuter<UBlueprint>() : nullptr;

	if (Blueprint == nullptr) return false;

	if (FBlueprintEditorUtils::FindLocalVariableGuidByName(Blueprint, Graph, *Name).IsValid()) return true;

	FEdGraphPinType Type;

	if (!TypeOfProperty(Property, Type, Container)) return false;

	return FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, *Name, Type);
}

void FBytecodeGraph::PointAtLocal(UK2Node_Variable* Node, const FString& Name) {
	UBlueprint* Blueprint = Graph->GetTypedOuter<UBlueprint>();

	Node->VariableReference.SetLocalMember(*Name, Graph->GetName(), FBlueprintEditorUtils::FindLocalVariableGuidByName(Blueprint, Graph, *Name));
}

FBytecodeGraph::FValue FBytecodeGraph::ReadVariable(const FUObjectJsonValueExport& Expression) {
	FValue Value;

	const FUObjectJsonValueExport Variable = Expression.GetObject(TEXT("Variable"));

	const FString Name = MacroReading::NamedProperty(Variable);

	if (Name.IsEmpty()) return Value;

	const bool bReadsMember = MacroReading::TokenOf(Expression) == TEXT("EX_InstanceVariable");

	if (bReadsMember) {
		if (const TPair<TWeakObjectPtr<UK2Node>, FName>* Track = Tracks.Find(Name)) {
			if (Track->Key.IsValid()) {
				if (UEdGraphPin* Pin = Track->Key->FindPin(Track->Value, EGPD_Output)) {
					Value.Pin = Pin;

					return Value;
				}
			}
		}
	}

	if (bReadsMember && (Handouts.Contains(Name) || Locals.Contains(Name) || HasLocal(Name))) {
		UK2Node_VariableGet* Member = AddNode<UK2Node_VariableGet>();

		Member->VariableReference.SetSelfMember(*Name);
		Member->AllocateDefaultPins();

		Value.Pin = Member->FindPin(*Name, EGPD_Output);

		return Value;
	}

	if (Handouts.Contains(Name) && !bReadsMember) {
		UK2Node_VariableGet* Reading = AddNode<UK2Node_VariableGet>();

		PointAtLocal(Reading, Name);

		Reading->AllocateDefaultPins();

		if (UEdGraphPin* Held = Reading->FindPin(*Name, EGPD_Output)) {
			Value.Pin = Held;

			return Value;
		}

		Graph->RemoveNode(Reading);
	}

	if (KeepsTemporary(Name)) {
		Value.Pin = Temporary(Name, Variable.Has(TEXT("Property")) ? Variable.GetObject(TEXT("Property")) : Variable);

		return Value;
	}

	if (UEdGraphPin** Known = Locals.Find(Name)) {
		Value.Pin = *Known;

		return Value;
	}

	if (const FString* Carries = Carried.Find(Name)) {
		Value.Literal = *Carries;

		return Value;
	}

	if (IsMade(Name)) {
		Value.Wanted = Name;

		return Value;
	}

	UK2Node_VariableGet* Node = AddNode<UK2Node_VariableGet>();

	if (HasLocal(Name) && !bReadsMember) {
		PointAtLocal(Node, Name);
	} else {
		Node->VariableReference.SetSelfMember(*Name);
	}

	Node->AllocateDefaultPins();

	Value.Pin = Node->FindPin(*Name, EGPD_Output);

	return Value;
}

const UClass* FBytecodeGraph::DeclaredClassOf(const FUObjectJsonValueExport& Expression) {
	const FUObjectJsonValueExport Variable = Expression.GetObject(TEXT("Variable"));
	const FUObjectJsonValueExport Property = Variable.GetObject(TEXT("Property"));

	if (!Property.Has(TEXT("PropertyClass"))) return nullptr;

	const FUObjectJsonValueExport HeldClass = Property.GetObject(TEXT("PropertyClass"));

	FString Owner, Member;
	SplitReference(HeldClass, Owner, Member);

	const FString Called = Member.IsEmpty() ? Owner : Member;

	if (Called.IsEmpty()) return nullptr;

	const UClass* Of = FindClassByType(Called);

	if (Of == nullptr) {
		BringInClass(HeldClass);

		Of = FindClassByType(Called);
	}

	return Of;
}

const UClass* FBytecodeGraph::ClassNamedBy(const FUObjectJsonValueExport& Expression) {
	if (MacroReading::TokenOf(Expression) != TEXT("EX_ObjectConst")) return nullptr;

	const FUObjectJsonValueExport Named = Expression.GetObject(TEXT("Value"));

	FString Owner, Member;
	SplitReference(Named, Owner, Member);

	const FString Called = Member.IsEmpty() ? Owner : Member;

	if (!Called.StartsWith(DEFAULT_OBJECT_PREFIX)) return nullptr;

	return FindClassByType(Called.RightChop(FCString::Strlen(DEFAULT_OBJECT_PREFIX)));
}

void FBytecodeGraph::BringInClass(const FUObjectJsonValueExport& Named) {
	FString Where = Named.Has(TEXT("ObjectPath")) ? Named.GetString(TEXT("ObjectPath")) : FString();

	if (Where.IsEmpty() || Where.StartsWith(TEXT("/Script"))) return;

	FString Owner, Member;
	SplitReference(Named, Owner, Member);

	FString Kind = Member.IsEmpty() ? Owner : Member;

	if (Kind.StartsWith(DEFAULT_OBJECT_PREFIX)) {
		Kind = Kind.RightChop(FCString::Strlen(DEFAULT_OBJECT_PREFIX));
	}

	if (Kind.IsEmpty() || FindClassByType(Kind) != nullptr) return;

	Where = ToEditorPackagePath(Where);

	if (int32 Sits; Where.FindLastChar(TEXT('.'), Sits)) {
		const FString After = Where.RightChop(Sits + 1);

		if (!After.IsEmpty() && After.IsNumeric()) {
			LeftInline(Where, Sits);
		}
	}

	if (Where.IsEmpty()) return;

	FString Leaf = Where;

	if (int32 Slash; Where.FindLastChar(TEXT('/'), Slash)) {
		Leaf = Where.RightChop(Slash + 1);
	}

	if (Leaf.IsEmpty()) return;

	const FString Whole = Where + TEXT(".") + Leaf;

	if (FPackageName::DoesPackageExist(Where)) {
		LoadObject<UObject>(nullptr, *Whole);

		if (FindClassByType(Kind) != nullptr) {
			UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\" was already in the project, and was loaded"), *Whole);

			return;
		}
	}

	static TSet<FString> Fetching;

	if (Fetching.Contains(Where)) return;

	Fetching.Add(Where);

	TObjectPtr<UObject> Brought = nullptr;
	bool bBrought = false;

	FAssetUtilities::ConstructAsset<UObject>(Where, Whole, TEXT("BlueprintGeneratedClass"), Brought, bBrought);

	Fetching.Remove(Where);

	UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\" is called on and the project hasn't got it, so it was asked for: %s"),
		*Whole, FindClassByType(Kind) != nullptr ? TEXT("here now") : TEXT("still not here"));
}

FString FBytecodeGraph::Canonical(const FUObjectJsonValueExport& Expression) {
	if (!Expression.JsonObject.IsValid()) return FString();

	TArray<FString> Keys;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Expression.JsonObject->Values) {
		Keys.Add(Field.Key);
	}

	Keys.Sort();

	FString Spelled = TEXT("{");

	for (const FString& Key : Keys) {
		const TSharedPtr<FJsonValue> Value = Expression.JsonObject->Values[StringToJsonKey(Key)];

		Spelled += Key + TEXT(":");

		if (!Value.IsValid()) continue;

		if (Value->Type == EJson::Object) {
			Spelled += Canonical(FUObjectJsonValueExport(Value->AsObject()));
		} else if (Value->Type == EJson::Array) {
			Spelled += TEXT("[");

			for (const TSharedPtr<FJsonValue>& Held : Value->AsArray()) {
				if (Held.IsValid() && Held->Type == EJson::Object) {
					Spelled += Canonical(FUObjectJsonValueExport(Held->AsObject()));
				} else if (Held.IsValid()) {
					Spelled += Held->AsString();
				}

				Spelled += TEXT(",");
			}

			Spelled += TEXT("]");
		} else {
			Spelled += Value->AsString();
		}

		Spelled += TEXT(";");
	}

	return Spelled + TEXT("}");
}

FString FBytecodeGraph::Stamp(const FUObjectJsonValueExport& Expression) const {
	TArray<FString> Saw;

	Gather(Expression, Saw);

	if (Saw.Num() == 0) return FString();

	Saw.Sort();

	return TEXT("|as|") + FString::Join(Saw, TEXT(","));
}

void FBytecodeGraph::Gather(const FUObjectJsonValueExport& Expression, TArray<FString>& Saw) const {
	if (!Expression.JsonObject.IsValid()) return;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Expression.JsonObject->Values) {
		const TSharedPtr<FJsonValue> Value = Field.Value;

		if (!Value.IsValid()) continue;

		if (Value->Type == EJson::Object) {
			const FUObjectJsonValueExport Held(Value->AsObject());

			if (Field.Key == TEXT("Variable")) {
				const FString Name = MacroReading::NamedProperty(Held);

				if (!Name.IsEmpty() && Writes.FindRef(Name) > 1) {
					Saw.AddUnique(FString::Printf(TEXT("%s=%d"), *Name, Version.FindRef(Name)));
				}
			}

			Gather(Held, Saw);
		} else if (Value->Type == EJson::Array) {
			for (const TSharedPtr<FJsonValue>& Each : Value->AsArray()) {
				if (Each.IsValid() && Each->Type == EJson::Object) Gather(FUObjectJsonValueExport(Each->AsObject()), Saw);
			}
		}
	}
}

FBytecodeGraph::FValue FBytecodeGraph::Read(const FUObjectJsonValueExport& Expression) {
	const FString Same = bTidy ? Canonical(Expression) + Stamp(Expression) : FString();

	if (!Same.IsEmpty()) {
		if (const FShared* Known = Reused.Find(Same)) {
			if (Placing - Known->Made <= SharingReach) {
				FValue Shared;
				Shared.Pin = Known->Pin;

				return Shared;
			}
		}
	}

	const FValue Value = ReadExpression(Expression);

	Remember(Same, Value);

	return Value;
}

FBytecodeGraph::FValue FBytecodeGraph::ReadExpression(const FUObjectJsonValueExport& Expression) {
	FValue Value;

	if (!Expression.JsonObject.IsValid() || !Expression.Has(TEXT("Token"))) return Value;

	const FString Token = Expression.GetString(TEXT("Token"));

	if (Token == TEXT("EX_LocalVariable") || Token == TEXT("EX_InstanceVariable") || Token == TEXT("EX_LocalOutVariable") || Token == TEXT("EX_DefaultVariable") || Token == TEXT("EX_ClassSparseDataVariable")) {
		return ReadVariable(Expression);
	}

	if (Token == TEXT("EX_IntZero")) {
		Value.Literal = TEXT("0");

		return Value;
	}

	if (Token == TEXT("EX_IntOne")) {
		Value.Literal = TEXT("1");

		return Value;
	}

	if (Token == TEXT("EX_IntConst") || Token == TEXT("EX_ByteConst") || Token == TEXT("EX_IntConstByte") || Token == TEXT("EX_BitFieldConst") || Token == TEXT("EX_FloatConst") || Token == TEXT("EX_DoubleConst") || Token == TEXT("EX_Int64Const") || Token == TEXT("EX_UInt64Const")) {
		Value.Literal = LexToString(Expression.GetNumber(TEXT("Value")));

		return Value;
	}

	if (Token == TEXT("EX_True")) {
		Value.Literal = TEXT("true");

		return Value;
	}

	if (Token == TEXT("EX_False")) {
		Value.Literal = TEXT("false");

		return Value;
	}

	if (Token == TEXT("EX_NameConst") || Token == TEXT("EX_StringConst") || Token == TEXT("EX_UnicodeStringConst") || Token == TEXT("EX_InstanceDelegate") || Token == TEXT("EX_PropertyConst")) {
		Value.Literal = Expression.GetString(TEXT("Value"));

		return Value;
	}

	if (Token == TEXT("EX_TextConst")) {
		const FUObjectJsonValueExport Held = Expression.GetObject(TEXT("Value"));

		if (!Held.Has(TEXT("KeyString"))) {
			Value.Literal = TEXT("INVTEXT(\"\")");

			return Value;
		}

		const auto Spelled = [&Held](const TCHAR* Field) {
			const FUObjectJsonValueExport Inner = Held.GetObject(Field);

			FString Said = Inner.Has(TEXT("Value")) ? Inner.GetString(TEXT("Value")) : FString();

			return Said.ReplaceCharWithEscapedChar();
		};

		Value.Literal = FString::Printf(TEXT("NSLOCTEXT(\"%s\", \"%s\", \"%s\")"),
			*Spelled(TEXT("Namespace")), *Spelled(TEXT("KeyString")), *Spelled(TEXT("SourceString")));

		return Value;
	}

	if (Token == TEXT("EX_NoObject") || Token == TEXT("EX_NoInterface")) {
		return Value;
	}

	if (Token == TEXT("EX_StructConst")) {
		Value.Literal = ReadStructConst(Expression);

		return Value;
	}

	if (Token == TEXT("EX_Self")) {
		UK2Node_Self* Node = AddNode<UK2Node_Self>();

		Node->AllocateDefaultPins();

		Value.Pin = Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Output);

		return Value;
	}

	if (Token == TEXT("EX_DynamicCast")) {
		FString Owner, Member;
		SplitReference(Expression.GetObject(TEXT("InterfaceClass")), Owner, Member);

		UClass* To = const_cast<UClass*>(FindClassByType(Member.IsEmpty() ? Owner : Member));

		if (To == nullptr) {
			BringInClass(Expression.GetObject(TEXT("InterfaceClass")));

			To = const_cast<UClass*>(FindClassByType(Member.IsEmpty() ? Owner : Member));
		}

		if (To == nullptr) {
			Unhandled.AddUnique(FString::Printf(TEXT("cast to \"%s\", which this build does not carry"), *Owner));

			return Value;
		}

		UK2Node_DynamicCast* Node = AddNode<UK2Node_DynamicCast>();

		const bool bPure = !Writing.IsEmpty() && PureCasts.Contains(Writing);

		Node->TargetType = To;
		Node->SetPurity(bPure);

		Node->AllocateDefaultPins();

		if (!bPure) EnterNode(Node);

		Give(Read(Expression.GetObject(TEXT("Target"))), Node->GetCastSourcePin());

		if (!bPure) Flow = Node->GetValidCastPin();

		Value.Pin = Node->GetCastResultPin();

		return Value;
	}

	if (Token == TEXT("EX_Cast")) {
		const FValue Held = Read(Expression.GetObject(TEXT("Target")));

		if (Held.Pin != nullptr) {
			if (UK2Node_DynamicCast* Casting = ::Cast<UK2Node_DynamicCast>(Held.Pin->GetOwningNode());
				Casting != nullptr && Held.Pin == Casting->GetCastResultPin()) {
				if (UEdGraphPin* Worked = Casting->GetBoolSuccessPin()) {
					FValue Says;

					Says.Pin = Worked;

					return Says;
				}
			}
		}

		return Held;
	}

	if (Token == TEXT("EX_ObjectConst")) {
		const FUObjectJsonValueExport Named = Expression.GetObject(TEXT("Value"));

		FString Owner, Member;
		SplitReference(Named, Owner, Member);

		const FString Called = Member.IsEmpty() ? Owner : Member;

		if (Called.StartsWith(DEFAULT_OBJECT_PREFIX)) {
			BringInClass(Named);

			return Value;
		}

		FString Where = Named.Has(TEXT("ObjectPath")) ? ToEditorPackagePath(Named.GetString(TEXT("ObjectPath"))) : FString();

		if (int32 Sits; Where.FindLastChar(TEXT('.'), Sits)) {
			const FString After = Where.RightChop(Sits + 1);

			if (!After.IsEmpty() && After.IsNumeric()) {
				LeftInline(Where, Sits);
			}
		}

		Value.Literal = Where.IsEmpty() ? Called : Where + TEXT(".") + Called;

		if (!Value.Literal.IsEmpty() && LoadObjectByPath<UObject>(Value.Literal) == nullptr) {
			FString Kind;

			if (Named.Has(TEXT("ObjectName"))) {
				Named.GetString(TEXT("ObjectName")).Split(TEXT("'"), &Kind, nullptr);
			}

			FString Package = Value.Literal;

			if (int32 Sits; Package.FindLastChar(TEXT('.'), Sits)) {
				LeftInline(Package, Sits);
			}

			if (!Kind.IsEmpty() && !Package.IsEmpty()) {
				TObjectPtr<UObject> Brought = nullptr;
				bool bBrought = false;

				FAssetUtilities::ConstructAsset<UObject>(Package, Value.Literal, Kind, Brought, bBrought);

				if (Brought == nullptr) {
					UE_LOG(LogReflectionBytecode, Warning, TEXT("\"%s\" is a %s the project hasn't got and the cloud would not give"), *Value.Literal, *Kind);
				}
			}
		}

		return Value;
	}

	if (Token == TEXT("EX_ArrayConst")) {
		const TArray<FUObjectJsonValueExport> Values = Expression.Has(TEXT("Values"))
			? Expression.GetArray(TEXT("Values"))
			: TArray<FUObjectJsonValueExport>();

		UK2Node_MakeArray* Node = AddNode<UK2Node_MakeArray>();

		Node->NumInputs = Values.Num();
		Node->AllocateDefaultPins();

		const FUObjectJsonValueExport Stored = Expression.GetObject(TEXT("InnerProperty"));

		FEdGraphPinType Element;
		bool bKnown = false;

		if (const FUObjectJsonValueExport Held = Stored.GetObject(TEXT("Property")); Held.Has(TEXT("Inner"))) {
			bKnown = TypeOfProperty(DescribedBy(Held, TEXT("Inner"), Container), Element, Container);
		}

		if (!bKnown && Stored.Has(TEXT("ResolvedOwner"))) {
			FString Owner, Member;
			SplitReference(Stored.GetObject(TEXT("ResolvedOwner")), Owner, Member);

			const FString Called = Member.IsEmpty() ? Owner : Member;

			const UStruct* On = Called.IsEmpty() ? nullptr : static_cast<const UStruct*>(FindStructByType(Called));

			if (On == nullptr && !Called.IsEmpty()) On = FindClassByType(Called);

			if (On != nullptr) {
				if (const FArrayProperty* Holds = FindFProperty<FArrayProperty>(On, *MacroReading::NamedProperty(Stored))) {
					bKnown = GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Holds->Inner, Element);
				}
			}
		}

		if (bKnown) {
			for (UEdGraphPin* Pin : Node->Pins) {
				if (Pin != nullptr && Pin->Direction == EGPD_Input) Pin->PinType = Element;
			}

			if (UEdGraphPin* Out = Node->GetOutputPin()) {
				FEdGraphPinType Whole = Element;
				Whole.ContainerType = EPinContainerType::Array;

				Out->PinType = Whole;
			}
		}

		TArray<UEdGraphPin*> Into;

		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin != nullptr && Pin->Direction == EGPD_Input) Into.Add(Pin);
		}

		for (int32 At = 0; At < Values.Num() && Into.IsValidIndex(At); ++At) {
			Give(Read(Values[At]), Into[At]);
		}

		Value.Pin = Node->GetOutputPin();

		return Value;
	}

	if (Token == TEXT("EX_SetArray")) {
		const TArray<FUObjectJsonValueExport> Elements = Expression.Has(TEXT("Elements")) ? Expression.GetArray(TEXT("Elements")) : TArray<FUObjectJsonValueExport>();

		UK2Node_MakeArray* Node = AddNode<UK2Node_MakeArray>();

		Node->NumInputs = Elements.Num();
		Node->AllocateDefaultPins();

		FUObjectJsonValueExport Stored = Expression.GetObject(TEXT("AssigningProperty")).GetObject(TEXT("Variable"));

		if (!Stored.Has(TEXT("Inner")) && Stored.Has(TEXT("Property"))) Stored = Stored.GetObject(TEXT("Property"));

		if (Stored.Has(TEXT("Inner"))) {
			if (FEdGraphPinType Element; TypeOfProperty(DescribedBy(Stored, TEXT("Inner"), Container), Element, Container)) {
				for (UEdGraphPin* Pin : Node->Pins) {
					if (Pin != nullptr && Pin->Direction == EGPD_Input) Pin->PinType = Element;
				}

				if (UEdGraphPin* Out = Node->GetOutputPin()) {
					FEdGraphPinType Whole = Element;
					Whole.ContainerType = EPinContainerType::Array;

					Out->PinType = Whole;
				}
			}
		}

		TArray<UEdGraphPin*> Into;

		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin != nullptr && Pin->Direction == EGPD_Input) Into.Add(Pin);
		}

		for (int32 Which = 0; Which < Elements.Num(); ++Which) {
			if (!Into.IsValidIndex(Which)) break;

			const FValue Held = Read(Elements[Which]);

			if (Held.Pin != nullptr) Connect(Held.Pin, Into[Which]);
			else if (!Held.Literal.IsEmpty()) ApplyLiteral(Into[Which], Held.Literal);
		}

		if (Elements.Num() > 0) {
			Node->PostReconstructNode();
		}

		Placed++;

		Value.Pin = Node->GetOutputPin();

		return Value;
	}

	if (Token == TEXT("EX_SetMap")) {
		const TArray<FUObjectJsonValueExport> Values = Expression.Has(TEXT("Values")) ? Expression.GetArray(TEXT("Values")) : TArray<FUObjectJsonValueExport>();

		UK2Node_MakeMap* Node = AddNode<UK2Node_MakeMap>();

		Node->NumInputs = Values.Num() / 2;
		Node->AllocateDefaultPins();

		FUObjectJsonValueExport Stored = Expression.GetObject(TEXT("MapProperty")).GetObject(TEXT("Variable"));

		if (!Stored.Has(TEXT("KeyProp")) && Stored.Has(TEXT("Property"))) Stored = Stored.GetObject(TEXT("Property"));

		TArray<UEdGraphPin*> Keys;
		TArray<UEdGraphPin*> Mapped;
		Node->GetKeyAndValuePins(Keys, Mapped);

		if (FEdGraphPinType Whole; Stored.Has(TEXT("KeyProp")) && TypeOfProperty(Stored, Whole, Container)) {
			FEdGraphPinType Key = Whole;
			Key.ContainerType = EPinContainerType::None;
			Key.PinValueType = FEdGraphTerminalType();

			const FEdGraphPinType Held = FEdGraphPinType::GetPinTypeForTerminalType(Whole.PinValueType);

			for (UEdGraphPin* Pin : Keys) Pin->PinType = Key;
			for (UEdGraphPin* Pin : Mapped) Pin->PinType = Held;

			if (UEdGraphPin* Out = Node->GetOutputPin()) Out->PinType = Whole;
		}

		for (int32 Pair = 0; Pair < Values.Num() / 2; ++Pair) {
			if (Keys.IsValidIndex(Pair)) Give(Read(Values[Pair * 2]), Keys[Pair]);
			if (Mapped.IsValidIndex(Pair)) Give(Read(Values[Pair * 2 + 1]), Mapped[Pair]);
		}

		Placed++;

		Value.Pin = Node->GetOutputPin();

		return Value;
	}

	if (Token == TEXT("EX_ArrayGetByRef")) {
		UK2Node_GetArrayItem* Node = AddNode<UK2Node_GetArrayItem>();

		Node->AllocateDefaultPins();

		Give(Read(Expression.GetObject(TEXT("ArrayVariable"))), Node->GetTargetArrayPin());
		Give(Read(Expression.GetObject(TEXT("ArrayIndex"))), Node->GetIndexPin());

		Placed++;

		Value.Pin = Node->GetResultPin();

		return Value;
	}

	if (Token == TEXT("EX_SwitchValue")) {
		FString Spelled;

		if (Expression.JsonObject.IsValid()) {
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Spelled);

			FJsonSerializer::Serialize(Expression.JsonObject.ToSharedRef(), Writer);

			for (const TCHAR* Jump : { TEXT("\"EndGotoOffset\":"), TEXT("\"NextOffset\":") }) {
				for (int32 At = Spelled.Find(Jump); At != INDEX_NONE; At = Spelled.Find(Jump, ESearchCase::CaseSensitive, ESearchDir::FromStart, At)) {
					int32 End = At + FCString::Strlen(Jump);

					while (End < Spelled.Len() && (FChar::IsDigit(Spelled[End]) || Spelled[End] == TEXT('-'))) ++End;
					if (End < Spelled.Len() && Spelled[End] == TEXT(',')) ++End;

					Spelled.RemoveAt(At, End - At);
				}
			}
		}

		if (UEdGraphNode* const* Before = Picked.Find(Spelled); Before != nullptr && Graph->Nodes.Contains(*Before)) {
			if (UK2Node_Select* Again = Cast<UK2Node_Select>(*Before)) {
				Value.Pin = Again->GetReturnValuePin();

				return Value;
			}
		}

		UK2Node_Select* Node = AddNode<UK2Node_Select>();

		Node->AllocateDefaultPins();

		if (UEdGraphPin* Index = Node->GetIndexPin()) {
			const FValue Looked = Read(Expression.GetObject(TEXT("IndexTerm")));

			if (Looked.Pin != nullptr && Looked.Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int && Expression.Has(TEXT("Cases"))) {
				const int32 Wanted = Expression.GetArray(TEXT("Cases")).Num();

				TArray<UEdGraphPin*> Grown;
				Node->GetOptionPins(Grown);

				while (Grown.Num() < Wanted && Node->CanAddPin()) {
					const int32 Had = Grown.Num();

					Node->AddInputPin();
					Node->GetOptionPins(Grown);

					if (Grown.Num() <= Had) break;
				}

				Index = Node->GetIndexPin();
			}

			if (Looked.Pin != nullptr && Index != nullptr) Connect(Looked.Pin, Index);
			else if (!Looked.Literal.IsEmpty()) ApplyLiteral(Index, Looked.Literal);
		}

		Node->ReconstructNode();

		if (const UEdGraphPin* Looks = Node->GetIndexPin();
			Looks == nullptr || (Looks->LinkedTo.Num() == 0 && Looks->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)) {
			Unhandled.AddUnique(FString::Printf(TEXT("a Select with nothing to pick by, whose index this build could not read")));

			Graph->RemoveNode(Node);

			return Value;
		}

		TArray<UEdGraphPin*> Options;
		Node->GetOptionPins(Options);

		if (const FUObjectJsonValueExport Untaken = Expression.Has(TEXT("DefaultTerm")) ? Expression.GetObject(TEXT("DefaultTerm")) : FUObjectJsonValueExport(); Untaken.Has(TEXT("Variable")) && Options.Num() > 0) {
			const FUObjectJsonValueExport Held = Untaken.GetObject(TEXT("Variable"));

			const TSharedPtr<FJsonObject> Describes = Held.Has(TEXT("Property"))
				? Held.GetObject(TEXT("Property")).JsonObject
				: Held.JsonObject;

			if (FEdGraphPinType Picking; FBlueprintVariables::GetPinType(Describes, Picking)) {
				Options[0]->PinType = Picking;

				Node->PinTypeChanged(Options[0]);

				Node->GetOptionPins(Options);
			}
		}

		const TArray<FUObjectJsonValueExport> Cases = Expression.Has(TEXT("Cases")) ? Expression.GetArray(TEXT("Cases")) : TArray<FUObjectJsonValueExport>();

		for (int32 Which = 0; Which < Cases.Num(); ++Which) {
			if (!Options.IsValidIndex(Which) || !Cases[Which].Has(TEXT("CaseTerm"))) continue;

			const FValue Taken = Read(Cases[Which].GetObject(TEXT("CaseTerm")));

			if (Taken.Pin != nullptr) Connect(Taken.Pin, Options[Which]);
			else if (!Taken.Literal.IsEmpty()) ApplyLiteral(Options[Which], Taken.Literal);
		}

		Placed++;

		Value.Pin = Node->GetReturnValuePin();

		if (!Spelled.IsEmpty()) Picked.Add(Spelled, Node);

		return Value;
	}

	if (Token == TEXT("EX_CallMath") || Token == TEXT("EX_FinalFunction") || Token == TEXT("EX_LocalFinalFunction") || Token == TEXT("EX_VirtualFunction") || Token == TEXT("EX_LocalVirtualFunction")) {
		if (UK2Node* Node = PlaceCall(Expression, nullptr)) {
			Value.Pin = HandsBack(Node);
		}

		return Value;
	}

	if (Token == TEXT("EX_Context") || Token == TEXT("EX_Context_FailSilent") || Token == TEXT("EX_ClassContext")) {
		const FUObjectJsonValueExport Upon = Expression.GetObject(TEXT("ObjectExpression"));

		const FValue Target = Read(Upon);
		const FUObjectJsonValueExport Inner = Expression.GetObject(TEXT("ContextExpression"));

		if (Inner.Has(TEXT("Token")) && Inner.GetString(TEXT("Token")) == TEXT("EX_DefaultVariable")) {
			const FUObjectJsonValueExport Held = Inner.GetObject(TEXT("Variable"));

			const FString Named = MacroReading::NamedProperty(Held);

			const FUObjectJsonValueExport Points = Upon.GetObject(TEXT("Value"));

			FString Owner, Member;
			SplitReference(Points, Owner, Member);

			FString Called = Member.IsEmpty() ? Owner : Member;

			if (Called.StartsWith(DEFAULT_OBJECT_PREFIX)) {
				Called = Called.RightChop(FCString::Strlen(DEFAULT_OBJECT_PREFIX));
			}

			UClass* Of = Called.IsEmpty() ? nullptr : const_cast<UClass*>(FindClassByType(Called));

			if (Of == nullptr && !Called.IsEmpty()) {
				BringInClass(Points);

				Of = const_cast<UClass*>(FindClassByType(Called));
			}

			if (!Named.IsEmpty() && Of != nullptr) {
				UK2Node_GetClassDefaults* Defaults = AddNode<UK2Node_GetClassDefaults>();

				Defaults->AllocateDefaultPins();

				for (UEdGraphPin* Pin : Defaults->Pins) {
					if (Pin == nullptr || Pin->Direction != EGPD_Input) continue;
					if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Class) continue;

					Pin->DefaultObject = Of;

					Defaults->PinDefaultValueChanged(Pin);

					break;
				}

				if (UEdGraphPin* Reads = Defaults->FindPin(*Named, EGPD_Output)) {
					Value.Pin = Reads;

					return Value;
				}

				Graph->RemoveNode(Defaults);
			}

			if (!Named.IsEmpty() && Of == nullptr && Target.Pin != nullptr && Target.Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class) {
				UK2Node_GetClassDefaults* Defaults = AddNode<UK2Node_GetClassDefaults>();

				Defaults->AllocateDefaultPins();

				if (UEdGraphPin* ClassPin = Defaults->FindClassPin()) {
					Target.Pin->MakeLinkTo(ClassPin);

					Defaults->PinConnectionListChanged(ClassPin);
				}

				if (UEdGraphPin* Reads = Defaults->FindPin(*Named, EGPD_Output)) {
					Value.Pin = Reads;

					return Value;
				}

				Graph->RemoveNode(Defaults);
			}
		}

		if (Inner.Has(TEXT("Token")) && Inner.GetString(TEXT("Token")).Contains(TEXT("Function"))) {
			const bool bTargetLater = Target.Pin == nullptr && !Target.Wanted.IsEmpty();

			const UClass* Against = bTargetLater ? DeclaredClassOf(Upon) : nullptr;

			if (UK2Node* Node = PlaceCall(Inner, Target.Pin, Against != nullptr ? Against : ClassNamedBy(Upon))) {
				if (bTargetLater) {
					if (UEdGraphPin* Self = Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input)) {
						Later.Add(TPair<UEdGraphPin*, FString>(Self, Target.Wanted));
					}
				}

				Value.Pin = HandsBack(Node);
			}

			return Value;
		}

		if (Inner.Has(TEXT("Token")) && Inner.GetString(TEXT("Token")).EndsWith(TEXT("Variable"))) {
			const FUObjectJsonValueExport Held = Inner.GetObject(TEXT("Variable"));

			FString Name = MacroReading::NamedProperty(Held);
			FString Owner;

			if (Held.Has(TEXT("ResolvedOwner"))) {
				FString Member;
				SplitReference(Held.GetObject(TEXT("ResolvedOwner")), Owner, Member);
			} else if (Held.Has(TEXT("ObjectName"))) {
				FString Member;
				SplitReference(Held, Owner, Member);
			} else if (Held.Has(TEXT("Owner"))) {
				FString Member;
				SplitReference(Held.GetObject(TEXT("Owner")), Owner, Member);
			}

			UClass* On = Owner.IsEmpty() ? nullptr : const_cast<UClass*>(FindClassByType(Owner));

			if (On == nullptr && !Owner.IsEmpty() && Held.Has(TEXT("Owner"))) {
				BringInClass(Held.GetObject(TEXT("Owner")));

				On = const_cast<UClass*>(FindClassByType(Owner));
			}

			if (!Name.IsEmpty() && On != nullptr && (Target.Pin != nullptr || !Target.Wanted.IsEmpty())) {
				UK2Node_VariableGet* Node = AddNode<UK2Node_VariableGet>();

				Node->VariableReference.SetExternalMember(*Name, On);
				Node->AllocateDefaultPins();

				Give(Target, Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input));

				Value.Pin = Node->FindPin(*Name, EGPD_Output);

				return Value;
			}
		}

		return Read(Inner);
	}

	if (Token == TEXT("EX_StructMemberContext")) {
		const FValue Held = Read(Expression.GetObject(TEXT("StructExpression")));

		const FUObjectJsonValueExport Property = Expression.GetObject(TEXT("Property"));

		FString Member;

		if (Property.Has(TEXT("Name"))) {
			Member = Property.GetString(TEXT("Name"));
		} else if (Property.JsonObject.IsValid()) {
			const TArray<TSharedPtr<FJsonValue>>* Path;

			if (Property.JsonObject->TryGetArrayField(TEXT("Path"), Path) && Path->Num() > 0) {
				Member = (*Path)[Path->Num() - 1]->AsString();
			}
		}

		FString Owner, Unused;

		if (Property.Has(TEXT("ResolvedOwner"))) {
			SplitReference(Property.GetObject(TEXT("ResolvedOwner")), Owner, Unused);
		}

		ReadPropertyReference(Property, Owner, Member);

		if (Owner.IsEmpty() && Property.Has(TEXT("Outer"))) {
			Owner = StripObjectOuter(Property.GetObject(TEXT("Outer")).GetString(TEXT("ObjectName")));
		}

		UScriptStruct* Struct = Owner.IsEmpty() ? nullptr : FindStructAnywhere(Owner);

		if (Held.Pin != nullptr && Struct != nullptr && Held.Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct) {
			if (UScriptStruct* Holds = Cast<UScriptStruct>(Held.Pin->PinType.PinSubCategoryObject.Get()); Holds != nullptr && Holds != Struct && Holds->IsChildOf(Struct)) {
				Struct = Holds;
			}
		}

		if (Held.Pin != nullptr && Struct != nullptr && !Member.IsEmpty()) {
			const FString SameBreak = bTidy
				? Canonical(Expression.GetObject(TEXT("StructExpression"))) + TEXT("|apart|") + Struct->GetName()
				: FString();

			UK2Node_BreakStruct* Node = nullptr;

			if (!SameBreak.IsEmpty()) {
				if (const FTakenApart* Known = Apart.Find(SameBreak)) {
					if (Placing - Known->Made <= SharingReach) {
						Node = Cast<UK2Node_BreakStruct>(Known->Node);
					}
				}
			}

			const bool bMade = Node == nullptr;

			if (bMade) {
				Node = AddNode<UK2Node_BreakStruct>();

				Node->StructType = Struct;
				Node->AllocateDefaultPins();

				Connect(Held.Pin, Node->FindPinChecked(Struct->GetFName(), EGPD_Input));
			}

			Value.Pin = Node->FindPin(*Member, EGPD_Output);

			if (Value.Pin != nullptr) {
				if (bMade && !SameBreak.IsEmpty()) {
					Apart.Add(SameBreak, FTakenApart{ Node, Placing });
				}

				return Value;
			}

			if (bMade) {
				Graph->RemoveNode(Node);
			}
		}

		if (Held.Pin != nullptr && !Member.IsEmpty()) {
			if (Held.Pin->SubPins.Num() == 0) {
				GetDefault<UEdGraphSchema_K2>()->SplitPin(Held.Pin, false);
			}

			for (UEdGraphPin* Piece : Held.Pin->SubPins) {
				if (Piece == nullptr) continue;

				if (!Piece->PinName.ToString().EndsWith(Member)) continue;

				Value.Pin = Piece;

				return Value;
			}
		}

		Unhandled.AddUnique(FString::Printf(TEXT("%s off %s"), *Member, *Owner));

		return Value;
	}

	if (Token == TEXT("EX_TransformConst")) {
		const FUObjectJsonValueExport Held = Expression.GetObject(TEXT("Value"));

		const FUObjectJsonValueExport Translation = Held.GetObject(TEXT("Translation"));
		const FUObjectJsonValueExport Rotation = Held.GetObject(TEXT("Rotation"));
		const FUObjectJsonValueExport Scale = Held.GetObject(TEXT("Scale3D"));

		const FQuat Turn(Rotation.GetNumber(TEXT("X")), Rotation.GetNumber(TEXT("Y")), Rotation.GetNumber(TEXT("Z")), Rotation.GetNumber(TEXT("W")));
		const FRotator Facing = Turn.Rotator();

		Value.Literal = FString::Printf(
			TEXT("%f,%f,%f|%f,%f,%f|%f,%f,%f"),
			Translation.GetNumber(TEXT("X")), Translation.GetNumber(TEXT("Y")), Translation.GetNumber(TEXT("Z")),
			Facing.Pitch, Facing.Yaw, Facing.Roll,
			Scale.GetNumber(TEXT("X")), Scale.GetNumber(TEXT("Y")), Scale.GetNumber(TEXT("Z"))
		);

		return Value;
	}

	if (Token == TEXT("EX_VectorConst") || Token == TEXT("EX_Vector3fConst")) {
		const FUObjectJsonValueExport Held = Expression.GetObject(TEXT("Value"));

		Value.Literal = FString::Printf(TEXT("%f,%f,%f"), Held.GetNumber(TEXT("X")), Held.GetNumber(TEXT("Y")), Held.GetNumber(TEXT("Z")));

		return Value;
	}

	if (Token == TEXT("EX_RotationConst")) {
		const FUObjectJsonValueExport Held = Expression.GetObject(TEXT("Value"));

		Value.Literal = FString::Printf(TEXT("%f,%f,%f"), Held.GetNumber(TEXT("Pitch")), Held.GetNumber(TEXT("Yaw")), Held.GetNumber(TEXT("Roll")));

		return Value;
	}

	if (Token == TEXT("EX_Skip") || Token == TEXT("EX_InterfaceContext") || Token == TEXT("EX_SoftObjectConst") || Token == TEXT("EX_FieldPathConst")) {
		return Read(Inner(Expression));
	}

	if (Token == TEXT("EX_MetaCast") || Token == TEXT("EX_ObjToInterfaceCast") || Token == TEXT("EX_CrossInterfaceCast") || Token == TEXT("EX_InterfaceToObjCast")) {
		FString Owner, Member;
		SplitReference(Names(Expression), Owner, Member);

		UClass* To = const_cast<UClass*>(FindClassByType(Member.IsEmpty() ? Owner : Member));

		if (To == nullptr) {
			BringInClass(Names(Expression));

			To = const_cast<UClass*>(FindClassByType(Member.IsEmpty() ? Owner : Member));
		}

		if (To == nullptr) {
			Unhandled.AddUnique(FString::Printf(TEXT("a cast to \"%s\", which this build does not carry"), *Owner));

			return Value;
		}

		UK2Node_DynamicCast* Node = Token == TEXT("EX_MetaCast")
			? static_cast<UK2Node_DynamicCast*>(AddNode<UK2Node_ClassDynamicCast>())
			: AddNode<UK2Node_DynamicCast>();

		Node->TargetType = To;

		const bool bPure = Writing.IsEmpty() || !ImpureCasts.Contains(Writing);

		Node->SetPurity(bPure);

		Node->AllocateDefaultPins();

		if (!bPure) EnterNode(Node);

		Give(Read(Inner(Expression)), Node->GetCastSourcePin());

		if (!bPure) Flow = Node->GetValidCastPin();

		Value.Pin = Node->GetCastResultPin();

		return Value;
	}

	if (Token == TEXT("EX_NothingInt32") || Token == TEXT("EX_Breakpoint") || Token == TEXT("EX_Tracepoint")
	 || Token == TEXT("EX_WireTracepoint") || Token == TEXT("EX_InstrumentationEvent") || Token == TEXT("EX_DeprecatedOp4A")
	 || Token == TEXT("EX_AutoRtfmTransact") || Token == TEXT("EX_AutoRtfmStopTransact") || Token == TEXT("EX_AutoRtfmAbortIfNot")
	 || Token == TEXT("EX_Assert")) {
		return Value;
	}

	Unhandled.AddUnique(Token);

	return Value;
}

UClass* FBytecodeGraph::EnsureComponentTemplate(const FString& Name) {
	UBlueprint* Blueprint = Graph != nullptr ? Graph->GetTypedOuter<UBlueprint>() : nullptr;

	if (Blueprint == nullptr || Name.IsEmpty() || Container == nullptr) return nullptr;

	FUObjectExport* Export = nullptr;

	for (FUObjectExport* Candidate : Container->Exports) {
		if (Candidate != nullptr && Candidate->IsJsonValid() && Candidate->GetName().ToString() == Name) {
			Export = Candidate;

			break;
		}
	}

	for (UActorComponent* Existing : Blueprint->ComponentTemplates) {
		if (Existing != nullptr && Existing->GetName() == Name) {
			if (Export != nullptr && ObjectSerializer != nullptr) {
				Export->Object = Existing;

				ObjectSerializer->DeserializeObjectProperties(Export->GetProperties(), Existing);
			}

			return Existing->GetClass();
		}
	}

	if (Export == nullptr) return nullptr;

	UClass* Class = Export->GetClass();

	if (Class == nullptr || !Class->IsChildOf(UActorComponent::StaticClass())) return nullptr;

	UObject* Keeps = Blueprint->GeneratedClass != nullptr ? static_cast<UObject*>(Blueprint->GeneratedClass) : nullptr;

	if (Keeps == nullptr) return nullptr;

	UActorComponent* Template = NewObject<UActorComponent>(Keeps, Class, *Name, RF_ArchetypeObject | RF_Public | RF_Transactional);

	if (Template == nullptr) return nullptr;

	Export->Object = Template;

	if (ObjectSerializer != nullptr) {
		ObjectSerializer->DeserializeObjectProperties(Export->GetProperties(), Template);
	}

	Blueprint->ComponentTemplates.Add(Template);

	UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\" is a %s the blueprint now keeps a copy of"), *Name, *Class->GetName());

	return Class;
}

void FBytecodeGraph::Remember(const FString& Same, const FValue& Value) {
	if (Same.IsEmpty() || Value.Pin == nullptr) return;

	const UEdGraphNode* MadeOwningNode = Value.Pin->GetOwningNode();

	if (MadeOwningNode == nullptr || MadeOwningNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input) != nullptr) return;

	Reused.Add(Same, FShared{ Value.Pin, Placing });
}

namespace {
	FName KindOfPropertyType(const FString& Type) {
		if (Type == TEXT("ArrayProperty")) return TEXT("Array");
		if (Type == TEXT("SetProperty")) return TEXT("Set");
		if (Type == TEXT("MapProperty")) return TEXT("Map");
		if (Type == TEXT("BoolProperty")) return TEXT("Bool");
		if (Type == TEXT("StructProperty")) return TEXT("Struct");
		if (Type == TEXT("NameProperty")) return TEXT("Name");
		if (Type == TEXT("StrProperty")) return TEXT("String");
		if (Type == TEXT("TextProperty")) return TEXT("Text");

		if (Type == TEXT("ObjectProperty") || Type == TEXT("ClassProperty") || Type == TEXT("InterfaceProperty") || Type == TEXT("SoftObjectProperty")
		 || Type == TEXT("SoftClassProperty") || Type == TEXT("WeakObjectProperty") || Type == TEXT("LazyObjectProperty")) {
			return TEXT("Object");
		}

		if (Type.EndsWith(TEXT("IntProperty")) || Type.StartsWith(TEXT("Int")) || Type.StartsWith(TEXT("UInt")) || Type == TEXT("ByteProperty")
		 || Type == TEXT("EnumProperty") || Type == TEXT("FloatProperty") || Type == TEXT("DoubleProperty")) {
			return TEXT("Number");
		}

		return NAME_None;
	}

	FName KindOfArgument(const FUObjectJsonValueExport& Value) {
		const FString Token = MacroReading::TokenOf(Value);

		if (Token.StartsWith(TEXT("EX_Int")) || Token == TEXT("EX_ByteConst") || Token == TEXT("EX_FloatConst") || Token == TEXT("EX_DoubleConst")
		 || Token == TEXT("EX_UInt64Const") || Token == TEXT("EX_Int64Const")) {
			return TEXT("Number");
		}

		if (Token == TEXT("EX_True") || Token == TEXT("EX_False")) return TEXT("Bool");
		if (Token == TEXT("EX_ArrayConst")) return TEXT("Array");
		if (Token == TEXT("EX_StructConst") || Token == TEXT("EX_VectorConst") || Token == TEXT("EX_RotationConst") || Token == TEXT("EX_TransformConst")) return TEXT("Struct");
		if (Token == TEXT("EX_NameConst")) return TEXT("Name");
		if (Token == TEXT("EX_StringConst") || Token == TEXT("EX_UnicodeStringConst")) return TEXT("String");
		if (Token == TEXT("EX_TextConst")) return TEXT("Text");
		if (Token == TEXT("EX_ObjectConst") || Token == TEXT("EX_NoObject") || Token == TEXT("EX_Self")) return TEXT("Object");

		if (Value.Has(TEXT("Variable"))) {
			const FUObjectJsonValueExport Held = Value.GetObject(TEXT("Variable"));
			const TSharedPtr<FJsonObject> Described = Held.Has(TEXT("Property")) ? Held.GetObject(TEXT("Property")).JsonObject : Held.JsonObject;

			FString Type;

			if (Described.IsValid() && Described->TryGetStringField(TEXT("Type"), Type)) return KindOfPropertyType(Type);
		}

		return NAME_None;
	}

	FName KindOfParameter(const FProperty* Parameter) {
		if (Parameter->IsA<FArrayProperty>()) return TEXT("Array");
		if (Parameter->IsA<FSetProperty>()) return TEXT("Set");
		if (Parameter->IsA<FMapProperty>()) return TEXT("Map");
		if (Parameter->IsA<FBoolProperty>()) return TEXT("Bool");
		if (Parameter->IsA<FStructProperty>()) return TEXT("Struct");
		if (Parameter->IsA<FNameProperty>()) return TEXT("Name");
		if (Parameter->IsA<FStrProperty>()) return TEXT("String");
		if (Parameter->IsA<FTextProperty>()) return TEXT("Text");
		if (Parameter->IsA<FObjectPropertyBase>() || Parameter->IsA<FInterfaceProperty>()) return TEXT("Object");
		if (Parameter->IsA<FNumericProperty>() || Parameter->IsA<FEnumProperty>()) return TEXT("Number");

		return NAME_None;
	}

	bool IsOptionalParameter(const UFunction* Function, const FProperty* Parameter) {
		const FString Name = Parameter->GetName();

		if (Function->HasMetaData(*(TEXT("CPP_Default_") + Name))) return true;

		TArray<FString> AutoCreated;
		Function->GetMetaData(TEXT("AutoCreateRefTerm")).ParseIntoArray(AutoCreated, TEXT(","), true);

		return AutoCreated.ContainsByPredicate([&Name](const FString& One) { return One.TrimStartAndEnd() == Name; });
	}
}

UK2Node* FBytecodeGraph::PlaceCall(const FUObjectJsonValueExport& Expression, UEdGraphPin* Target, const UClass* Against) {
	if (!Expression.Has(TEXT("Function"))) return nullptr;

	UFunction* Function = nullptr;

	if (Expression.JsonObject.IsValid() && Expression.JsonObject->HasTypedField<EJson::String>(TEXT("Function"))) {
		const FString Named = Expression.GetString(TEXT("Function"));

		const UClass* On = Target != nullptr ? Cast<UClass>(Target->PinType.PinSubCategoryObject.Get()) : Against;

		if (const UBlueprint* Own = On == nullptr && Graph != nullptr ? Graph->GetTypedOuter<UBlueprint>() : nullptr) {
			On = Own->SkeletonGeneratedClass != nullptr ? Own->SkeletonGeneratedClass : Own->GeneratedClass;

			if (On != nullptr && On->FindFunctionByName(*Named) == nullptr && Own->GeneratedClass != nullptr) {
				On = Own->GeneratedClass;
			}
		}

		Function = FindFunctionOn(On, Named);

		if (Function == nullptr && Target == nullptr && Graph != nullptr) {
			if (const UBlueprint* Own = Graph->GetTypedOuter<UBlueprint>()) {
				if (Own->SkeletonGeneratedClass != nullptr) Function = FindFunctionOn(Own->SkeletonGeneratedClass, Named);
				if (Function == nullptr && Own->GeneratedClass != nullptr) Function = FindFunctionOn(Own->GeneratedClass, Named);
				if (Function != nullptr) On = Function->GetOwnerClass();
			}
		}

		if (Function != nullptr && On != nullptr && Function->GetOwnerClass() != nullptr && Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Native)) {
			if (UFunction* Own = On->FindFunctionByName(*(Named + FBlueprintGraphs::ClashSuffix))) {
				Function = Own;
			}
		}
	} else {
		Function = ResolveFunction(Expression.GetObject(TEXT("Function")));
	}

	if (Function == nullptr) {
		const FString Names = Expression.JsonObject.IsValid() && Expression.JsonObject->HasTypedField<EJson::String>(TEXT("Function"))
			? Expression.GetString(TEXT("Function"))
			: Expression.GetObject(TEXT("Function")).GetString(TEXT("ObjectName"));

		Unhandled.AddUnique(FString::Printf(TEXT("%s calls %s, which this build has not got"),
			*Expression.GetString(TEXT("Token")),
			Names.IsEmpty() ? TEXT("a function it does not name") : *Names));

		return nullptr;
	}

	if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure)) {
		const TArray<FUObjectJsonValueExport> Given = Expression.Has(TEXT("Parameters")) ? Expression.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

		bool bWorksOutAValue = false;
		bool bAllHandedOut = true;

		int32 Which = 0;

		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It) {
			if (It->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
			if (!Given.IsValidIndex(Which)) break;

			const FUObjectJsonValueExport& Argument = Given[Which++];

			if (!It->HasAnyPropertyFlags(CPF_OutParm) || It->HasAnyPropertyFlags(CPF_ConstParm)) continue;

			bWorksOutAValue = true;

			const FString Into = Argument.Has(TEXT("Variable")) ? MacroReading::NamedProperty(Argument.GetObject(TEXT("Variable"))) : FString();

			if (Into.IsEmpty() || !Owned.Contains(Into) || !Locals.Contains(Into)) {
				bAllHandedOut = false;

				UE_LOG(LogReflectionBytecode, Display, TEXT("%s works out \"%s\", which no macro answers for (owned %d, known %d)"),
					*Function->GetName(), *Into, Owned.Contains(Into) ? 1 : 0, Locals.Contains(Into) ? 1 : 0);
			}
		}

		if (bWorksOutAValue && bAllHandedOut) {
			UE_LOG(LogReflectionBytecode, Display, TEXT("%s was passed over, since a macro already hands out everything it works out"), *Function->GetName());

			return nullptr;
		}
	}

	if (Function->GetFName() == TEXT("Format") && Function->GetOwnerClass() == UKismetTextLibrary::StaticClass()) {
		if (UK2Node* Filled = FormatText(Expression)) return Filled;
	}

	if (Function->GetFName() == TEXT("GetDataTableRowFromName") && Function->GetOwnerClass() == UDataTableFunctionLibrary::StaticClass()) {
		if (UK2Node* Reading = GetDataTableRow(Expression)) return Reading;
	}

	UK2Node_CallFunction* Node = nullptr;

	if (Function->GetFName() == TEXT("AddComponent")) {
		const TArray<FUObjectJsonValueExport> Arguments = Expression.Has(TEXT("Parameters")) ? Expression.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

		UClass* Kind = Arguments.Num() > 0 && Arguments[0].Has(TEXT("Value")) ? EnsureComponentTemplate(Arguments[0].GetString(TEXT("Value"))) : nullptr;

		if (Kind != nullptr) {
			UK2Node_AddComponent* Adding = AddNode<UK2Node_AddComponent>();

			Adding->TemplateType = Kind;

			Node = Adding;
		}
	}

	if (Node == nullptr && Target == nullptr) {
		const UBlueprint* Own = Graph != nullptr ? Graph->GetTypedOuter<UBlueprint>() : nullptr;

		const UClass* Mine = Own != nullptr ? Own->GeneratedClass : nullptr;
		const UClass* Theirs = Function->GetOwnerClass();

		if (Mine != nullptr && Theirs != nullptr && Mine != Theirs && Mine->IsChildOf(Theirs)) {
			const UFunction* Ours = Mine->FindFunctionByName(Function->GetFName());

			if (Ours == nullptr && Own->SkeletonGeneratedClass != nullptr) {
				Ours = Own->SkeletonGeneratedClass->FindFunctionByName(Function->GetFName());
			}

			const FString CallToken = Expression.Has(TEXT("Token")) ? Expression.GetString(TEXT("Token")) : FString();
			const bool bFinalCallToEvent = Function->HasAnyFunctionFlags(FUNC_BlueprintEvent)
				&& (CallToken == TEXT("EX_FinalFunction") || CallToken == TEXT("EX_LocalFinalFunction"));

			if ((Ours != nullptr && Ours != Function) || bFinalCallToEvent) {
				Node = AddNode<UK2Node_CallParentFunction>();
			}

		}
	}

	if (Node == nullptr) {
		Node = AddNodeOfClass<UK2Node_CallFunction>(NodeClassFor(Function));
	}

	Node->SetFromFunction(Function);
	Node->AllocateDefaultPins();

	if (UK2Node_AddComponent* Adding = Cast<UK2Node_AddComponent>(Node)) {
		const TArray<FUObjectJsonValueExport> Arguments = Expression.Has(TEXT("Parameters")) ? Expression.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

		if (Arguments.Num() > 0 && Arguments[0].Has(TEXT("Value"))) {
			for (UEdGraphPin* Pin : Adding->Pins) {
				if (Pin != nullptr && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name) {
					Pin->DefaultValue = Arguments[0].GetString(TEXT("Value"));

					break;
				}
			}
		}

		Adding->ReconstructNode();
	}

	const FString Name = FString::Printf(TEXT("CallFunc_%s"), *Function->GetName());

	Node->Rename(*MakeUniqueObjectName(Graph, Node->GetClass(), *Name).ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);

	if (Target != nullptr) {
		if (UEdGraphPin* Self = Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input)) {
			Connect(Target, Self);
		}
	}

	const TArray<FUObjectJsonValueExport> Parameters = Expression.Has(TEXT("Parameters")) ? Expression.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

	bool bWaits = false;

	int32 Argument = 0;

	TArray<FProperty*> Takes;

	for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It) {
		if (It->HasAnyPropertyFlags(CPF_ReturnParm)) continue;

		Takes.Add(*It);
	}

	if (const UClass* Owner = Function->GetOwnerClass(); Owner != nullptr && Owner->ClassGeneratedBy != nullptr) {
		const int32 World = Takes.IndexOfByPredicate([](const FProperty* One) {
			return One != nullptr && One->GetFName() == TEXT("__WorldContext");
		});

		if (World != INDEX_NONE) {
			FProperty* Held = Takes[World];

			Takes.RemoveAt(World);

			int32 At = Takes.IndexOfByPredicate([](const FProperty* One) {
				return One != nullptr && One->HasAnyPropertyFlags(CPF_OutParm) && !One->HasAnyPropertyFlags(CPF_ReferenceParm);
			});

			if (At == INDEX_NONE) At = Takes.Num();

			Takes.Insert(Held, At);
		}
	}

	for (int32 Taking = 0; Taking < Takes.Num(); ++Taking) {
		FProperty* Parameter = Takes[Taking];

		if (!Parameters.IsValidIndex(Argument)) break;

		if (Parameters.Num() - Argument < Takes.Num() - Taking && IsOptionalParameter(Function, Parameter)) {
			const FName Given = KindOfArgument(Parameters[Argument]);
			const FName Wanted = KindOfParameter(Parameter);

			if (!Given.IsNone() && !Wanted.IsNone() && Given != Wanted) {
				UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\" was called without \"%s\", which this build added, so it keeps its default"),
					*Function->GetName(), *Parameter->GetName());

				continue;
			}
		}

		UEdGraphPin* Pin = Node->FindPin(Parameter->GetFName());
		const FUObjectJsonValueExport& Value = Parameters[Argument++];

		if (MacroReading::TokenOf(Value) == TEXT("EX_StructConst") && Value.Has(TEXT("Struct"))
		 && Value.GetObject(TEXT("Struct")).GetString(TEXT("ObjectName")).Contains(TEXT("LatentActionInfo"))) {
			for (const FUObjectJsonValueExport& Held : Value.Has(TEXT("Properties")) ? Value.GetArray(TEXT("Properties")) : TArray<FUObjectJsonValueExport>()) {
				if (MacroReading::TokenOf(Held) != TEXT("EX_SkipOffsetConst")) continue;

				if (const int32 Resumes = Held.GetInteger(TEXT("Value"), INDEX_NONE); Resumes >= 0) {
					if (UEdGraphPin* Then = Node->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output)) {
						Jumps.Add(MakeTuple(Then, Resumes));
					}
				}

				break;
			}

			bWaits = true;

			continue;
		}

		if (Pin == nullptr) {
			UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\" takes \"%s\", which \"%s\" has no pin for"),
				*Function->GetName(), *Parameter->GetName(), *Node->GetName());

			continue;
		}

		if (Pin->bHidden || Pin->bNotConnectable) continue;

		if (Pin->Direction == EGPD_Output) {
			if (Value.Has(TEXT("Variable"))) {
				const FUObjectJsonValueExport Variable = Value.GetObject(TEXT("Variable"));

				if (const FString Named = MacroReading::NamedProperty(Variable); !Named.IsEmpty()) {
					Locals.Add(Named, Pin);
				}
			}

			continue;
		}

		Give(this->Read(Value), Pin);
	}

	if (UK2Node_CallFunction* Made = Cast<UK2Node_CallFunction>(Node); Made != nullptr && Target != nullptr && Flow != nullptr && Flow->LinkedTo.Num() == 0 && WayIn(Made) != nullptr) {
		UK2Node_CallFunction* Before = Cast<UK2Node_CallFunction>(Flow->GetOwningNode());
		UEdGraphPin* BeforeSelf = Before != nullptr ? Before->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input) : nullptr;

		bool bAgain = Before != nullptr && Before != Made && BeforeSelf != nullptr && BeforeSelf->LinkedTo.Num() > 0
			&& Before->GetTargetFunction() != nullptr && Before->GetTargetFunction() == Made->GetTargetFunction();

		for (const UEdGraphPin* Pin : Made->Pins) {
			if (!bAgain) break;
			if (Pin == nullptr || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->PinName == UEdGraphSchema_K2::PN_Self) continue;

			if (Pin->Direction == EGPD_Output) {
				bAgain = false;

				break;
			}

			const UEdGraphPin* Same = Before->FindPin(Pin->PinName, EGPD_Input);

			bAgain = Same != nullptr && Same->LinkedTo == Pin->LinkedTo && Same->DefaultValue == Pin->DefaultValue
				&& Same->DefaultObject == Pin->DefaultObject && Same->DefaultTextValue.IdenticalTo(Pin->DefaultTextValue);
		}

		if (bAgain) {
			Made->BreakAllNodeLinks();
			Graph->RemoveNode(Made);

			Connect(Target, BeforeSelf);

			return Before;
		}
	}

	ChainExecution(Node);

	{
		int32 Wired = 0;

		for (const UEdGraphPin* Pin : Node->Pins) {
			if (Pin != nullptr) Wired += Pin->LinkedTo.Num();
		}

		if (Wired == 0) {
			UE_LOG(LogReflectionBytecode, Warning, TEXT("\"%s\" was laid down with nothing wired to it"), *Function->GetName());
		}
	}

	if (bWaits) Flow = nullptr;

	Placed++;

	return Node;
}

void FBytecodeGraph::ApplyLiteral(UEdGraphPin* Pin, const FString& Literal) {
	if (Pin == nullptr || Literal.IsEmpty()) return;

	const FName Category = Pin->PinType.PinCategory;

	FString Value = Literal;

	if (Category == UEdGraphSchema_K2::PC_Int || Category == UEdGraphSchema_K2::PC_Int64) {
		Value = FString::Printf(TEXT("%lld"), static_cast<int64>(FCString::Atod(*Literal)));
	} else if (Category == UEdGraphSchema_K2::PC_Byte) {
		if (const UEnum* Enum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get())) {
			Value = Enum->GetNameStringByValue(static_cast<int64>(FCString::Atod(*Literal)));
		} else {
			Value = FString::Printf(TEXT("%lld"), static_cast<int64>(FCString::Atod(*Literal)));
		}
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	if (Category == UEdGraphSchema_K2::PC_Text) {
		FText Words;

		if (FTextStringHelper::ReadFromBuffer(*Value, Words) != nullptr) {
			Schema->TrySetDefaultText(*Pin, Words);
		}

		return;
	}

	if (Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Class
	 || Category == UEdGraphSchema_K2::PC_SoftObject || Category == UEdGraphSchema_K2::PC_SoftClass
	 || Category == UEdGraphSchema_K2::PC_Interface) {
		UObject* Pointed = LoadObjectByPath<UObject>(Value);

		if (Pointed == nullptr) {
			const UClass* Takes = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());

			FString Package = Value;

			if (int32 Named; Package.FindLastChar(TEXT('.'), Named)) {
				LeftInline(Package, Named);
			}

			if (Takes != nullptr && !Package.IsEmpty()) {
				TObjectPtr<UObject> Brought = nullptr;
				bool bBrought = false;

				FAssetUtilities::ConstructAsset<UObject>(Package, Value, Takes->GetName(), Brought, bBrought);

				Pointed = Brought;
			}
		}

		if (Pointed != nullptr) {
			Schema->TrySetDefaultObject(*Pin, Pointed);
		} else {
			UE_LOG(LogReflectionBytecode, Warning, TEXT("nothing at \"%s\" for pin \"%s\" on \"%s\""), *Value, *Pin->PinName.ToString(), *Pin->GetOwningNode()->GetName());
		}

		return;
	}

	const FString Refused = Schema->IsPinDefaultValid(Pin, Value, nullptr, FText::GetEmpty());

	if (!Refused.IsEmpty()) {
		UE_LOG(LogReflectionBytecode, Warning, TEXT("pin \"%s\" on \"%s\" (%s) would not take \"%s\": %s"), *Pin->PinName.ToString(), *Pin->GetOwningNode()->GetName(), *Category.ToString(), *Value, *Refused);
	}

	Schema->TrySetDefaultValue(*Pin, Value);
}

void FBytecodeGraph::Give(const FValue& What, UEdGraphPin* To) {
	if (To == nullptr) return;

	if (What.Pin != nullptr) {
		Connect(What.Pin, To);
	} else if (!What.Literal.IsEmpty()) {
		ApplyLiteral(To, What.Literal);
	} else if (!What.Wanted.IsEmpty()) {
		Later.Add(TPair<UEdGraphPin*, FString>(To, What.Wanted));
	}
}

void FBytecodeGraph::Connect(UEdGraphPin* From, UEdGraphPin* To) {
	if (From == nullptr || To == nullptr) return;

	if (To->bHidden || To->bNotConnectable || From->bHidden || From->bNotConnectable) return;

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	if (Schema->IsSelfPin(*To) && !To->GetOwningNode()->IsA<UK2Node_CallFunction>()) {
		if (UK2Node_Self* Itself = Cast<UK2Node_Self>(From->GetOwningNode())) {
			if (From->LinkedTo.Num() == 0) Graph->RemoveNode(Itself);

			return;
		}
	}

	if (Schema->TryCreateConnection(From, To)) return;

	UClass* Target = Cast<UClass>(To->PinType.PinSubCategoryObject.Get());

	if (Target != nullptr) {
		if (const UClass* Already = Cast<UClass>(From->PinType.PinSubCategoryObject.Get()); Already != nullptr && Already->IsChildOf(Target)) {
			UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\" is a %s already, so it was not cast to one again"),
				*From->PinName.ToString(), *Target->GetName());

			return;
		}
	}

	if (Target != nullptr && From->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && To->PinType.PinCategory == UEdGraphSchema_K2::PC_Object) {
		UK2Node_DynamicCast* Node = AddNode<UK2Node_DynamicCast>();

		Node->TargetType = Target;
		Node->AllocateDefaultPins();

		Node->SetPurity(true);

		UEdGraphPin* Source = Node->GetCastSourcePin();
		UEdGraphPin* Result = Node->GetCastResultPin();

		if (Source != nullptr && Result != nullptr && Schema->TryCreateConnection(From, Source) && Schema->TryCreateConnection(Result, To)) {
			return;
		}

		Graph->RemoveNode(Node);
	}

	Unhandled.AddUnique(FString::Printf(TEXT("link %s -> %s"), *From->PinName.ToString(), *To->PinName.ToString()));
}

void FBytecodeGraph::LinkExecution(UEdGraphPin* From, UEdGraphPin* To) {
	if (From == nullptr || To == nullptr) return;

	if (From->LinkedTo.Num() > 0) {
		Contested.AddUnique(FString::Printf(TEXT("%s.%s"), *From->GetOwningNode()->GetName(), *From->PinName.ToString()));

		return;
	}

	From->MakeLinkTo(To);
}

UEdGraphPin* FBytecodeGraph::WayIn(UEdGraphNode* Node) {
	if (Node == nullptr) return nullptr;

	if (UEdGraphPin* Named = Node->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input)) return Named;

	for (UEdGraphPin* Pin : Node->Pins) {
		if (Pin != nullptr && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) return Pin;
	}

	return nullptr;
}

void FBytecodeGraph::EnterNode(UK2Node* Node) {
	UEdGraphPin* In = WayIn(Node);

	if (In == nullptr) return;

	if (Flow != nullptr) {
		LinkExecution(Flow, In);

		if (Start == nullptr) {
			Start = In;
		}

		Chained++;
	} else {
		UE_LOG(LogReflectionBytecode, Warning, TEXT("nothing leads into \"%s\""), *Node->GetName());

		Orphaned++;
	}
}

void FBytecodeGraph::ChainExecution(UK2Node* Node) {
	if (WayIn(Node) == nullptr) return;

	EnterNode(Node);

	Flow = Node->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);

	if (Flow == nullptr) {
		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin != nullptr && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) {
				Flow = Pin;

				break;
			}
		}

		if (Flow == nullptr && !Node->IsA<UK2Node_FunctionResult>()) {
			Unhandled.AddUnique(FString::Printf(TEXT("no way out of %s"), *Node->GetName()));
		}
	}
}

bool FBytecodeGraph::FillStruct(const FUObjectJsonValueExport& Statement, const FUObjectJsonValueExport& Variable, const FValue& Expression) {
	const FUObjectJsonValueExport Held = Variable.GetObject(TEXT("Property"));

	FString Owner, Member;
	SplitReference(Held.GetObject(TEXT("ResolvedOwner")), Owner, Member);

	FString Named;

	if (Owner.IsEmpty()) {
		ReadPropertyReference(Held, Owner, Named);
	}

	UScriptStruct* Kind = FindStructByType(Member.IsEmpty() ? Owner : Member);

	if (Kind == nullptr) {
		Unhandled.AddUnique(FString::Printf(TEXT("a struct of \"%s\", which this build does not carry"), *Owner));

		return false;
	}

	if (Named.IsEmpty()) {
		if (const TArray<TSharedPtr<FJsonValue>>* Path = nullptr; Held.JsonObject.IsValid() && Held.JsonObject->TryGetArrayField(TEXT("Path"), Path) && Path->Num() == 1) {
			Named = (*Path)[0]->AsString();
		}
	}

	if (Named.IsEmpty()) return false;

	const FString Into = MacroReading::NamedProperty(Variable.GetObject(TEXT("StructExpression")).GetObject(TEXT("Variable")));

	if (Into.IsEmpty()) return false;

	const FUObjectJsonValueExport Holder = Variable.GetObject(TEXT("StructExpression"));
	const FString HeldThrough = MacroReading::TokenOf(Holder);

	if (HasLocal(Into) || Handouts.Contains(Into) || HeldThrough == TEXT("EX_InstanceVariable")) {
		UK2Node_SetFieldsInStruct* Setting = nullptr;

		if (const TPair<UK2Node_SetFieldsInStruct*, int32>* Last = SettingMembers.Find(Into); Last != nullptr && Last->Value == Placing - 1) {
			Setting = Last->Key;
		}

		if (Setting == nullptr) {
			const FValue Kept = ReadVariable(Holder);

			if (Kept.Pin == nullptr) return false;

			Setting = AddNode<UK2Node_SetFieldsInStruct>();

			Setting->StructType = Kind;
			Setting->bMadeAfterOverridePinRemoval = true;
			Setting->AllocateDefaultPins();

			Connect(Kept.Pin, Setting->FindPin(TEXT("StructRef"), EGPD_Input));

			ChainExecution(Setting);

			Placed++;
		}

		SettingMembers.Add(Into, TPair<UK2Node_SetFieldsInStruct*, int32>(Setting, Placing));

		for (FOptionalPinFromProperty& Shown : Setting->ShowPinForProperties) {
			if (Shown.PropertyName == FName(*Named)) Shown.bShowPin = true;
		}

		const bool bRunLeavesHere = Flow != nullptr && Flow->GetOwningNodeUnchecked() == Setting;

		Setting->ReconstructNode();

		if (bRunLeavesHere) Flow = Setting->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);

		UEdGraphPin* MemberPin = Setting->FindPin(*Named, EGPD_Input);

		if (MemberPin == nullptr) return false;

		Give(Expression, MemberPin);

		return true;
	}

	UK2Node_MakeStruct* Node = nullptr;

	if (UEdGraphPin** Already = Locals.Find(Into); Already != nullptr && *Already != nullptr) {
		Node = Cast<UK2Node_MakeStruct>((*Already)->GetOwningNode());
	}

	if (Node == nullptr) {
		Node = AddNode<UK2Node_MakeStruct>();

		Node->StructType = Kind;

		Node->bMadeAfterOverridePinRemoval = true;

		Node->AllocateDefaultPins();

		if (UEdGraphPin* Out = Node->FindPin(Kind->GetFName(), EGPD_Output)) {
			Locals.Add(Into, Out);
		}

		Placed++;
	}

	UEdGraphPin* Pin = Node->FindPin(*Named, EGPD_Input);

	if (Pin == nullptr) return false;

	if (Expression.Pin != nullptr) {
		Connect(Expression.Pin, Pin);
	} else if (!Expression.Literal.IsEmpty()) {
		ApplyLiteral(Pin, Expression.Literal);
	}

	return true;
}

namespace {
	UFunction* SetterFor(const UClass* Class, const FString& Member) {
		if (Class == nullptr || Member.IsEmpty()) return nullptr;

		const FProperty* Property = FindFProperty<FProperty>(Class, *Member);

		if (Property == nullptr) return nullptr;
		if (Property->HasAnyPropertyFlags(CPF_BlueprintVisible) && !Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly)) return nullptr;

		UFunction* Setter = Class->FindFunctionByName(*(FString(TEXT("Set")) + Member));

		if (Setter == nullptr || !Setter->HasAnyFunctionFlags(FUNC_BlueprintCallable)) return nullptr;

		const FProperty* Taken = nullptr;
		int32 Taking = 0;

		for (TFieldIterator<FProperty> It(Setter); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) {
			if (It->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
			if (It->HasAnyPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ReferenceParm)) continue;

			Taken = *It;
			Taking++;
		}

		return Taking == 1 && Taken != nullptr && Taken->SameType(Property) ? Setter : nullptr;
	}
}

bool FBytecodeGraph::Place(const FUObjectJsonValueExport& Statement) {
	if (!Statement.Has(TEXT("Token"))) return false;

	const FString Token = Statement.GetString(TEXT("Token"));

	if (Token == TEXT("EX_Let") || Token == TEXT("EX_LetBool") || Token == TEXT("EX_LetObj") || Token == TEXT("EX_LetWeakObjPtr")) {
		const FUObjectJsonValueExport Variable = Statement.GetObject(TEXT("Variable"));

		const FString Was = Writing;

		Writing = Variable.Has(TEXT("Variable")) ? MacroReading::NamedProperty(Variable.GetObject(TEXT("Variable"))) : FString();

		const bool bAgain = !Writing.IsEmpty() && Writes.FindRef(Writing) > 1;

		FValue Expression;

		if (Writing.StartsWith(TEXT("K2Node_MathExpression"))) {
			Expression = ReadMathExpression(Statement.GetObject(TEXT("Expression")));
		}

		if (!Expression.IsSet()) {
			Expression = bAgain
				? ReadExpression(Statement.GetObject(TEXT("Expression")))
				: Read(Statement.GetObject(TEXT("Expression")));
		}

		const FString Into = Writing;

		Writing = Was;

		const auto CallSetter = [this, &Expression](UFunction* Setter, const FValue& Target) {
			UK2Node_CallFunction* Node = AddNodeOfClass<UK2Node_CallFunction>(NodeClassFor(Setter));

			Node->SetFromFunction(Setter);
			Node->AllocateDefaultPins();

			if (Target.Pin != nullptr || !Target.Wanted.IsEmpty()) {
				Give(Target, Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input));
			}

			for (TFieldIterator<FProperty> It(Setter); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) {
				if (It->HasAnyPropertyFlags(CPF_ReturnParm)) continue;

				if (UEdGraphPin* Pin = Node->FindPin(It->GetFName(), EGPD_Input)) {
					Give(Expression, Pin);

					break;
				}
			}

			ChainExecution(Node);

			Placed++;

			return true;
		};

		if (!Into.IsEmpty()) Version.FindOrAdd(Into)++;

		if (MacroReading::TokenOf(Variable) == TEXT("EX_StructMemberContext")) {
			if (Decided.Num() > 0) {
				FString Owner, Member;

				ReadStructMemberContext(Variable, Owner, Member);

				if (UEdGraphPin* const* Answers = Decided.Find(FString::Printf(TEXT("%s.%s"), *Owner, *Member))) {
					if (Expression.Pin != nullptr) {
						Connect(Expression.Pin, *Answers);
					} else if (!Expression.Literal.IsEmpty()) {
						ApplyLiteral(*Answers, Expression.Literal);
					}

					return true;
				}
			}

			return FillStruct(Statement, Variable, Expression);
		}

		if (const FString Upon = MacroReading::TokenOf(Variable);
			Upon == TEXT("EX_Context") || Upon == TEXT("EX_Context_FailSilent") || Upon == TEXT("EX_ClassContext")) {
			const FUObjectJsonValueExport Inner = Variable.GetObject(TEXT("ContextExpression"));

			if (Inner.Has(TEXT("Token")) && Inner.GetString(TEXT("Token")).EndsWith(TEXT("Variable"))) {
				const FUObjectJsonValueExport Held = Inner.GetObject(TEXT("Variable"));

				const FString Member = MacroReading::NamedProperty(Held);
				FString Owner;

				if (Held.Has(TEXT("ResolvedOwner"))) {
					FString Unused;
					SplitReference(Held.GetObject(TEXT("ResolvedOwner")), Owner, Unused);
				} else if (Held.Has(TEXT("ObjectName"))) {
					FString Unused;
					SplitReference(Held, Owner, Unused);
				} else if (Held.Has(TEXT("Owner"))) {
					FString Unused;
					SplitReference(Held.GetObject(TEXT("Owner")), Owner, Unused);
				} else if (Held.Has(TEXT("Outer"))) {
					FString Unused;
					SplitReference(Held.GetObject(TEXT("Outer")), Owner, Unused);
				}

				UClass* On = Owner.IsEmpty() ? nullptr : const_cast<UClass*>(FindClassByType(Owner));

				if (On == nullptr && !Owner.IsEmpty()) {
					if (Held.Has(TEXT("Owner"))) {
						BringInClass(Held.GetObject(TEXT("Owner")));
					} else if (Held.Has(TEXT("Outer"))) {
						BringInClass(Held.GetObject(TEXT("Outer")));
					}

					On = const_cast<UClass*>(FindClassByType(Owner));
				}

				const FValue Target = On != nullptr && !Member.IsEmpty()
					? Read(Variable.GetObject(TEXT("ObjectExpression")))
					: FValue();

				if (Target.Pin != nullptr || !Target.Wanted.IsEmpty()) {
					if (UFunction* Setter = SetterFor(On, Member)) {
						return CallSetter(Setter, Target);
					}

					UK2Node_VariableSet* Node = AddNode<UK2Node_VariableSet>();

					Node->VariableReference.SetExternalMember(*Member, On);
					Node->AllocateDefaultPins();

					Give(Target, Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input));

					if (UEdGraphPin* Pin = Node->FindPin(*Member, EGPD_Input)) {
						Give(Expression, Pin);
					}

					ChainExecution(Node);

					Placed++;

					return true;
				}
			}
		}

		FString Name;

		if (Variable.Has(TEXT("Variable"))) {
			Name = MacroReading::NamedProperty(Variable.GetObject(TEXT("Variable")));
		}

		if (const FString* Out = !Name.IsEmpty() ? Answered.Find(Name) : nullptr) {
			return AnswerHere(*Out, Expression);
		}

		if (!Name.IsEmpty() && Answered.FindRef(MacroReading::NamedProperty(
			Statement.GetObject(TEXT("Expression")).GetObject(TEXT("Variable")))) == Name) {
			return true;
		}

		if (MacroReading::TokenOf(Variable) == TEXT("EX_LocalOutVariable") && !Name.IsEmpty() && !Answered.Contains(Name)) {
			return AnswerHere(Name, Expression);
		}

		if (!Name.IsEmpty() && (IsMade(Name))) {
			if (Expression.Pin != nullptr) {
				Locals.Add(Name, Expression.Pin);
			} else if (Constants.Contains(Name) && !Expression.Literal.IsEmpty()) {
				Carried.Add(Name, Expression.Literal);
			}

			if (AnsweredAt == Placing - 1) AnsweredAt = Placing;

			return true;
		}

		if (UEdGraphPin* Answer = MacroReading::TokenOf(Variable) != TEXT("EX_InstanceVariable") ? FindAnswer(Name) : nullptr) {
			if (Expression.Pin != nullptr) {
				Connect(Expression.Pin, Answer);
			} else if (!Expression.Literal.IsEmpty()) {
				ApplyLiteral(Answer, Expression.Literal);
			}

			return true;
		}

		if (KeepsTemporary(Name)) {
			const FUObjectJsonValueExport Held = Variable.GetObject(TEXT("Variable"));

			UK2Node_AssignmentStatement* Node = AddNode<UK2Node_AssignmentStatement>();

			Node->AllocateDefaultPins();

			Connect(Temporary(Name, Held.Has(TEXT("Property")) ? Held.GetObject(TEXT("Property")) : Held), Node->GetVariablePin());

			Give(Expression, Node->GetValuePin());

			ChainExecution(Node);

			Placed++;

			return true;
		}

		if (!Name.IsEmpty()) {
			const FUObjectJsonValueExport Inner = Variable.GetObject(TEXT("Variable"));

			const FString WrittenThrough = MacroReading::TokenOf(Variable);
			const bool bWritesParameter = WrittenThrough == TEXT("EX_LocalVariable") && Handouts.Contains(Name);
			const bool bWritesMember = WrittenThrough == TEXT("EX_InstanceVariable");
			const bool bWritesLocal = bWritesParameter || (HasLocal(Name) && !bWritesMember);

			if (!bWritesLocal) {
				const UBlueprint* Owning = Graph != nullptr ? Graph->GetTypedOuter<UBlueprint>() : nullptr;
				const UClass* Own = Owning != nullptr ? (Owning->SkeletonGeneratedClass != nullptr ? Owning->SkeletonGeneratedClass.Get() : Owning->ParentClass.Get()) : nullptr;

				if (UFunction* Setter = SetterFor(Own, Name)) {
					return CallSetter(Setter, FValue());
				}
			}

			UK2Node_VariableSet* Node = AddNode<UK2Node_VariableSet>();

			if (bWritesLocal) {
				PointAtLocal(Node, Name);
			} else {
				Node->VariableReference.SetSelfMember(*Name);
			}

			Node->AllocateDefaultPins();

			if (UEdGraphPin* Pin = Node->FindPin(*Name, EGPD_Input)) {
				Give(Expression, Pin);
			}

			ChainExecution(Node);

			Placed++;

			return true;
		}

		return false;
	}

	if (Token == TEXT("EX_Context") || Token == TEXT("EX_Context_FailSilent") || Token == TEXT("EX_ClassContext") || Token == TEXT("EX_CallMath") || Token == TEXT("EX_FinalFunction") || Token == TEXT("EX_LocalFinalFunction") || Token == TEXT("EX_VirtualFunction") || Token == TEXT("EX_LocalVirtualFunction")) {
		return Read(Statement).IsSet() || Placed > 0;
	}

	if (Token == TEXT("EX_PushExecutionFlow")) {
		if (const int32 Ends = MacroReading::IndexOfAddress(Statements, Statement.GetInteger(TEXT("PushingAddress"), -1));
			Statements.IsValidIndex(Ends) && MacroReading::TokenOf(Statements[Ends]) == TEXT("EX_Return")) {
			return true;
		}

		UK2Node_ExecutionSequence* Node = AddNode<UK2Node_ExecutionSequence>();

		Node->AllocateDefaultPins();

		EnterNode(Node);

		Flow = Node->GetThenPinGivenIndex(0);

		Jumps.Add(MakeTuple(Node->GetThenPinGivenIndex(1), Statement.GetInteger(TEXT("PushingAddress"), -1)));

		Placed++;

		return true;
	}

	if (Token == TEXT("EX_PopExecutionFlow")) {
		Flow = nullptr;

		return true;
	}

	if (Token == TEXT("EX_PopExecutionFlowIfNot") || Token == TEXT("EX_JumpIfNot")) {
		if (const FValue Asked = Read(Statement.GetObject(TEXT("BooleanExpression"))); Asked.Pin != nullptr) {
			if (UK2Node_DynamicCast* Cast = ::Cast<UK2Node_DynamicCast>(Asked.Pin->GetOwningNode()); Cast != nullptr && !Cast->IsNodePure()) {
				if (Token == TEXT("EX_JumpIfNot")) {
					Jumps.Add(MakeTuple(Cast->GetInvalidCastPin(), Statement.GetInteger(TEXT("CodeOffset"), -1)));
				}

				Placed++;

				return true;
			}

			if (UK2Node_GetDataTableRow* Row = ::Cast<UK2Node_GetDataTableRow>(Asked.Pin->GetOwningNode())) {
				if (Token == TEXT("EX_JumpIfNot")) {
					Jumps.Add(MakeTuple(Row->GetRowNotFoundPin(), Statement.GetInteger(TEXT("CodeOffset"), -1)));
				}

				Placed++;

				return true;
			}
		}

		UK2Node_IfThenElse* Node = AddNode<UK2Node_IfThenElse>();

		Node->AllocateDefaultPins();

		EnterNode(Node);

		const FValue Condition = Read(Statement.GetObject(TEXT("BooleanExpression")));

		if (Condition.Pin != nullptr) {
			Connect(Condition.Pin, Node->GetConditionPin());
		} else if (!Condition.Literal.IsEmpty()) {
			ApplyLiteral(Node->GetConditionPin(), Condition.Literal);
		}

		Flow = Node->GetThenPin();

		if (Token == TEXT("EX_JumpIfNot")) {
			Jumps.Add(MakeTuple(Node->GetElsePin(), Statement.GetInteger(TEXT("CodeOffset"), -1)));
		}

		Placed++;

		return true;
	}

	if (Token == TEXT("EX_Jump")) {
		const int32 Address = MacroReading::AddressOf(Statement);
		const int32 Ends = MacroReading::IndexOfAddress(Statements, Statement.GetInteger(TEXT("CodeOffset"), -1));

		if (HoldsLocals() && AnsweredAt != Placing - 1 && Statements.IsValidIndex(Ends) && MacroReading::TokenOf(Statements[Ends]) == TEXT("EX_Return")) {
			bool bGoneTo = false;

			for (const FUObjectJsonValueExport& Other : Statements) {
				const FString Going = MacroReading::TokenOf(Other);

				if (Going == TEXT("EX_Jump") || Going == TEXT("EX_JumpIfNot")) {
					bGoneTo = Other.GetInteger(TEXT("CodeOffset"), -1) == Address;
				} else if (Going == TEXT("EX_PushExecutionFlow")) {
					bGoneTo = Other.GetInteger(TEXT("PushingAddress"), -1) == Address;
				}

				if (bGoneTo) break;
			}

			if (bGoneTo) {
				ChainExecution(NewReturn());

				Placed++;

				return true;
			}
		}

		Jumps.Add(MakeTuple(Flow, Statement.GetInteger(TEXT("CodeOffset"), -1)));

		Flow = nullptr;

		return true;
	}

	if (Token == TEXT("EX_Return")) {
		const bool bAnswers = Graph->Nodes.ContainsByPredicate([](const UEdGraphNode* Node) {
			if (Node == nullptr || !Node->IsA<UK2Node_FunctionResult>()) return false;

			return Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin) {
				return Pin != nullptr && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec;
			});
		});

		if (HoldsLocals() && !bAnswers) {
			const int32 Address = MacroReading::AddressOf(Statement);

			bool bStacks = false;
			bool bGoneTo = false;

			for (const FUObjectJsonValueExport& Other : Statements) {
				const FString Going = MacroReading::TokenOf(Other);

				if (Going == TEXT("EX_PushExecutionFlow")) bStacks = true;

				if ((Going == TEXT("EX_Jump") || Going == TEXT("EX_JumpIfNot")) && Other.GetInteger(TEXT("CodeOffset"), -1) == Address) bGoneTo = true;
			}

			if (bStacks && (Flow != nullptr || bGoneTo)) {
				UK2Node_FunctionResult* Node = NewReturn();

				if (UEdGraphPin* In = WayIn(Node); In != nullptr && Address >= 0) {
					Entries.Add(Address, In);
				}

				if (Flow != nullptr) ChainExecution(Node);

				Placed++;

				return true;
			}

			return true;
		}

		for (UEdGraphNode* Node : Graph->Nodes) {
			if (UK2Node_FunctionResult* Answering = Cast<UK2Node_FunctionResult>(Node)) {
				const int32 Address = MacroReading::AddressOf(Statement);

				if (bAnswers) break;

				if (Address >= 0 && !Entries.Contains(Address)) {
					if (UEdGraphPin* In = WayIn(Answering)) {
						Entries.Add(Address, In);
					}
				}

				ChainExecution(Answering);

				break;
			}
		}

		return true;
	}

	if (Token == TEXT("EX_EndOfScript") || Token == TEXT("EX_Nothing")) {
		return true;
	}

	if (Token == TEXT("EX_BindDelegate")) {
		UK2Node_CreateDelegate* Node = AddNode<UK2Node_CreateDelegate>();

		Node->AllocateDefaultPins();

		if (Statement.Has(TEXT("ObjectTerm"))) {
			const FValue Of = Read(Statement.GetObject(TEXT("ObjectTerm")));

			if (Of.Pin != nullptr && Node->GetObjectInPin() != nullptr) Connect(Of.Pin, Node->GetObjectInPin());
		}

		if (Statement.Has(TEXT("FunctionName"))) {
			Node->SetFunction(FName(*Statement.GetString(TEXT("FunctionName"))));
		}

		if (Statement.Has(TEXT("Delegate"))) {
			const FUObjectJsonValueExport Into = Statement.GetObject(TEXT("Delegate"));

			if (Into.Has(TEXT("Variable"))) {
				if (const FString Name = MacroReading::NamedProperty(Into.GetObject(TEXT("Variable"))); !Name.IsEmpty() && Node->GetDelegateOutPin() != nullptr) {
					Locals.Add(Name, Node->GetDelegateOutPin());
				}
			}
		}

		Placed++;

		return true;
	}

	if (Token == TEXT("EX_AddMulticastDelegate") || Token == TEXT("EX_RemoveMulticastDelegate")) {
		UK2Node_BaseMCDelegate* Node = Token == TEXT("EX_AddMulticastDelegate")
			? static_cast<UK2Node_BaseMCDelegate*>(AddNode<UK2Node_AddDelegate>())
			: static_cast<UK2Node_BaseMCDelegate*>(AddNode<UK2Node_RemoveDelegate>());

		UEdGraphPin* Against = PointAtDelegate(Node, Statement.GetObject(TEXT("MulticastDelegate")));

		Node->AllocateDefaultPins();

		if (Against != nullptr && Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input) != nullptr) {
			Connect(Against, Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input));
		}

		if (Statement.Has(TEXT("Delegate"))) {
			const FValue Bound = Read(Statement.GetObject(TEXT("Delegate")));

			if (Bound.Pin != nullptr && Node->GetDelegatePin() != nullptr) Connect(Bound.Pin, Node->GetDelegatePin());
		}

		ChainExecution(Node);

		Placed++;

		return true;
	}

	if (Token == TEXT("EX_ClearMulticastDelegate")) {
		UK2Node_ClearDelegate* Node = AddNode<UK2Node_ClearDelegate>();

		UEdGraphPin* Against = PointAtDelegate(Node, Statement.GetObject(TEXT("DelegateToClear")));

		Node->AllocateDefaultPins();

		if (Against != nullptr && Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input) != nullptr) {
			Connect(Against, Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input));
		}

		ChainExecution(Node);

		Placed++;

		return true;
	}

	if (Token == TEXT("EX_CallMulticastDelegate")) {
		UK2Node_CallDelegate* Node = AddNode<UK2Node_CallDelegate>();

		UEdGraphPin* Against = PointAtDelegate(Node, Statement.GetObject(TEXT("Delegate")));

		Node->AllocateDefaultPins();

		if (Against != nullptr && Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input) != nullptr) {
			Connect(Against, Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input));
		}

		const TArray<FUObjectJsonValueExport> Given = Statement.Has(TEXT("Parameters")) ? Statement.GetArray(TEXT("Parameters")) : TArray<FUObjectJsonValueExport>();

		if (const UFunction* Signature = Node->GetDelegateSignature()) {
			int32 Which = 0;

			for (TFieldIterator<FProperty> It(Signature); It && (It->PropertyFlags & CPF_Parm); ++It) {
				if (It->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
				if (!Given.IsValidIndex(Which)) break;

				UEdGraphPin* Pin = Node->FindPin(It->GetFName());
				const FValue Held = Read(Given[Which++]);

				if (Pin == nullptr) continue;

				Give(Held, Pin);
			}
		}

		ChainExecution(Node);

		Placed++;

		return true;
	}

	if (Token == TEXT("EX_SetArray")) {
		const FValue Built = Read(Statement);

		if (Built.Pin != nullptr && Statement.Has(TEXT("AssigningProperty"))) {
			const FUObjectJsonValueExport Into = Statement.GetObject(TEXT("AssigningProperty"));

			if (Into.Has(TEXT("Variable"))) {
				if (const FString Name = MacroReading::NamedProperty(Into.GetObject(TEXT("Variable"))); !Name.IsEmpty()) {
					Locals.Add(Name, Built.Pin);
				}
			}
		}

		return true;
	}

	if (Token == TEXT("EX_SetMap")) {
		const FValue Built = Read(Statement);

		if (Built.Pin != nullptr && Statement.Has(TEXT("MapProperty"))) {
			const FUObjectJsonValueExport Into = Statement.GetObject(TEXT("MapProperty"));

			if (Into.Has(TEXT("Variable"))) {
				if (const FString Name = MacroReading::NamedProperty(Into.GetObject(TEXT("Variable"))); !Name.IsEmpty()) {
					Locals.Add(Name, Built.Pin);
				}
			}
		}

		return true;
	}

	Unhandled.AddUnique(Token);

	return false;
}

void FBytecodeGraph::Only(const int32 From, const int32 To) {
	Stretch.Add(TPair<int32, int32>(From, To));
}

void FBytecodeGraph::LeaveOut(const int32 From, const int32 To) {
	Elsewhere.Add(TPair<int32, int32>(From, To));
}

void FBytecodeGraph::LeaveOutAt(const int32 Address) {
	if (Address >= 0) ElsewhereAt.Add(Address);
}

void FBytecodeGraph::HandsInto(const FString& Owner, const FName Member, UEdGraphPin* Pin) {
	if (Owner.IsEmpty() || Pin == nullptr) return;

	Decided.Add(FString::Printf(TEXT("%s.%s"), *Owner, *Member.ToString()), Pin);
}

void FBytecodeGraph::EnterAt(const int32 Address, UK2Node* Node, const FName Through) {
	if (Node == nullptr || Address < 0) return;

	int32 Begins = Address;

	const int32 Stub = MacroReading::IndexOfAddress(Statements, Address);

	if (Statements.IsValidIndex(Stub) && MacroReading::TokenOf(Statements[Stub]) == TEXT("EX_Jump")) {
		Ignored.Add(Address);

		Begins = Statements[Stub].GetInteger(TEXT("CodeOffset"), Address);
	}

	Starts.FindOrAdd(Begins).Add(TPair<TWeakObjectPtr<UK2Node>, FName>(Node, Through));
}

void FBytecodeGraph::SitsAt(const int32 Address, UK2Node* Node) {
	if (Node == nullptr || Address < 0) return;

	Sitting.FindOrAdd(Address).Add(Node);
}

void FBytecodeGraph::HandOverTrack(const FString& Name, UK2Node* Node, const FName Pin) {
	if (Name.IsEmpty() || Node == nullptr || Pin.IsNone()) return;

	Tracks.Add(Name, TPair<TWeakObjectPtr<UK2Node>, FName>(Node, Pin));
}

void FBytecodeGraph::HandOver(const FString& Frame, const FString& Parameter, UK2Node* Node) {
	if (Frame.IsEmpty() || Parameter.IsEmpty() || Node == nullptr) return;

	Handed.Add(Frame, TPair<TWeakObjectPtr<UK2Node>, FString>(Node, Parameter));
}

int32 FBytecodeGraph::DeclareLocals() {
	if (Graph == nullptr) return 0;

	FindConstants();
	FindAnswers();
	FindPureCasts();

	if (bTidy) {
		FindMacros();
	}

	int32 Added = 0;

	for (UEdGraphNode* Node : Graph->Nodes) {
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node)) {
			Entry->LocalVariables.Empty();

			break;
		}
	}

	for (const FUObjectJsonValueExport& Local : Declared) {
		if (!Local.Has(TEXT("Name"))) continue;

		if (bool bGivenBack; FBlueprintGraphs::IsParameter(Local.JsonObject, bGivenBack)) continue;

		const FString Name = Local.GetString(TEXT("Name"));

		if (IsMade(Name)) continue;
		if (HasLocal(Name)) continue;

		if (!HoldsLocals()) {
			if (KeepsTemporary(Name)) continue;

			Unhandled.AddUnique(FString::Printf(TEXT("\"%s\" belongs to a macro that was not read back, and %s cannot keep one"), *Name, *Graph->GetName()));

			continue;
		}

		if (EnsureLocal(Name, Local)) {
			Added++;
		} else {
			Unhandled.AddUnique(FString::Printf(TEXT("local %s"), *Name));
		}
	}

	return Added;
}

UEdGraph* FBytecodeGraph::FindStandardMacro(const TCHAR* Named) {
	return MacroReading::StandardMacro(Named);
}

namespace {
	void NamesRead(const TSharedPtr<FJsonValue>& Held, TArray<FString>& Out) {
		if (!Held.IsValid()) return;

		if (Held->Type == EJson::Array) {
			for (const TSharedPtr<FJsonValue>& One : Held->AsArray()) NamesRead(One, Out);

			return;
		}

		if (Held->Type != EJson::Object) return;

		const TSharedPtr<FJsonObject> Object = Held->AsObject();

		if (!Object.IsValid()) return;

		FString Token;

		if (Object->TryGetStringField(TEXT("Token"), Token) && Token == TEXT("EX_LocalVariable")) {
			const TSharedPtr<FJsonObject>* Named = nullptr;

			if (Object->TryGetObjectField(TEXT("Variable"), Named) && Named != nullptr && (*Named).IsValid()) {
				if (const FString Name = MacroReading::NamedProperty(FUObjectJsonValueExport(*Named)); !Name.IsEmpty()) {
					Out.Add(Name);
				}
			}
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values) NamesRead(Field.Value, Out);
	}

	void ArraysChanged(const TSharedPtr<FJsonValue>& Held, TArray<FString>& Out) {
		static const TSet<FString> Changing = {
			TEXT("Array_Add"), TEXT("Array_AddUnique"), TEXT("Array_Append"), TEXT("Array_Insert"),
			TEXT("Array_Remove"), TEXT("Array_RemoveItem"), TEXT("Array_Clear"), TEXT("Array_Resize"),
			TEXT("Array_Shuffle"), TEXT("Array_Set"), TEXT("Array_Swap"),
		};

		if (!Held.IsValid()) return;

		if (Held->Type == EJson::Array) {
			for (const TSharedPtr<FJsonValue>& One : Held->AsArray()) ArraysChanged(One, Out);

			return;
		}

		if (Held->Type != EJson::Object) return;

		const TSharedPtr<FJsonObject> Object = Held->AsObject();

		if (!Object.IsValid()) return;

		const TSharedPtr<FJsonObject>* Function = nullptr;
		FString Called;

		if (Object->TryGetObjectField(TEXT("Function"), Function) && Function != nullptr && (*Function).IsValid()
			&& (*Function)->TryGetStringField(TEXT("ObjectName"), Called) && Called.Contains(TEXT("KismetArrayLibrary:"))) {
			FString Short = Called;
			Short.Split(TEXT(":"), nullptr, &Short);
			Short.RemoveFromEnd(TEXT("'"));

			const TArray<TSharedPtr<FJsonValue>>* Parameters = nullptr;

			if (Changing.Contains(Short) && Object->TryGetArrayField(TEXT("Parameters"), Parameters) && Parameters != nullptr && Parameters->Num() > 0) {
				TArray<FString> First;
				NamesRead((*Parameters)[0], First);

				if (First.Num() == 1) Out.Add(First[0]);
			}
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values) ArraysChanged(Field.Value, Out);
	}
}

void FBytecodeGraph::FindPureCasts() {
	if (bLookedForPureCasts) return;

	bLookedForPureCasts = true;

	TMap<FString, FString> Asking;

	for (const FUObjectJsonValueExport& Statement : Statements) {
		if (!MacroReading::IsLet(MacroReading::TokenOf(Statement))) continue;

		const FUObjectJsonValueExport Held = Statement.GetObject(TEXT("Expression"));

		if (MacroReading::TokenOf(Held) != TEXT("EX_Cast")) continue;

		const FString Name = MacroReading::WrittenTo(Statement);
		const FString Reached = MacroReading::ReadFrom(Held.GetObject(TEXT("Target")));

		if (Name.IsEmpty() || Reached.IsEmpty()) continue;

		Asking.Add(Name, Reached);
	}

	if (Asking.Num() == 0) return;

	TSet<FString> Tested;

	for (const FUObjectJsonValueExport& Statement : Statements) {
		if (!Statement.JsonObject.IsValid()) continue;

		const FString Token = MacroReading::TokenOf(Statement);
		const bool bTests = Token == TEXT("EX_JumpIfNot") || Token == TEXT("EX_PopExecutionFlowIfNot");

		TArray<FString> Names;

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Statement.JsonObject->Values) {
			if (bTests && Field.Key == TEXT("BooleanExpression")) {
				TArray<FString> Asked;

				NamesRead(Field.Value, Asked);

				Tested.Append(Asked);

				continue;
			}

			if (Field.Key == TEXT("Variable")) continue;

			NamesRead(Field.Value, Names);
		}

		for (const FString& Name : Names) {
			if (const FString* Reached = Asking.Find(Name)) PureCasts.Add(*Reached);
		}
	}

	for (const TPair<FString, FString>& One : Asking) {
		if (!Tested.Contains(One.Key)) PureCasts.Add(One.Value);
	}

	for (const TPair<FString, FString>& One : Asking) {
		if (Tested.Contains(One.Key) && !PureCasts.Contains(One.Value)) ImpureCasts.Add(One.Value);
	}
}

void FBytecodeGraph::FindAnswers() {
	if (bLookedForAnswers) return;

	bLookedForAnswers = true;

	for (const FUObjectJsonValueExport& Statement : Statements) {
		if (!MacroReading::IsLet(MacroReading::TokenOf(Statement))) continue;

		const FUObjectJsonValueExport Variable = Statement.GetObject(TEXT("Variable"));

		if (MacroReading::TokenOf(Variable) != TEXT("EX_LocalOutVariable")) continue;

		const FUObjectJsonValueExport Held = Statement.GetObject(TEXT("Expression"));

		if (MacroReading::TokenOf(Held) != TEXT("EX_LocalVariable")) continue;

		const FString Out = MacroReading::NamedProperty(Variable.GetObject(TEXT("Variable")));
		const FString Local = MacroReading::NamedProperty(Held.GetObject(TEXT("Variable")));

		if (Out.IsEmpty() || Local != Out + TEXT("_Local")) continue;

		const FUObjectJsonValueExport Kept = Held.GetObject(TEXT("Variable"));
		const FUObjectJsonValueExport Described = Kept.Has(TEXT("Property")) ? Kept.GetObject(TEXT("Property")) : Kept;

		if (Described.Has(TEXT("PropertyFlags"))) {
			TArray<FString> Flags;
			Described.GetString(TEXT("PropertyFlags")).ParseIntoArray(Flags, TEXT(" | "));

			if (Flags.Contains(TEXT("Edit"))) continue;
		}

		Answered.Add(Local, Out);
	}
}

bool FBytecodeGraph::AnswerHere(const FString& Out, const FValue& Expression) {
	if (Graph == nullptr) return false;

	UK2Node_FunctionResult* Node = AnsweredAt == Placing - 1 && AnsweredWith.IsValid() ? AnsweredWith.Get() : nullptr;

	const bool bCarryingOn = Node != nullptr;

	if (Node == nullptr) Node = NewReturn();

	UEdGraphPin* Answer = Node->FindPin(*Out, EGPD_Input);

	if (Answer == nullptr) {
		if (!bCarryingOn) Graph->RemoveNode(Node);

		Unhandled.AddUnique(FString::Printf(TEXT("the answer \"%s\", which this graph has no Return pin for"), *Out));

		return false;
	}

	if (Expression.Pin != nullptr) {
		Connect(Expression.Pin, Answer);
	} else if (!Expression.Literal.IsEmpty()) {
		ApplyLiteral(Answer, Expression.Literal);
	}

	if (!bCarryingOn) {
		ChainExecution(Node);

		Placed++;
	}

	AnsweredAt = Placing;
	AnsweredWith = Node;

	return true;
}

UK2Node_FunctionResult* FBytecodeGraph::NewReturn() {
	UK2Node_FunctionResult* Node = AddNode<UK2Node_FunctionResult>();

	if (Node->Pins.Num() == 0) {
		for (UEdGraphNode* Held : Graph->Nodes) {
			UK2Node_FunctionResult* Already = Cast<UK2Node_FunctionResult>(Held);

			if (Already == nullptr || Already == Node || Already->UserDefinedPins.Num() == 0) continue;

			Node->FunctionReference = Already->FunctionReference;

			for (const TSharedPtr<FUserPinInfo>& Described : Already->UserDefinedPins) {
				Node->UserDefinedPins.Add(MakeShared<FUserPinInfo>(*Described));
			}

			break;
		}

		Node->AllocateDefaultPins();
	}

	return Node;
}

void FBytecodeGraph::FindConstants() {
	if (bLookedForConstants) return;

	bLookedForConstants = true;

	TMap<FString, bool> Plain;

	TMap<FString, TSet<FString>> Feeds;

	for (const FUObjectJsonValueExport& Statement : Statements) {
		if (!MacroReading::IsLet(MacroReading::TokenOf(Statement))) continue;

		const FString Name = MacroReading::WrittenTo(Statement);

		if (Name.IsEmpty()) continue;

		Writes.FindOrAdd(Name)++;

		{
			TArray<FString> Reads;

			if (Statement.JsonObject.IsValid()) {
				NamesRead(Statement.JsonObject->TryGetField(TEXT("Expression")), Reads);
			}

			TSet<FString>& From = Feeds.FindOrAdd(Name);

			for (const FString& One : Reads) From.Add(One);
		}

		if (!Name.StartsWith(TEXT("Temp_"))) continue;

		const FString What = MacroReading::TokenOf(Statement.GetObject(TEXT("Expression")));

		const bool bSaidOutright = What.EndsWith(TEXT("Const"))
			|| What == TEXT("EX_True")
			|| What == TEXT("EX_False")
			|| What == TEXT("EX_IntZero")
			|| What == TEXT("EX_IntOne")
			|| What == TEXT("EX_NoObject")
			|| What == TEXT("EX_NoInterface");

		const bool bCopied = What == TEXT("EX_LocalVariable")
			|| What == TEXT("EX_InstanceVariable")
			|| What == TEXT("EX_DefaultVariable")
			|| What == TEXT("EX_LocalOutVariable")
			|| What == TEXT("EX_StructMemberContext");

		if (!Plain.Contains(Name)) Plain.Add(Name, true);

		bool& Holds = Plain[Name];

		Holds = Holds && (bSaidOutright || bCopied);
	}

	for (const FUObjectJsonValueExport& Statement : Statements) {
		const FString Token = MacroReading::TokenOf(Statement);

		if (Token != TEXT("EX_JumpIfNot") && Token != TEXT("EX_PopExecutionFlowIfNot")) continue;

		if (const FString Asked = MacroReading::ReadFrom(Statement.GetObject(TEXT("BooleanExpression"))); Asked.StartsWith(TEXT("Temp_"))) {
			TestedScratch.Add(Asked);
		}
	}

	for (const FUObjectJsonValueExport& Statement : Statements) {
		if (!Statement.JsonObject.IsValid()) continue;

		TArray<FString> Reads;

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Statement.JsonObject->Values) NamesRead(Field.Value, Reads);

		for (const FString& Read : Reads) {
			if (Read.StartsWith(TEXT("Temp_")) && !Writes.Contains(Read)) HeldScratch.Add(Read);
		}

		TArray<FString> Changed;

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Statement.JsonObject->Values) ArraysChanged(Field.Value, Changed);

		for (const FString& One : Changed) {
			if (One.StartsWith(TEXT("Temp_"))) HeldScratch.Add(One);
		}
	}

	for (const TPair<FString, bool>& Wrote : Plain) {
		if (!Wrote.Value) continue;

		bool bItself = false;

		TSet<FString> Seen;
		TArray<FString> Walk;

		if (const TSet<FString>* From = Feeds.Find(Wrote.Key)) {
			for (const FString& One : *From) Walk.Add(One);
		}

		while (Walk.Num() > 0) {
			const FString One = Walk.Pop();

			if (One == Wrote.Key) {
				bItself = true;

				break;
			}

			if (Seen.Contains(One)) continue;

			Seen.Add(One);

			if (const TSet<FString>* From = Feeds.Find(One)) {
				for (const FString& Next : *From) Walk.Add(Next);
			}
		}

		if (bItself) {
			Running.Add(Wrote.Key);

			UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\" is worked out from itself, so what keeps it running stays in the graph"), *Wrote.Key);

			continue;
		}

		Constants.Add(Wrote.Key);
	}
}

void FBytecodeGraph::FindMacros() {
	if (bLookedForMacros) return;

	bLookedForMacros = true;

	for (int32 Index = 0; Index < Statements.Num(); ++Index) {
		if (Skipped.Contains(Index)) continue;

		for (const TSharedRef<FMacroPattern>& Pattern : GetMacroPatterns()) {
			FMacroMatch Match;

			if (!Pattern->Match(Statements, Index, Match) || !Match.IsValid()) continue;

			bool bTaken = false;

			for (const int32 Inside : Match.Internal) {
				if (Skipped.Contains(Inside)) {
					bTaken = true;

					break;
				}
			}

			if (bTaken) continue;

			Match.Pattern = &Pattern.Get();

			Macros.Add(Match.First, Match);

			for (const int32 Inside : Match.Internal) {
				Skipped.Add(Inside);
			}

			for (const TPair<FString, FName>& Handout : Match.Handouts) {
				Owned.Add(Handout.Key);
			}

			for (const int32 Inside : Match.Internal) {
				if (!Statements.IsValidIndex(Inside)) continue;

				if (const FString Kept = MacroReading::WrittenTo(Statements[Inside]); !Kept.IsEmpty() && !Match.ByReference.Contains(Kept)) {
					Owned.Add(Kept);
				}
			}

			UE_LOG(LogReflectionBytecode, Display, TEXT("statements %d to %d are one %s"), Match.First, Match.Last, Pattern->GetName());

			break;
		}
	}
}

UEdGraphPin* FBytecodeGraph::MacroPin(UK2Node* Node, const FName Named, const EEdGraphPinDirection Direction) {
	if (Node == nullptr) return nullptr;

	if (UEdGraphPin* Exact = Node->FindPin(Named, Direction)) return Exact;

	const FString Wanted = Named.ToString().Replace(TEXT(" "), TEXT(""));

	for (UEdGraphPin* Pin : Node->Pins) {
		if (Pin == nullptr || Pin->Direction != Direction) continue;

		if (Pin->PinName.ToString().Replace(TEXT(" "), TEXT("")).Equals(Wanted, ESearchCase::IgnoreCase)) return Pin;
	}

	return nullptr;
}

void FBytecodeGraph::MakeMacros() {
	for (const TPair<int32, FMacroMatch>& Match : Macros) {
		UEdGraph* Definition = FindStandardMacro(Match.Value.Pattern->GetName());

		if (Definition == nullptr) {
			Unhandled.AddUnique(FString::Printf(TEXT("no %s macro to read back into"), Match.Value.Pattern->GetName()));

			continue;
		}

		UK2Node_MacroInstance* Node = AddNode<UK2Node_MacroInstance>();

		Node->SetMacroGraph(Definition);
		Node->AllocateDefaultPins();

		for (const TPair<FString, FName>& Handout : Match.Value.Handouts) {
			if (UEdGraphPin* Pin = MacroPin(Node, Handout.Value, EGPD_Output)) {
				Locals.Add(Handout.Key, Pin);
			}
		}

		for (const TPair<int32, FName>& Arriving : Match.Value.Takes) {
			UEdGraphPin* Pin = MacroPin(Node, Arriving.Value, EGPD_Input);

			if (Pin == nullptr || !Statements.IsValidIndex(Arriving.Key)) continue;

			Arrivals.Add(Arriving.Key, Pin);

			const int32 Address = MacroReading::AddressOf(Statements[Arriving.Key]);

			if (Address >= 0 && !Entries.Contains(Address)) Entries.Add(Address, Pin);
		}

		Written.Add(Match.Key, Node);
	}
}

bool FBytecodeGraph::PlaceMacro(const FMacroMatch& Match) {
	UK2Node_MacroInstance* Node = Written.FindRef(Match.First);

	if (Node == nullptr) return false;

	if (Statements.IsValidIndex(Match.First)) {
		const int32 Address = MacroReading::AddressOf(Statements[Match.First]);

		if (Address >= 0 && !Entries.Contains(Address)) {
			if (UEdGraphPin* In = WayIn(Node)) {
				Entries.Add(Address, In);
			}
		}
	}

	EnterNode(Node);

	for (const TPair<FName, FUObjectJsonValueExport>& Input : Match.Inputs) {
		if (UEdGraphPin* Pin = MacroPin(Node, Input.Key, EGPD_Input)) {
			const FValue Given = Read(Input.Value);

			if (Given.Pin != nullptr) Connect(Given.Pin, Pin);
			else if (!Given.Literal.IsEmpty()) ApplyLiteral(Pin, Given.Literal);
		}
	}

	Placed++;

	Open.Push(TTuple<const FMacroMatch*, UK2Node*>(&Match, Node));

	if (Match.Leads.Num() > 0) {
		for (const TPair<FName, int32>& Lead : Match.Leads) {
			if (UEdGraphPin* Pin = MacroPin(Node, Lead.Key, EGPD_Output)) {
				Jumps.Add(MakeTuple(Pin, Lead.Value));
			}
		}

		Flow = nullptr;

		return true;
	}

	if (Match.Bodies.Num() > 0) {
		Flow = MacroPin(Node, Match.Bodies[0].Key, EGPD_Output);
	}

	return true;
}

void FBytecodeGraph::Arrange() {
	if (Graph == nullptr) return;

	constexpr int32 Column = 420;
	constexpr int32 Row = 200;

	TSet<UEdGraphNode*> Settled;

	TSet<TPair<int32, int32>> Taken;

	int32 Deepest = 0;

	auto PutDown = [&Taken, &Deepest, Row](UEdGraphNode* Node, const int32 X, int32 Y) {
		while (Taken.Contains(TPair<int32, int32>(X, Y))) {
			Y += Row;
		}

		Taken.Add(TPair<int32, int32>(X, Y));

		Node->NodePosX = X;
		Node->NodePosY = Y;

		Deepest = FMath::Max(Deepest, Y);

		return Y;
	};

	TFunction<int32(UEdGraphNode*, int32, int32)> Feeders = [&](UEdGraphNode* Node, int32 X, int32 Y) {
		int32 Used = 0;

		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin == nullptr || Pin->Direction != EGPD_Input || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;

			for (UEdGraphPin* Linked : Pin->LinkedTo) {
				UEdGraphNode* Feeding = Linked != nullptr ? Linked->GetOwningNode() : nullptr;

				if (Feeding == nullptr || Settled.Contains(Feeding)) continue;

				Settled.Add(Feeding);

				const int32 Landed = PutDown(Feeding, X - Column, Y + Used * Row);

				Used += FMath::Max(1, Feeders(Feeding, X - Column, Landed));
			}
		}

		return Used;
	};

	TFunction<int32(UEdGraphNode*, TSet<UEdGraphNode*>&)> Needed = [&](UEdGraphNode* Node, TSet<UEdGraphNode*>& Asking) {
		if (Node == nullptr || Asking.Contains(Node)) return 0;

		Asking.Add(Node);

		int32 Need = 0;

		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin == nullptr || Pin->Direction != EGPD_Input || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;

			for (UEdGraphPin* Linked : Pin->LinkedTo) {
				UEdGraphNode* Feeding = Linked != nullptr ? Linked->GetOwningNode() : nullptr;

				if (Feeding == nullptr) continue;

				const int32 Sits = Settled.Contains(Feeding) ? Feeding->NodePosX : Needed(Feeding, Asking);

				Need = FMath::Max(Need, Sits + Column);
			}
		}

		Asking.Remove(Node);

		return Need;
	};

	int32 Across = 0;
	int32 Down = 0;

	int32 Base = 0;

	TFunction<void(UEdGraphNode*, int32)> Run = [&](UEdGraphNode* Node, int32 Line) {
		if (Node == nullptr || Settled.Contains(Node)) return;

		Settled.Add(Node);

		{
			TSet<UEdGraphNode*> Asking;

			Across = FMath::Max(Across, Needed(Node, Asking));
		}

		PutDown(Node, Across, Base + Line * Row * 3);

		Feeders(Node, Across, Node->NodePosY + Row);

		const int32 Here = Node->NodePosX;

		int32 Branch = 0;

		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin == nullptr || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;

			for (UEdGraphPin* Linked : Pin->LinkedTo) {
				if (Linked != nullptr) {
					Across = Here + Column;

					Run(Linked->GetOwningNode(), Line + Branch);
				}
			}

			Branch++;
		}
	};

	int32 Line = 0;

	TArray<UEdGraphNode*> Ways;

	for (UEdGraphNode* Node : Graph->Nodes) {
		if (Node != nullptr && (Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_Event>())) {
			Ways.Add(Node);
		}
	}

	for (const TPair<int32, TArray<TPair<TWeakObjectPtr<UK2Node>, FName>>>& Entered : Starts) {
		for (const TPair<TWeakObjectPtr<UK2Node>, FName>& One : Entered.Value) {
			if (One.Key.IsValid()) Ways.AddUnique(One.Key.Get());
		}
	}

	for (UEdGraphNode* Way : Ways) {
		if (Settled.Contains(Way)) continue;

		UEdGraphNode* Joins = nullptr;

		int32 Depth = 0;

		{
			TArray<TPair<UEdGraphNode*, int32>> Ahead;
			TSet<UEdGraphNode*> Seen;

			Ahead.Add(TPair<UEdGraphNode*, int32>(Way, 0));
			Seen.Add(Way);

			for (int32 At = 0; At < Ahead.Num() && Joins == nullptr; ++At) {
				for (UEdGraphPin* Pin : Ahead[At].Key->Pins) {
					if (Pin == nullptr || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;

					for (UEdGraphPin* Linked : Pin->LinkedTo) {
						UEdGraphNode* Next = Linked != nullptr ? Linked->GetOwningNode() : nullptr;

						if (Next == nullptr || Seen.Contains(Next)) continue;

						if (Settled.Contains(Next)) {
							Joins = Next;
							Depth = Ahead[At].Value + 1;

							break;
						}

						Seen.Add(Next);
						Ahead.Add(TPair<UEdGraphNode*, int32>(Next, Ahead[At].Value + 1));
					}

					if (Joins != nullptr) break;
				}
			}
		}

		if (Joins != nullptr) {
			Across = FMath::Max(0, Joins->NodePosX - Depth * Column);

			Base = Joins->NodePosY + Row;

			Run(Way, 0);

			Base = 0;

			continue;
		}

		Across = 0;

		Run(Way, Line);

		Line = Deepest / (Row * 3) + 2;
	}

	auto HeaderOf = [Row](UEdGraphNode* Node) {

		#if !PLATFORM_LINUX
		if (const UK2Node* Written = Cast<UK2Node>(Node); Written != nullptr && Written->ShouldDrawCompact()) {
			return 0;
		}
		#else
		if (const UK2Node* _Written = Cast<UK2Node>(Node); _Written != nullptr && _Written->ShouldDrawCompact()) {
			return 0;
		}
		#endif

		const FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

		int32 Lines = 1;

		for (const TCHAR Letter : Title) {
			if (Letter == TEXT('\n')) Lines++;
		}

		return (Row / 12) * (Lines + 1);
	};

	TSet<UEdGraphNode*> Levelled;

	for (int32 At = 0; At < Ways.Num(); ++At) {
		UEdGraphNode* From = Ways[At];

		for (UEdGraphPin* Pin : From->Pins) {
			if (Pin == nullptr || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;

			for (UEdGraphPin* Linked : Pin->LinkedTo) {
				UEdGraphNode* To = Linked != nullptr ? Linked->GetOwningNode() : nullptr;

				if (To == nullptr || To == From || Levelled.Contains(To)) continue;

				Levelled.Add(To);
				Ways.Add(To);

				if (To->NodePosY != From->NodePosY) continue;

				To->NodePosY = From->NodePosY + HeaderOf(From) - HeaderOf(To);
			}
		}
	}

	Down = 0;

	for (UEdGraphNode* Node : Graph->Nodes) {
		if (Node == nullptr || Settled.Contains(Node)) continue;

		Settled.Add(Node);

		PutDown(Node, 0, Row * 3 * (6 + Down++));

		Feeders(Node, 0, Node->NodePosY + Row);
	}
}

void FBytecodeGraph::Clear() {
	if (Graph == nullptr) return;

	for (int32 Index = Graph->Nodes.Num() - 1; Index >= 0; --Index) {
		UEdGraphNode* Node = Graph->Nodes[Index];

		if (Node == nullptr) continue;

		if (Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_FunctionResult>() || Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_Timeline>()) {
			Node->BreakAllNodeLinks();

			continue;
		}

		if (!Node->CanUserDeleteNode()) {
			Node->BreakAllNodeLinks();

			continue;
		}

		Graph->RemoveNode(Node);
	}
}

int32 FBytecodeGraph::Build() {
	if (Graph == nullptr) return 0;

	if (bTidy) {
		FindMacros();
	}

	Clear();

	for (UEdGraphNode* Node : Graph->Nodes) {
		if (Node == nullptr || !Node->IsA<UK2Node_FunctionEntry>()) continue;

		const bool bKept = HoldsLocals();

		for (UEdGraphPin* Pin : Node->Pins) {
			if (Pin == nullptr || Pin->Direction != EGPD_Output) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;

			Locals.Add(Pin->PinName.ToString(), Pin);

			if (bKept) Handouts.Add(Pin->PinName.ToString());
		}
	}

	for (const TPair<FString, TPair<TWeakObjectPtr<UK2Node>, FName>>& Track : Tracks) {
		if (!Track.Value.Key.IsValid()) continue;

		if (UEdGraphPin* Pin = Track.Value.Key->FindPin(Track.Value.Value, EGPD_Output)) {
			Locals.Add(Track.Key, Pin);
		}
	}

	for (const TPair<FString, TPair<TWeakObjectPtr<UK2Node>, FString>>& Given : Handed) {
		if (!Given.Value.Key.IsValid()) continue;

		if (UEdGraphPin* Pin = Given.Value.Key->FindPin(*Given.Value.Value, EGPD_Output)) {
			Locals.Add(Given.Key, Pin);
		}
	}

	if (Starts.Num() == 0) {
		for (UEdGraphNode* Node : Graph->Nodes) {
			if (UEdGraphPin* Then = Node->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output)) {
				Flow = Then;

				UE_LOG(LogReflectionBytecode, Display, TEXT("starting the run from \"%s\""), *Node->GetName());

				break;
			}
		}
	}

	if (ElsewhereAt.Num() > 0) {
		Ignored.Append(ElsewhereAt);

		for (TMap<int32, FMacroMatch>::TIterator It(Macros); It; ++It) {
			const int32 At = Statements.IsValidIndex(It.Key()) ? MacroReading::AddressOf(Statements[It.Key()]) : INDEX_NONE;

			if (At >= 0 && ElsewhereAt.Contains(At)) It.RemoveCurrent();
		}
	}

	if (Stretch.Num() > 0 || Elsewhere.Num() > 0) {
		const auto Inside = [](const TArray<TPair<int32, int32>>& Stretches, const int32 At) {
			for (const TPair<int32, int32>& One : Stretches) {
				if (At >= One.Key && At <= One.Value) return true;
			}

			return false;
		};

		const auto Ours = [&Inside, this](const int32 At) {
			if (At < 0) return true;
			if (Stretch.Num() > 0 && !Inside(Stretch, At)) return false;

			return !Inside(Elsewhere, At);
		};

		for (const FUObjectJsonValueExport& One : Statements) {
			const int32 At = MacroReading::AddressOf(One);

			if (At >= 0 && !Ours(At)) Ignored.Add(At);
		}

		for (TMap<int32, FMacroMatch>::TIterator It(Macros); It; ++It) {
			const int32 At = Statements.IsValidIndex(It.Key()) ? MacroReading::AddressOf(Statements[It.Key()]) : INDEX_NONE;

			if (!Ours(At)) It.RemoveCurrent();
		}
	}

	FindConstants();
	FindAnswers();
	FindPureCasts();

	MakeMacros();

	if (Statements.Num() > 0 && MacroReading::TokenOf(Statements[0]) == TEXT("EX_PushExecutionFlow")) {
		const int32 Ends = MacroReading::IndexOfAddress(Statements, Statements[0].GetInteger(TEXT("PushingAddress"), INDEX_NONE));

		if (Statements.IsValidIndex(Ends) && MacroReading::TokenOf(Statements[Ends]) == TEXT("EX_Return")) {
			Ignored.Add(MacroReading::AddressOf(Statements[0]));
		}
	}

	for (int32 Index = 0; Index < Statements.Num(); ++Index) {
		if (MacroReading::TokenOf(Statements[Index]) != TEXT("EX_ComputedJump")) continue;

		Ignored.Add(MacroReading::AddressOf(Statements[Index]));

		if (Index > 0 && MacroReading::TokenOf(Statements[Index - 1]) == TEXT("EX_PushExecutionFlow")) {
			Ignored.Add(MacroReading::AddressOf(Statements[Index - 1]));
		}
	}

	for (int32 Index = 0; Index < Statements.Num(); ++Index) {
		const FUObjectJsonValueExport& Statement = Statements[Index];
		const FString Token = MacroReading::TokenOf(Statement);

		const FString Called = Statement.Has(TEXT("Function")) && Statement.JsonObject->HasTypedField<EJson::Object>(TEXT("Function"))
			? Statement.GetObject(TEXT("Function")).GetString(TEXT("ObjectName"))
			: FString();

		if (Token == TEXT("EX_CallMath") && Called.EndsWith(TEXT(":MarkPropertyDirtyFromRepIndex'"))) {
			Ignored.Add(MacroReading::AddressOf(Statement));

			continue;
		}

		const bool bFlushesOther = (Token == TEXT("EX_Context") || Token == TEXT("EX_Context_FailSilent"))
			&& Statement.GetObject(TEXT("ContextExpression")).Has(TEXT("Function"))
			&& Statement.GetObject(TEXT("ContextExpression")).JsonObject->HasTypedField<EJson::Object>(TEXT("Function"))
			&& Statement.GetObject(TEXT("ContextExpression")).GetObject(TEXT("Function")).GetString(TEXT("ObjectName")).EndsWith(TEXT("Actor:FlushNetDormancy'"));

		if (bFlushesOther) {
			const FString Flushed = MacroReading::NamedProperty(Statement.GetObject(TEXT("ObjectExpression")).GetObject(TEXT("Variable")));

			for (int32 Ahead = Index + 1; Statements.IsValidIndex(Ahead); ++Ahead) {
				const FUObjectJsonValueExport& Next = Statements[Ahead];
				const FString Going = MacroReading::TokenOf(Next);

				if (Going == TEXT("EX_CallMath")) continue;

				if ((Going == TEXT("EX_Context") || Going == TEXT("EX_Context_FailSilent"))
					&& MacroReading::TokenOf(Next.GetObject(TEXT("ObjectExpression"))) == TEXT("EX_ObjectConst")
					&& Next.GetObject(TEXT("ObjectExpression")).GetObject(TEXT("Value")).GetString(TEXT("ObjectName")).Contains(DEFAULT_OBJECT_PREFIX)) continue;

				if (!Going.StartsWith(TEXT("EX_Let")) || !Next.Has(TEXT("Variable"))) break;

				const FUObjectJsonValueExport Target = Next.GetObject(TEXT("Variable"));
				const FString Into = MacroReading::TokenOf(Target);

				if (Into == TEXT("EX_LocalVariable")) continue;

				if (Into != TEXT("EX_Context") && Into != TEXT("EX_Context_FailSilent")) break;

				if (MacroReading::NamedProperty(Target.GetObject(TEXT("ObjectExpression")).GetObject(TEXT("Variable"))) != Flushed) break;

				const FUObjectJsonValueExport Member = Target.GetObject(TEXT("ContextExpression")).GetObject(TEXT("Variable"));
				const FString Name = MacroReading::NamedProperty(Member);

				FString Owner, Unused;
				if (Member.Has(TEXT("ResolvedOwner"))) {
					SplitReference(Member.GetObject(TEXT("ResolvedOwner")), Owner, Unused);
				}

				const UClass* On = Owner.IsEmpty() ? nullptr : FindClassByType(Owner);
				const FProperty* WrittenMember = On != nullptr && !Name.IsEmpty() ? On->FindPropertyByName(*Name) : nullptr;

				if (WrittenMember != nullptr && WrittenMember->HasAnyPropertyFlags(CPF_Net)) {
					Ignored.Add(MacroReading::AddressOf(Statement));
				}

				break;
			}

			continue;
		}

		if (Token != TEXT("EX_FinalFunction") || !Called.EndsWith(TEXT("Actor:FlushNetDormancy'"))) continue;

		for (int32 Ahead = Index + 1; Statements.IsValidIndex(Ahead); ++Ahead) {
			const FUObjectJsonValueExport& Next = Statements[Ahead];
			const FString Going = MacroReading::TokenOf(Next);

			if (Going == TEXT("EX_CallMath")) continue;

			if ((Going == TEXT("EX_Context") || Going == TEXT("EX_Context_FailSilent"))
				&& MacroReading::TokenOf(Next.GetObject(TEXT("ObjectExpression"))) == TEXT("EX_ObjectConst")
				&& Next.GetObject(TEXT("ObjectExpression")).GetObject(TEXT("Value")).GetString(TEXT("ObjectName")).Contains(DEFAULT_OBJECT_PREFIX)) continue;

			if (!Going.StartsWith(TEXT("EX_Let")) || !Next.Has(TEXT("Variable"))) break;

			const FUObjectJsonValueExport Target = Next.GetObject(TEXT("Variable"));
			const FString Into = MacroReading::TokenOf(Target);

			if (Into == TEXT("EX_LocalVariable")) continue;

			if (Into == TEXT("EX_InstanceVariable") && Target.Has(TEXT("Variable"))) {
				const FUObjectJsonValueExport Held = Target.GetObject(TEXT("Variable"));

				const FUObjectJsonValueExport Assigned = Held.Has(TEXT("Property")) ? Held.GetObject(TEXT("Property")) : Held;

				bool bReplicated = Assigned.Has(TEXT("RepIndex"));

				if (!bReplicated && Assigned.Has(TEXT("PropertyFlags"))) {
					TArray<FString> Flags;
					Assigned.GetString(TEXT("PropertyFlags")).ParseIntoArray(Flags, TEXT(" | "));

					bReplicated = Flags.Contains(TEXT("Net"));
				}

				if (bReplicated) {
					Ignored.Add(MacroReading::AddressOf(Statement));
				}
			}

			break;
		}
	}

	{
		auto NotifyOfSet = [](const FUObjectJsonValueExport& Statement) -> FString {
			if (!MacroReading::TokenOf(Statement).StartsWith(TEXT("EX_Let")) || !Statement.Has(TEXT("Variable"))) return FString();

			const FUObjectJsonValueExport Target = Statement.GetObject(TEXT("Variable"));

			if (MacroReading::TokenOf(Target) != TEXT("EX_InstanceVariable") || !Target.Has(TEXT("Variable"))) return FString();

			const FUObjectJsonValueExport Held = Target.GetObject(TEXT("Variable"));
			const FUObjectJsonValueExport Assigned = Held.Has(TEXT("Property")) ? Held.GetObject(TEXT("Property")) : Held;

			const FString Notify = Assigned.Has(TEXT("RepNotifyFunc")) ? Assigned.GetString(TEXT("RepNotifyFunc")) : FString();

			return Notify == TEXT("None") ? FString() : Notify;
		};

		auto CalledOnSelf = [](const FUObjectJsonValueExport& Statement) -> FString {
			const FString Going = MacroReading::TokenOf(Statement);

			if (Going != TEXT("EX_LocalFinalFunction") && Going != TEXT("EX_LocalVirtualFunction") && Going != TEXT("EX_FinalFunction") && Going != TEXT("EX_VirtualFunction")) return FString();

			FString Calls;

			if (Statement.Has(TEXT("Function")) && Statement.JsonObject->HasTypedField<EJson::Object>(TEXT("Function"))) {
				Calls = Statement.GetObject(TEXT("Function")).GetString(TEXT("ObjectName"));
			} else if (Statement.Has(TEXT("Function"))) {
				Calls = Statement.GetString(TEXT("Function"));
			} else if (Statement.Has(TEXT("VirtualFunctionName"))) {
				Calls = Statement.GetString(TEXT("VirtualFunctionName"));
			}

			Calls.RemoveFromEnd(TEXT("'"));

			int32 Colon = INDEX_NONE;
			if (Calls.FindLastChar(TEXT(':'), Colon)) {
				Calls = Calls.RightChop(Colon + 1);
			}

			return Calls;
		};

		TMap<int32, int32> Ways;
		TMap<int32, int32> FromSets;

		for (int32 Index = 0; Index < Statements.Num(); ++Index) {
			const FUObjectJsonValueExport& Statement = Statements[Index];
			const FString Token = MacroReading::TokenOf(Statement);

			const bool bFallsThrough = Token != TEXT("EX_Jump") && Token != TEXT("EX_Return") && Token != TEXT("EX_PopExecutionFlow") && Token != TEXT("EX_ComputedJump") && Token != TEXT("EX_EndOfScript");

			if (bFallsThrough && Statements.IsValidIndex(Index + 1)) {
				const FString Callee = CalledOnSelf(Statements[Index + 1]);

				if (!Callee.IsEmpty()) {
					Ways.FindOrAdd(Index + 1)++;

					if (NotifyOfSet(Statement) == Callee) FromSets.FindOrAdd(Index + 1)++;
				}
			}

			for (const TCHAR* Field : { TEXT("CodeOffset"), TEXT("PushingAddress") }) {
				if (!Statement.Has(Field)) continue;

				const int32 To = MacroReading::IndexOfAddress(Statements, Statement.GetInteger(Field, -1));

				if (!Statements.IsValidIndex(To)) continue;

				const FString Callee = CalledOnSelf(Statements[To]);

				if (Callee.IsEmpty()) continue;

				Ways.FindOrAdd(To)++;

				if (Token == TEXT("EX_Jump") && Index > 0 && NotifyOfSet(Statements[Index - 1]) == Callee) FromSets.FindOrAdd(To)++;
			}
		}

		for (const TPair<int32, int32>& Way : Ways) {
			const int32 Address = MacroReading::AddressOf(Statements[Way.Key]);

			if (Starts.Contains(Address)) continue;

			if (Way.Value > 0 && FromSets.FindRef(Way.Key) == Way.Value) {
				Ignored.Add(Address);
			}
		}
	}

	for (int32 Index = 0; Index < Statements.Num(); ++Index) {
		const FUObjectJsonValueExport& Statement = Statements[Index];

		const int32 Address = Statement.GetInteger(TEXT("StatementIndex"), -1);

		if (const TArray<TWeakObjectPtr<UK2Node>>* Here = Sitting.Find(Address)) {
			for (const TWeakObjectPtr<UK2Node>& Event : *Here) {
				if (Event.IsValid() && Graph->Nodes.Remove(Event.Get()) > 0) {
					Graph->Nodes.Add(Event.Get());
				}
			}
		}

		const int32 Before = Graph->Nodes.Num();

		Placing = Index;

		if (const TArray<TPair<TWeakObjectPtr<UK2Node>, FName>>* Entered = Starts.Find(Address)) {
			Flow = nullptr;

			for (const TPair<TWeakObjectPtr<UK2Node>, FName>& One : *Entered) {
				if (!One.Key.IsValid()) continue;

				UEdGraphPin* Out = One.Key->FindPin(One.Value.IsNone() ? UEdGraphSchema_K2::PN_Then : One.Value, EGPD_Output);

				if (Out == nullptr) continue;

				if (Flow == nullptr) {
					Flow = Out;
				} else {
					Jumps.Add(MakeTuple(Out, Address));
				}

				UE_LOG(LogReflectionBytecode, Display, TEXT("entering at %d, through \"%s\""), Address, *One.Key->GetName());
			}
		}

		const int32 WasChained = Chained;

		if (Ignored.Contains(Address)) {
		} else if (const FMacroMatch* Match = Macros.Find(Index)) {
			PlaceMacro(*Match);
		} else if (!Skipped.Contains(Index)) {
			Place(Statement);
		} else {
			if (UEdGraphPin* const* Arriving = Arrivals.Find(Index)) {
				if (Flow != nullptr) LinkExecution(Flow, *Arriving);

				Flow = nullptr;
			}

			const FString Token = MacroReading::TokenOf(Statement);

			if (Token == TEXT("EX_Jump") || Token == TEXT("EX_PopExecutionFlow")) {
				Flow = nullptr;
			}
		}

		for (const TPair<const FMacroMatch*, UK2Node*>& Waiting : Open) {
			for (const TPair<FName, FUObjectJsonValueExport>& Late : Waiting.Key->Deferred) {
				if (Index != Waiting.Key->Last) continue;

				if (UEdGraphPin* Pin = MacroPin(Waiting.Value, Late.Key, EGPD_Input)) {
					if (Pin->LinkedTo.Num() == 0 && Pin->DefaultValue.IsEmpty()) {
						const FValue Given = Read(Late.Value);

						if (Given.Pin != nullptr) Connect(Given.Pin, Pin);
						else if (!Given.Literal.IsEmpty()) ApplyLiteral(Pin, Given.Literal);
					}
				}
			}
		}

		while (Open.Num() > 0 && Index >= Open.Last().Key->Last) {
			const TPair<const FMacroMatch*, UK2Node*> Done = Open.Pop();

			if (Done.Key->Leads.Num() > 0) continue;

			Flow = MacroPin(Done.Value, TEXT("Completed"), EGPD_Output);

			if (Flow == nullptr && Done.Key->Bodies.Num() > 0) {
				Flow = MacroPin(Done.Value, Done.Key->Bodies.Last().Key, EGPD_Output);
			}
		}

		if (Chained > WasChained && Address >= 0) {
			LastChainedAddress = Address;
		}

		if (Address >= 0 && !Entries.Contains(Address)) {
			for (int32 Laid = Before; Laid < Graph->Nodes.Num(); ++Laid) {
				if (UEdGraphPin* In = WayIn(Graph->Nodes[Laid])) {
					Entries.Add(Address, In);

					break;
				}
			}
		}
	}

	for (const TPair<UEdGraphPin*, FString>& One : Later) {
		if (One.Key == nullptr || One.Key->LinkedTo.Num() > 0) continue;

		if (UEdGraphPin** Held = Locals.Find(One.Value); Held != nullptr && *Held != nullptr) {
			Connect(*Held, One.Key);
		}
	}

	for (const TPair<UEdGraphPin*, int32>& Jump : Jumps) {
		if (Jump.Key == nullptr) continue;

		UEdGraphPin* Entry = nullptr;

		bool bEnds = false;

		int32 Address = Jump.Value;
		TSet<int32> Walked;

		while (Address >= 0 && !Walked.Contains(Address)) {
			Walked.Add(Address);

			if (UEdGraphPin** Begins = Entries.Find(Address)) {
				Entry = *Begins;

				break;
			}

			const int32 At = MacroReading::IndexOfAddress(Statements, Address);

			if (At == INDEX_NONE) break;

			if (Skipped.Contains(At)) {
				bEnds = true;

				break;
			}

			const FString Token = MacroReading::TokenOf(Statements[At]);

			if (Token == TEXT("EX_Jump")) {
				Address = Statements[At].GetInteger(TEXT("CodeOffset"), -1);

				continue;
			}

			if (Token == TEXT("EX_PopExecutionFlow") || Token == TEXT("EX_Return") || Token == TEXT("EX_EndOfScript")) {
				bEnds = true;

				break;
			}

			if (!Statements.IsValidIndex(At + 1)) break;

			Address = MacroReading::AddressOf(Statements[At + 1]);
		}

		if (Entry != nullptr) {
			LinkExecution(Jump.Key, Entry);
		} else if (!bEnds) {
			Unhandled.AddUnique(FString::Printf(TEXT("jump to %d, which nothing begins"), Jump.Value));
		}
	}

	if (bTidy) {
		FString Skipping;
		FParse::Value(FCommandLine::Get(), TEXT("skiptidy="), Skipping, false);

		TArray<FString> LeftOut;
		Skipping.ParseIntoArray(LeftOut, TEXT(","));

		for (const TSharedRef<FGraphTidy>& Tidy : GetGraphTidies()) {
			if (LeftOut.Contains(Tidy->GetName())) continue;

			Tidy->Apply(Graph);
		}
	}

	Arrange();

	if (Start != nullptr) {
		for (UEdGraphNode* Node : Graph->Nodes) {
			if (Node == nullptr || !Node->IsA<UK2Node_FunctionEntry>()) continue;

			if (UEdGraphPin* Then = Node->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output)) {
				LinkExecution(Then, Start);
			}

			break;
		}
	}

	for (UEdGraphNode* Node : Graph->Nodes) {
		if (Node == nullptr || !Node->IsA<UK2Node_FunctionEntry>()) continue;

		UEdGraphPin* Then = Node->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);

		UE_LOG(LogReflectionBytecode, Display, TEXT("entry \"%s\" carries %d link(s) out of it"), *Node->GetName(), Then != nullptr ? Then->LinkedTo.Num() : -1);
	}

	int32 Idle = 0;

	for (bool bWent = true; bWent; ) {
		bWent = false;

		for (int32 At = Graph->Nodes.Num() - 1; At >= 0; --At) {
			UEdGraphNode* One = Graph->Nodes[At];

			if (One == nullptr || !One->CanUserDeleteNode()) continue;
			if (One->IsA<UK2Node_Event>() || One->IsA<UK2Node_FunctionEntry>() || One->IsA<UK2Node_FunctionResult>() || One->IsA<UK2Node_Timeline>()) continue;

			bool bRuns = false;
			bool bRead = false;

			for (const UEdGraphPin* Pin : One->Pins) {
				if (Pin == nullptr) continue;

				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) bRuns = true;
				if (Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0) bRead = true;
			}

			if (bRuns || bRead) continue;

			Graph->RemoveNode(One);

			Idle++;

			bWent = true;
		}
	}

	if (Idle > 0) {
		UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\": %d node(s) worked something out for nobody, and were left out"), *Graph->GetName(), Idle);
	}

	{
		TArray<UK2Node_FunctionResult*> Answering;

		for (UEdGraphNode* Node : Graph->Nodes) {
			if (UK2Node_FunctionResult* Out = Cast<UK2Node_FunctionResult>(Node)) Answering.Add(Out);
		}

		const auto bReached = [](const UK2Node_FunctionResult* Out) {
			for (const UEdGraphPin* Pin : Out->Pins) {
				if (Pin == nullptr || Pin->Direction != EGPD_Input) continue;
				if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;

				if (Pin->LinkedTo.Num() > 0) return true;
			}

			return false;
		};

		int32 Kept = 0;

		for (const UK2Node_FunctionResult* Out : Answering) {
			if (bReached(Out)) Kept++;
		}

		int32 Dropped = 0;

		for (UK2Node_FunctionResult* Out : Answering) {
			if (bReached(Out)) continue;

			if (Kept == 0) {
				Kept++;

				continue;
			}

			Graph->RemoveNode(Out);

			Dropped++;
		}

		if (Dropped > 0) {
			UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\": %d way(s) out that nothing reaches, and were left out"), *Graph->GetName(), Dropped);
		}
	}

	UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\": entered from %s, %d node(s) chained, %d left out of the run"), *Graph->GetName(), Flow != nullptr ? TEXT("a pin") : TEXT("nothing"), Chained, Orphaned);

	if (Contested.Num() > 0) {
		UE_LOG(LogReflectionBytecode, Warning, TEXT("%d way(s) out were already spoken for: %s"), Contested.Num(), *FString::Join(Contested, TEXT(", ")));
	}

	if (Unhandled.Num() > 0) {
		UE_LOG(LogReflectionBytecode, Warning, TEXT("\"%s\" placed %d node(s), and read nothing from: %s"), *Graph->GetName(), Placed, *FString::Join(Unhandled, TEXT(", ")));

		FImportIssues::Report(
			EImportIssue::Data,
			FString::Printf(TEXT("Part of \"%s\" could not be read back"), *Graph->GetName()),
			FString::Printf(TEXT("%d node(s) were laid out. Nothing was read from: %s"), Placed, *FString::Join(Unhandled, TEXT(", ")))
		);
	} else {
		UE_LOG(LogReflectionBytecode, Display, TEXT("\"%s\" placed %d node(s) from %d statement(s)"), *Graph->GetName(), Placed, Statements.Num());
	}

	return Placed;
}
