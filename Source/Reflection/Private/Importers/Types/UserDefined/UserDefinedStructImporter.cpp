#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/UserDefined/UserDefinedStructImporter.h"

#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UObject/StructOnScope.h"
#include "Utilities/JsonHelpers.h"
#include "Internationalization/Regex.h"

static const FRegexPattern PropertyNameRegexPattern(TEXT(R"((.*)_(\d+)_([0-9A-Z]+))"));

static const TMap<FString, const FName> PropertyCategoryMap = {
    {TEXT("BoolProperty"), TEXT("bool")},
    {TEXT("ByteProperty"), TEXT("byte")},
    {TEXT("IntProperty"), TEXT("int")},
    {TEXT("Int64Property"), TEXT("int64")},
    {TEXT("FloatProperty"), TEXT("real")},
    {TEXT("DoubleProperty"), TEXT("real")},
    {TEXT("StrProperty"), TEXT("string")},
    {TEXT("TextProperty"), TEXT("text")},
    {TEXT("NameProperty"), TEXT("name")},
    {TEXT("ClassProperty"), TEXT("class")},
    {TEXT("SoftClassProperty"), TEXT("softclass")},
    {TEXT("ObjectProperty"), TEXT("object")},
    {TEXT("SoftObjectProperty"), TEXT("softobject")},
    {TEXT("EnumProperty"), TEXT("byte")},
    {TEXT("StructProperty"), TEXT("struct")},
};

static const TMap<FString, EPinContainerType> ContainerTypeMap = {
    {TEXT("ArrayProperty"), EPinContainerType::Array},
    {TEXT("MapProperty"), EPinContainerType::Map},
    {TEXT("SetProperty"), EPinContainerType::Set},
};

UObject* IUserDefinedStructImporter::CreateAsset(UObject* CreatedAsset) {
    UUserDefinedStruct* UserDefinedStruct = NewObject<UUserDefinedStruct>(GetPackage(), *GetAssetName(), RF_Standalone | RF_Public | RF_Transactional);

    UserDefinedStruct->EditorData = NewObject<UUserDefinedStructEditorData>(UserDefinedStruct, NAME_None, RF_Transactional);
    UserDefinedStruct->Guid = FGuid::NewGuid();
    UserDefinedStruct->SetMetaData(TEXT("BlueprintType"), TEXT("true"));
    UserDefinedStruct->Bind();
    UserDefinedStruct->StaticLink(true);
    UserDefinedStruct->Status = UDSS_Error;

    return IImporter::CreateAsset(UserDefinedStruct);
}

