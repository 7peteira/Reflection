#pragma once

#include "Importers/Types/Texture/TextureTypes.h"

#include "Engine/Compatibility.h"
#include "UObject/MetaData.h"
#include "Dom/JsonObject.h"
#include "CoreMinimal.h"
#include "Serializers/SerializerContainer.h"

#include "Macros.h"

#include "TypesHelper.h"

#include "Registry/RegistrationInfo.h"
#include "ImportIssues.h"
#include "Styling/SlateIconFinder.h"
#include "Importers/Constructor/Asset.h"
#include "Engine/Package.h"
#include "Utilities/AssetPaths.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

#if UE4_26
#include "UObject/MetaData.h"
#endif

inline bool PackageFileOf(const FString& LongPackageName, FString& OutFilename) {
#if ENGINE_UE5
	return FPackageName::DoesPackageExist(LongPackageName, &OutFilename);
#else
	return FPackageName::DoesPackageExist(LongPackageName, nullptr, &OutFilename);
#endif
}

inline bool PackageHoldsAsset(const FString& LongPackageName) {
	FString Held;

	if (!PackageFileOf(LongPackageName, Held)) return false;

	if (const UPackage* Standing = FindPackage(nullptr, *LongPackageName)) {
		TArray<UObject*> Inside;
		GetObjectsWithOuter(Standing, Inside, false);

		for (const UObject* One : Inside) {
			if (One == nullptr) continue;

#if !UE5_6_BEYOND
			if (One->IsA<UMetaData>()) continue;
#endif

			return true;
		}

		return false;
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Found;
	Registry.GetAssetsByPackageName(*LongPackageName, Found, true);

	if (Found.Num() > 0) return true;

	Registry.ScanFilesSynchronous({ Held }, true);
	Registry.GetAssetsByPackageName(*LongPackageName, Found, true);

	return Found.Num() > 0;
}

class REFLECTION_API IImporter : public USerializerContainer {
public:
    IImporter() {}

    virtual ~IImporter() override {}

public:
    virtual bool Import() {
        return false;
    }

    virtual void Validate(UObject* Asset) const {}

    virtual UObject* CreateAsset(UObject* CreatedAsset = nullptr);

    template<typename T>
    T* Create() {
        UObject* TargetAsset = CreateAsset(nullptr);

        return Cast<T>(TargetAsset);
    }

public:
    template<class T = UObject>
    void LoadExport(const TSharedPtr<FJsonObject>* PackageIndex, TObjectPtr<T>& Object);

    template<class T = UObject>
    TArray<TObjectPtr<T>> LoadExport(const TArray<TSharedPtr<FJsonValue>>& PackageArray, TArray<TObjectPtr<T>> Array);

public:
    void Save() const;

    bool OnAssetCreation(UObject* Asset) const;

public:
    template <class T = UObject>
    FORCEINLINE static TObjectPtr<T> DownloadWrapper(TObjectPtr<T> InObject, FString Type, const FString Name, const FString Path, const FString Lands = FString(), const FString Within = FString());
};

template <class T>
TObjectPtr<T> IImporter::DownloadWrapper(TObjectPtr<T> InObject, FString Type, const FString Name, const FString Path, const FString Lands, const FString Within) {
    const UReflectionSettings* Settings = GetSettings();

    if ((
        InObject == nullptr ||
            (Settings->AssetSettings.Texture.ReflectExistingTextures && Type == "Texture2D")
        )
        && !Path.StartsWith("Engine/") && !Path.StartsWith("/Engine/")
    ) {
        const UObject* DefaultObject = GetClassDefaultObject(T::StaticClass());

        if (DefaultObject != nullptr && !Name.IsEmpty() && !Path.IsEmpty()) {
            bool DownloadStatus = false;

            FString NewPath = Path;
            FRRedirects::Reverse(NewPath);

            const FString Reflected = Type + TEXT("'") + NewPath + TEXT(".") + Name + TEXT("'");

            if (InObject != nullptr && FAssetUtilities::ReflectedThisRun.Contains(Reflected)) {
                return InObject;
            }

            FAssetUtilities::ReflectedThisRun.Add(Reflected);

            const FString Asked = FSoftObjectPath(Type + "'" + NewPath + "." + Name + "'").ToString();
            const FString Where = Lands.IsEmpty() ? Asked : FSoftObjectPath(Type + "'" + Lands + "'").ToString();

            if (InObject == nullptr && Path.StartsWith(TEXT("/Script/"))) {
                FImportIssues::ReportUnresolvedReference(Type, Name, Path, Within);

                return InObject;
            }

            if (InObject == nullptr && PackageHoldsAsset(FSoftObjectPath(Where).GetLongPackageName())) {
                FImportIssues::ReportUnresolvedReference(Type, Name, Path, Within);

                return InObject;
            }

            if (FAssetUtilities::ConstructAsset(Asked, Where, Type, InObject, DownloadStatus) && DownloadStatus) {
                AppendNotification(
                    FText::FromString(Name),
                    FText::FromString(Type),
                    2.0f,
                    FSlateIconFinder::FindCustomIconBrushForClass(FindObject<UClass>(nullptr, *("/Script/Engine." + Type)), TEXT("ClassThumbnail")),
                    SNotificationItem::CS_Success,
                    false,
                    310.0f
                );
            }

            if (InObject == nullptr) {
                FImportIssues::ReportUnresolvedReference(Type, Name, Path, Within);
            }
        }
    }

    return InObject;
}

template <typename T>
void IImporter::LoadExport(const TSharedPtr<FJsonObject>* PackageIndex, TObjectPtr<T>& Object) {
	FString ObjectType, ObjectName, ObjectPath, Outer;
	PackageIndex->Get()->GetStringField(TEXT("ObjectName")).Split("'", &ObjectType, &ObjectName);

	ObjectPath = PackageIndex->Get()->GetStringField(TEXT("ObjectPath"));
	ObjectPath.Split(".", &ObjectPath, nullptr);

	ObjectName = ObjectName.Replace(TEXT("'"), TEXT(""));

	const bool bWithin = ObjectName.Contains(TEXT(":"));

	const FString Spelled = ObjectName;

	ObjectName = ObjectName.Replace(TEXT(":"), TEXT("."));

	if (ObjectName.Contains(".")) {
		FString Chain;
		ObjectName.Split(".", &Chain, &ObjectName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

		Chain.Split(".", nullptr, &Outer, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	}

	ObjectPath = ToEditorPackagePath(ObjectPath);

	FString Held;

	if (bWithin && PackageFileOf(ObjectPath, Held) && FPaths::GetExtension(Held, true) == FPackageName::GetMapPackageExtension()) {
		return;
	}

	FString Lands;

	if (FTextureTypes::IsSupported(ObjectType) && GetSettings()->AssetSettings.PackagedAssets == ERPackagedAssets::Separate) {
		FString Leaf;

		if (ObjectPath.Split(TEXT("/"), nullptr, &Leaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd) && !Leaf.IsEmpty() && Leaf != ObjectName) {
			Lands = ObjectPath + TEXT("_Texture.") + Leaf + TEXT("_Texture");
		}
	}

	TObjectPtr<T> LoadedObject = Lands.IsEmpty() ? nullptr : LoadObjectByPath<T>(Lands);

	if (!LoadedObject && bWithin) {
		LoadedObject = LoadObjectByPath<T>(Spelled.StartsWith(TEXT("/")) ? Spelled : ObjectPath + TEXT(".") + Spelled);
	}

	if (!LoadedObject) LoadedObject = LoadObjectByPath<T>(ObjectPath + "." + ObjectName);

	if (!LoadedObject) {
		FString NewObjectPath;
		FString ObjectFileName; {
			ObjectPath.Split("/", &NewObjectPath, &ObjectFileName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		}

		NewObjectPath = NewObjectPath + "/" + ObjectName;

		if (ObjectFileName != ObjectName) {
			LoadedObject = LoadObjectByPath<T>(NewObjectPath + "." + ObjectName);
		}
	}

	if (GetParent() != nullptr) {
		if (!Outer.IsEmpty() && GetParent()->IsA(AActor::StaticClass())) {
			const AActor* NewLoadedObject = Cast<AActor>(GetParent());
			auto Components = NewLoadedObject->GetComponents();

			for (UActorComponent* Component : Components) {
#if UE4_24_BELOW
				if constexpr (TIsDerivedFrom<T, UActorComponent>::IsDerived) {
#else
				if constexpr (TIsDerivedFrom<T, UActorComponent>::Value) {
#endif
					if (ObjectName == Component->GetName()) {
						if (Component->IsA(T::StaticClass())) {
							LoadedObject = Cast<T>(Component);
						}
					}
				}
			}
		}
	}

	if (!LoadedObject && ObjectName.Contains("MaterialExpression")) {
		FString SplitObjectName;
		ObjectPath.Split("/", nullptr, &SplitObjectName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		LoadedObject = LoadObjectByPath<T>(ObjectPath + "." + SplitObjectName + ":" + ObjectName);
	}

	Object = LoadedObject;

	if (!Object && GetObjectSerializer() != nullptr && GetPropertySerializer() != nullptr && GetPropertySerializer()->ExportsContainer != nullptr) {
		FUObjectExport* Export = GetPropertySerializer()->ExportsContainer->Find(ObjectName);

		if (Export != nullptr && Export->IsJsonValid() && Export->Object == nullptr) {
			GetObjectSerializer()->SpawnExport(Export);
		}

		if (Export && Export->IsJsonAndObjectValid() && Export->Object != nullptr && Export->Object->IsA(T::StaticClass())) {
			Object = TObjectPtr<T>(Cast<T>(Export->Object));
		}
	}

	if (!Object) {
		if (ObjectType == "WidgetBlueprintGeneratedClass") return;

		Object = DownloadWrapper(LoadedObject, ObjectType, ObjectName, ObjectPath, Lands, bWithin ? Spelled : FString());
	}
}

template <typename T>
TArray<TObjectPtr<T>> IImporter::LoadExport(const TArray<TSharedPtr<FJsonValue>>& PackageArray, TArray<TObjectPtr<T>> Array) {
	for (const TSharedPtr<FJsonValue>& ArrayElement : PackageArray) {
		const TSharedPtr<FJsonObject> ObjectPtr = ArrayElement->AsObject();
		TObjectPtr<T> Out;

		LoadExport<T>(&ObjectPtr, Out);

		Array.Add(Out);
	}

	return Array;
}
