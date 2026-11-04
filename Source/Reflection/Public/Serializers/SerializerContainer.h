#pragma once

#include "CoreMinimal.h"

#include "Serializers/PropertySerializer.h"
#include "Serializers/ObjectSerializer.h"

class REFLECTION_API USerializerContainer {
public:
    USerializerContainer();

    virtual ~USerializerContainer() {}

    virtual void Initialize(FUObjectExport* Export, FUObjectExportContainer* Container);

public:
    FUObjectExport* AssetExport = new FUObjectExport();
    FUObjectExportContainer* AssetContainer = new FUObjectExportContainer();

public:
    virtual FString GetAssetName() const;
    virtual FString GetAssetType() const;
    virtual UClass* GetAssetClass();

    virtual TSharedPtr<FJsonObject> GetAssetData() const;
    virtual FUObjectJsonValueExport GetAssetDataAsValue() const;
    virtual FUObjectJsonValueExport GetAssetAsValue() const;
    virtual TSharedPtr<FJsonObject>& GetAssetExport();

    virtual UPackage* GetPackage() const;
    virtual void SetPackage(UPackage* NewPackage);

    virtual UObject* GetParent() const;
    virtual void SetParent(UObject* Parent);

    virtual UObject* GetAsset();
    virtual void SetAsset(UObject* InAsset);

    #if !PLATFORM_LINUX
    FORCEINLINE FUObjectExportContainer* GetContainer() const;
    #else
    FUObjectExportContainer* GetContainer() const;
    #endif

    template <class T>
    T* GetTypedAsset() const {
        return AssetExport->Object ? Cast<T>(AssetExport->Object) : nullptr;
    }

public:
    #if !PLATFORM_LINUX
    FORCEINLINE UObjectSerializer* GetObjectSerializer() const;
    FORCEINLINE UPropertySerializer* GetPropertySerializer() const;
    #else
    UObjectSerializer* GetObjectSerializer() const;
    UPropertySerializer* GetPropertySerializer() const;
    #endif

    void DeserializeExports(UObject* Parent, bool CreateObjects = true);

    virtual void ApplyModifications() { }
protected:
    void CreateSerializer();

private:
    UObjectSerializer* ObjectSerializer;
};