bool IUserDefinedStructImporter::Import() {
    UUserDefinedStruct* UserDefinedStruct = Create<UUserDefinedStruct>();

    if (UserDefinedStruct == nullptr) {
        return false;
    }

    if (GetAssetData()->HasTypedField<EJson::Object>(TEXT("DefaultProperties"))) {
        DefaultProperties = GetAssetData()->GetObjectField(TEXT("DefaultProperties"));
    } else {
        DefaultProperties = MakeShared<FJsonObject>();
    }

    GetObjectSerializer()->DeserializeObjectProperties(KeepPropertiesShared(GetAssetData(),
    {
        "Guid",
        "StructFlags"
    }), UserDefinedStruct);

    TMap<FName, FString> StructMetaData;
    TMap<FString, FMemberMetaData> MemberMetaData;

    ReadCookedMetaData(StructMetaData, MemberMetaData);

    for (const TPair<FName, FString>& Entry : StructMetaData) {
        UserDefinedStruct->SetMetaData(Entry.Key, *Entry.Value);

        if (Entry.Key == TEXT("Tooltip")) {
            FStructureEditorUtils::ChangeTooltip(UserDefinedStruct, Entry.Value);
        }
    }

    FStructureEditorUtils::ModifyStructData(UserDefinedStruct);

    TArray<TSharedPtr<FJsonObject>> Members;

    for (const TSharedPtr<FJsonObject>& Held : { GetAssetExport(), GetAssetData() }) {
        if (!Held.IsValid() || !Held->HasTypedField<EJson::Array>(TEXT("ChildProperties"))) continue;

        for (const TSharedPtr<FJsonValue>& Property : Held->GetArrayField(TEXT("ChildProperties"))) {
            if (const TSharedPtr<FJsonObject> Described = Property->AsObject(); Described.IsValid()) {
                Members.Add(Described);
            }
        }

        break;
    }

    if (Members.Num() == 0 && GetAssetExport().IsValid() && GetAssetExport()->HasTypedField<EJson::Array>(TEXT("Children"))) {
        for (const TSharedPtr<FJsonValue>& Named : GetAssetExport()->GetArrayField(TEXT("Children"))) {
            const FUObjectExport* Held = GetContainer()->GetExportByObjectPath(Named->AsObject());

            if (Held == nullptr || !Held->IsJsonValid()) continue;

            Members.Add(Held->JsonObject);
        }
    }

    for (const TSharedPtr<FJsonObject>& Described : Members) {
        AddMemberToStruct(UserDefinedStruct, Described, MemberMetaData);
    }

    FStructureEditorUtils::OnStructureChanged(UserDefinedStruct, FStructureEditorUtils::EStructureEditorChangeInfo::AddedVariable);

    ApplyDefaultValues(UserDefinedStruct);

    FStructureEditorUtils::OnStructureChanged(UserDefinedStruct, FStructureEditorUtils::EStructureEditorChangeInfo::DefaultValueChanged);

    return OnAssetCreation(UserDefinedStruct);
}

void IUserDefinedStructImporter::ReadCookedMetaData(TMap<FName, FString>& OutStructMetaData, TMap<FString, FMemberMetaData>& OutMemberMetaData) const {
    const FUObjectExport* CookedStructMetaData = GetContainer()->FindByType(FString("StructCookedMetaData"));

    if (CookedStructMetaData->IsJsonInvalid() || !CookedStructMetaData->Has(TEXT("Properties")) || !CookedStructMetaData->HasProperty("StructMetaData")) {
        return;
    }

    const FUObjectJsonValueExport StructMetaData = CookedStructMetaData->GetPropertiesAsValue().GetObject("StructMetaData");

    if (StructMetaData.Has("ObjectMetaData")) {
        const FUObjectJsonValueExport ObjectMetaData = StructMetaData.GetObject("ObjectMetaData");

        if (ObjectMetaData.Has("ObjectMetaData")) {
            for (const FUObjectJsonValueExport& Entry : ObjectMetaData.GetArray("ObjectMetaData")) {
                OutStructMetaData.Add(FName(*Entry.GetString("Key")), Entry.GetString("Value"));
            }
        }
    }

    if (!StructMetaData.Has("PropertiesMetaData")) {
        return;
    }

    for (const FUObjectJsonValueExport& Entry : StructMetaData.GetArray("PropertiesMetaData")) {
        FMemberMetaData& Member = OutMemberMetaData.Add(Entry.GetString("Key"));

        const FUObjectJsonValueExport Value = Entry.GetObject("Value");

        if (!Value.Has("FieldMetaData")) continue;

        for (const FUObjectJsonValueExport& Field : Value.GetArray("FieldMetaData")) {
            const FString Key = Field.GetString("Key");

            if (Key == TEXT("DisplayName")) {
                Member.DisplayName = Field.GetString("Value");
            } else if (Key == TEXT("Tooltip")) {
                Member.ToolTip = Field.GetString("Value");
            } else if (Key != TEXT("MakeStructureDefaultValue")) {
                Member.MetaData.Add(FName(*Key), Field.GetString("Value"));
            }
        }
    }
}

