#include "Engine/SharedPtrCompatibility.h"
#include "Serializers/Structs/NiagaraVariableSerializer.h"

#include "Engine/Compatibility.h"

#include "NiagaraTypes.h"

namespace {
	bool ReadType(const TSharedPtr<FJsonObject>& Entry, FNiagaraTypeDefinition& OutType) {
		const TSharedPtr<FJsonObject>* TypeDef;

		if (!Entry->TryGetObjectField(TEXT("TypeDef"), TypeDef)) return false;

		const TSharedPtr<FJsonObject>* Named;

		if (!(*TypeDef)->TryGetObjectField(TEXT("ClassStructOrEnum"), Named)) return false;

		FString ObjectName;

		if (!(*Named)->TryGetStringField(TEXT("ObjectName"), ObjectName)) return false;

		FString Leaf = ObjectName;

		if (Leaf.Contains(TEXT("'"))) {
			Leaf.Split(TEXT("'"), nullptr, &Leaf);

			Leaf = Leaf.Replace(TEXT("'"), TEXT(""));
		}

		if (Leaf.IsEmpty()) return false;

		FString Module;

		(*Named)->TryGetStringField(TEXT("ObjectPath"), Module);

		const FString Full = Module.IsEmpty() ? FString() : Module + TEXT(".") + Leaf;

#if UE4_24_BELOW
		constexpr int32 UnderlyingStruct = 1;
		constexpr int32 UnderlyingEnum = 2;
		constexpr int32 UnderlyingClass = 3;
#else
		constexpr int32 UnderlyingStruct = FNiagaraTypeDefinition::UT_Struct;
		constexpr int32 UnderlyingEnum = FNiagaraTypeDefinition::UT_Enum;
		constexpr int32 UnderlyingClass = FNiagaraTypeDefinition::UT_Class;
#endif

		int32 Kind = 0;

		(*TypeDef)->TryGetNumberField(TEXT("UnderlyingType"), Kind);

		if (Kind == UnderlyingStruct) {
			UScriptStruct* Struct = Full.IsEmpty() ? nullptr : FindObject<UScriptStruct>(nullptr, *Full);

			if (Struct == nullptr) Struct = FindStructByType(Leaf);
			if (Struct == nullptr) return false;

#if ENGINE_UE5
			OutType = FNiagaraTypeDefinition(Struct, FNiagaraTypeDefinition::EAllowUnfriendlyStruct::Allow);
#else
			OutType = FNiagaraTypeDefinition(Struct);
#endif
		} else if (Kind == UnderlyingEnum) {
			UEnum* Enum = Full.IsEmpty() ? nullptr : FindObject<UEnum>(nullptr, *Full);

			if (Enum == nullptr) Enum = FindEnumByType(Leaf);
			if (Enum == nullptr) return false;

			OutType = FNiagaraTypeDefinition(Enum);
		} else if (Kind == UnderlyingClass) {
			UClass* Class = Full.IsEmpty() ? nullptr : FindObject<UClass>(nullptr, *Full);

			if (Class == nullptr) Class = FindClassByType(Leaf);
			if (Class == nullptr) return false;

			OutType = FNiagaraTypeDefinition(Class);
		} else return false;

#if ENGINE_UE5
		if (int32 Flags = 0; (*TypeDef)->TryGetNumberField(TEXT("Flags"), Flags) && Flags != 0) {
			OutType.SetFlags(static_cast<FNiagaraTypeDefinition::FTypeFlags>(Flags));
		}
#endif

		return true;
	}
}

void FNiagaraVariableSerializer::Deserialize(UScriptStruct* Struct, void* StructData, const TSharedPtr<FJsonObject> JsonValue, UObject* OptionalOuter) {
	Fallback.Deserialize(Struct, StructData, JsonValue, OptionalOuter);

	if (Struct == nullptr || StructData == nullptr || !JsonValue.IsValid()) return;

	if (FNiagaraTypeDefinition Type; ReadType(JsonValue, Type)) {
#if UE4_24_BELOW
		static_cast<FNiagaraVariable*>(StructData)->SetType(Type);
#else
		static_cast<FNiagaraVariableBase*>(StructData)->SetType(Type);
#endif
	}

	if (Struct->IsChildOf(FNiagaraVariable::StaticStruct())) {
		static_cast<FNiagaraVariable*>(StructData)->AllocateData();
	}
}