void IUserDefinedStructImporter::AddMemberToStruct(UUserDefinedStruct* UserDefinedStruct, const TSharedPtr<FJsonObject>& PropertyJsonObject, const TMap<FString, FMemberMetaData>& MemberMetaData) {
    const FString Name = PropertyJsonObject->GetStringField(TEXT("Name"));

    FString FriendlyName = Name;
    FGuid VarGuid;

    FRegexMatcher RegexMatcher(PropertyNameRegexPattern, Name);

    if (RegexMatcher.FindNext()) {
        FriendlyName = RegexMatcher.GetCaptureGroup(1);
        VarGuid = StringToGuid(RegexMatcher.GetCaptureGroup(3));
    }

    CastChecked<UUserDefinedStructEditorData>(UserDefinedStruct->EditorData)->GenerateUniqueNameIdForMemberVariable();

    if (!VarGuid.IsValid()) {
        VarGuid = FGuid::NewGuid();
    }

    FStructVariableDescription Variable; {
        Variable.VarName = *Name;
        Variable.FriendlyName = FriendlyName;
        Variable.VarGuid = VarGuid;

        Variable.SetPinType(ResolvePropertyPinType(PropertyJsonObject));
    }

    if (const FMemberMetaData* Metadata = MemberMetaData.Find(Name)) {
        if (!Metadata->DisplayName.IsEmpty()) {
            Variable.FriendlyName = Metadata->DisplayName;
        }

        Variable.ToolTip = Metadata->ToolTip;

#if ENGINE_UE5 && false
        Variable.MetaData = Metadata->MetaData;
#endif
    }

    FStructureEditorUtils::GetVarDesc(UserDefinedStruct).Add(Variable);
}

void IUserDefinedStructImporter::ApplyDefaultValues(UUserDefinedStruct* UserDefinedStruct) {
    if (!DefaultProperties.IsValid() || DefaultProperties->Values.Num() == 0) {
        return;
    }

    FStructOnScope DefaultInstance(UserDefinedStruct);

    if (!DefaultInstance.IsValid()) {
        return;
    }

    for (FStructVariableDescription& Variable : FStructureEditorUtils::GetVarDesc(UserDefinedStruct)) {
        const TSharedPtr<FJsonValue>* DefaultValue = DefaultProperties->Values.Find(StringToJsonKey(Variable.VarName.ToString()));

        if (DefaultValue == nullptr || !DefaultValue->IsValid()) {
            continue;
        }

        FProperty* Property = FindFProperty<FProperty>(UserDefinedStruct, Variable.VarName);

        if (Property == nullptr) {
            continue;
        }

        void* PropertyValue = Property->ContainerPtrToValuePtr<void>(DefaultInstance.GetStructMemory());
        GetPropertySerializer()->DeserializePropertyValue(Property, DefaultValue->ToSharedRef(), PropertyValue);

        FString ExportedValue;
#if UE5_1_BEYOND
        Property->ExportTextItem_Direct(ExportedValue, PropertyValue, nullptr, UserDefinedStruct, 0);
#else
        Property->ExportText_Direct(ExportedValue, PropertyValue, nullptr, UserDefinedStruct, 0);
#endif

        Variable.DefaultValue = ExportedValue;
    }
}

FEdGraphPinType IUserDefinedStructImporter::ResolvePropertyPinType(const TSharedPtr<FJsonObject> &PropertyJsonObject) {
    FEdGraphPinType ResolvedType = FEdGraphPinType(NAME_None, NAME_None, nullptr, EPinContainerType::None,false, FEdGraphTerminalType());

    if (!PropertyJsonObject.IsValid()) {
        UE_LOG(LogReflection, Error, TEXT("Property has no type to resolve, defaulting to 'Byte'"));
        ResolvedType.PinCategory = TEXT("byte");

        return ResolvedType;
    }

    const FString Type = PropertyJsonObject->GetStringField(TEXT("Type"));

    if (const EPinContainerType* ContainerType = ContainerTypeMap.Find(Type)) {
        if (*ContainerType == EPinContainerType::Map) {
            TSharedPtr<FJsonObject> KeyPropObject = PropertyJsonObject->GetObjectField(TEXT("KeyProp"));

            ResolvedType = ResolvePropertyPinType(KeyPropObject);
            ResolvedType.ContainerType = *ContainerType;

            TSharedPtr<FJsonObject> ValuePropObject = PropertyJsonObject->GetObjectField(TEXT("ValueProp"));
            FEdGraphPinType ResolvedTerminalType = ResolvePropertyPinType(ValuePropObject);

            ResolvedType.PinValueType.TerminalCategory = ResolvedTerminalType.PinCategory;
            ResolvedType.PinValueType.TerminalSubCategory = ResolvedTerminalType.PinSubCategory;
            ResolvedType.PinValueType.TerminalSubCategoryObject = ResolvedTerminalType.PinSubCategoryObject;

            return ResolvedType;
        }

        if (*ContainerType == EPinContainerType::Set) {
            TSharedPtr<FJsonObject> ElementPropObject = PropertyJsonObject->GetObjectField(TEXT("ElementProp"));
            ResolvedType = ResolvePropertyPinType(ElementPropObject);

            ResolvedType.ContainerType = *ContainerType;

            return ResolvedType;
        }

        if (*ContainerType == EPinContainerType::Array) {
            TSharedPtr<FJsonObject> InnerTypeObject = PropertyJsonObject->GetObjectField(TEXT("Inner"));
            ResolvedType = ResolvePropertyPinType(InnerTypeObject);

            ResolvedType.ContainerType = *ContainerType;

            return ResolvedType;
        }
    }

    if (const FName* TypeCategory = PropertyCategoryMap.Find(Type)) {
        ResolvedType.PinCategory = *TypeCategory;
    } else {
        UE_LOG(LogReflection, Warning, TEXT("Type '%s' not found in PropertyCategoryMap, defaulting to 'Byte'"), *Type);
        ResolvedType.PinCategory = TEXT("byte");
    }

    if (Type == "DoubleProperty") {
        ResolvedType.PinSubCategory = TEXT("double");
    } else if (Type == "FloatProperty") {
        ResolvedType.PinSubCategory = TEXT("float");
    } else if (Type == "EnumProperty" || Type == "ByteProperty") {
        ResolvedType.PinSubCategoryObject = LoadObjectFromJsonReference(PropertyJsonObject, TEXT("Enum"));
    } else if (Type == "StructProperty") {
        ResolvedType.PinSubCategoryObject = LoadObjectFromJsonReference(PropertyJsonObject, TEXT("Struct"));
    } else if (Type == "ClassProperty" || Type == "SoftClassProperty") {
        ResolvedType.PinSubCategoryObject = LoadObjectFromJsonReference(PropertyJsonObject, TEXT("MetaClass"));
    } else if (Type == "ObjectProperty" || Type == "SoftObjectProperty") {
        ResolvedType.PinSubCategoryObject = LoadObjectFromJsonReference(PropertyJsonObject, TEXT("PropertyClass"));
    }

    return ResolvedType;
}

UObject* IUserDefinedStructImporter::LoadObjectFromJsonReference(const TSharedPtr<FJsonObject> &ParentJsonObject, const FString &ReferenceKey) {
    if (!ParentJsonObject->HasTypedField<EJson::Object>(ReferenceKey)) {
        UE_LOG(LogReflection, Error, TEXT("Failed to load Object from property %s: property not found"), *ReferenceKey);

        return nullptr;
    }

    const TSharedPtr<FJsonObject> ReferenceObject = ParentJsonObject->GetObjectField(ReferenceKey);

    TObjectPtr<UObject> LoadedObject;
    LoadExport<UObject>(&ReferenceObject, LoadedObject);

    return LoadedObject;
}
