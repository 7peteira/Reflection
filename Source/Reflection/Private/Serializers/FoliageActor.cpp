#include "Engine/SharedPtrCompatibility.h"
#include "Serializers/FoliageActor.h"

#include "Containers/ExportContainer.h"
#include "Engine/Compatibility.h"
#include "Engine/StaticMesh.h"
#include "FoliageType.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "Engine/Package.h"
#include "Modules/Toolbar/Tools/ImportFromPath.h"
#include "Serializers/PropertySerializer.h"
#include "Utilities/AssetPaths.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionFoliageActor, All, All);

namespace {
	template <typename T>
	T* LoadReferenced(const FString& CloudPath) {
		if (CloudPath.IsEmpty()) return nullptr;

		const FString Path = ToEditorPackagePath(CloudPath);

		if (T* Found = LoadObjectByPath<T>(Path)) return Found;

		if (!TToolImportFromPath::Import(CloudPath)) return nullptr;

		return LoadObjectByPath<T>(Path);
	}

	FString PathFromQuoted(const FString& Reference) {
		int32 Open, Close;

		if (!Reference.FindChar(TEXT('\''), Open)) return FString();
		if (!Reference.FindLastChar(TEXT('\''), Close) || Close <= Open) return FString();

		FString Path = Reference.Mid(Open + 1, Close - Open - 1);

		int32 Dot;

		if (Path.FindLastChar(TEXT('.'), Dot)) {
			LeftInline(Path, Dot);
		}

		return Path;
	}

	FString PathFromObjectPath(const TSharedPtr<FJsonObject>& Reference) {
		FString Path;

		if (!Reference.IsValid() || !Reference->TryGetStringField(TEXT("ObjectPath"), Path)) return FString();

		int32 Dot;

		if (Path.FindLastChar(TEXT('.'), Dot)) {
			LeftInline(Path, Dot);
		}

		return Path;
	}

	void ReadInstances(const FUObjectExport* Component, TArray<FTransform>& OutTransforms) {
		if (Component == nullptr || !Component->Has(TEXT("PerInstanceSMData"))) return;

		const TArray<FUObjectJsonValueExport> Instances = Component->GetArray(TEXT("PerInstanceSMData"));

		OutTransforms.Reserve(Instances.Num());

		for (const FUObjectJsonValueExport& Instance : Instances) {
			if (!Instance.JsonObject.IsValid() || !Instance.Has(TEXT("TransformData"))) continue;

			const FUObjectJsonValueExport Data = Instance.GetObject(TEXT("TransformData"));

			if (!Data.Has(TEXT("Rotation")) || !Data.Has(TEXT("Translation")) || !Data.Has(TEXT("Scale3D"))) continue;

			const FUObjectJsonValueExport Rotation = Data.GetObject(TEXT("Rotation"));
			const FUObjectJsonValueExport Translation = Data.GetObject(TEXT("Translation"));
			const FUObjectJsonValueExport Scale = Data.GetObject(TEXT("Scale3D"));

			FQuat Turn(
				Rotation.GetNumber(TEXT("X")),
				Rotation.GetNumber(TEXT("Y")),
				Rotation.GetNumber(TEXT("Z")),
				Rotation.GetNumber(TEXT("W"))
			);

			Turn.Normalize();

			OutTransforms.Add(FTransform(
				Turn,
				FVector(Translation.GetNumber(TEXT("X")), Translation.GetNumber(TEXT("Y")), Translation.GetNumber(TEXT("Z"))),
				FVector(Scale.GetNumber(TEXT("X")), Scale.GetNumber(TEXT("Y")), Scale.GetNumber(TEXT("Z")))
			));
		}
	}
}

void FFoliageActor::Rebuild(AInstancedFoliageActor* Actor, const UPropertySerializer* Serializer) {
	if (Actor == nullptr) return;

	if (Serializer == nullptr || Serializer->ExportsContainer == nullptr) {
		UE_LOG(LogReflectionFoliageActor, Warning, TEXT("\"%s\" was reached without the exports it came from, so nothing is planted"), *Actor->GetName());

		return;
	}

	FUObjectExport* Export = nullptr;

	for (FUObjectExport* Candidate : Serializer->ExportsContainer->Exports) {
		if (Candidate != nullptr && Candidate->Object == Actor) {
			Export = Candidate;

			break;
		}
	}

	if (Export == nullptr || !Export->IsJsonValid()) {
		UE_LOG(LogReflectionFoliageActor, Warning, TEXT("\"%s\" is not any of the %d export(s) read, so what it grows cannot be found"), *Actor->GetName(), Serializer->ExportsContainer->Exports.Num());

		return;
	}

	if (!Export->Has(TEXT("FoliageMeshes_Deprecated2"))) {
		UE_LOG(LogReflectionFoliageActor, Warning, TEXT("\"%s\" carries no map of what grows on it"), *Actor->GetName());

		return;
	}

	const TSharedPtr<FJsonObject> Meshes = Export->GetObject(TEXT("FoliageMeshes_Deprecated2")).JsonObject;

	if (!Meshes.IsValid()) {
		UE_LOG(LogReflectionFoliageActor, Warning, TEXT("\"%s\" carries a map of what grows on it that could not be read"), *Actor->GetName());

		return;
	}

	UE_LOG(LogReflectionFoliageActor, Display, TEXT("\"%s\" grows %d kind(s)"), *Actor->GetName(), Meshes->Values.Num());

	int32 Kinds = 0;
	int32 Planted = 0;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Meshes->Values) {
		const TSharedPtr<FJsonObject> Value = Entry.Value.IsValid() ? Entry.Value->AsObject() : nullptr;
		if (!Value.IsValid()) continue;

		const TSharedPtr<FJsonObject>* Reference;
		if (!Value->TryGetObjectField(TEXT("Component"), Reference)) continue;

		FUObjectExport* Component = Serializer->ExportsContainer->GetExportByObjectPath(*Reference);

		if (Component == nullptr || !Component->IsJsonValid()) {
			UE_LOG(LogReflectionFoliageActor, Warning, TEXT("\"%s\" names a component for \"%s\" that is not among the exports"), *Actor->GetName(), *Entry.Key);

			continue;
		}

		TArray<FTransform> Transforms;
		ReadInstances(Component, Transforms);

		if (Transforms.Num() == 0) {
			UE_LOG(LogReflectionFoliageActor, Warning, TEXT("\"%s\" has a component for \"%s\" carrying no instances"), *Actor->GetName(), *Entry.Key);

			continue;
		}

		UFoliageType* Type = LoadReferenced<UFoliageType>(PathFromQuoted(Entry.Key));

#if UE4_22_BELOW
		FFoliageMeshInfo* Info = nullptr;
#else
		FFoliageInfo* Info = nullptr;
#endif

		if (Type != nullptr) {
			Type = Actor->AddFoliageType(Type, &Info);

			if (Info == nullptr && Type != nullptr) {
				Info = Actor->FindOrAddMesh(Type);
			}
		}

		if (Info == nullptr) {
			const TSharedPtr<FJsonObject> Properties = Component->GetProperties();

			const TSharedPtr<FJsonObject>* MeshReference = nullptr;

			if (Properties.IsValid()) {
				Properties->TryGetObjectField(TEXT("StaticMesh"), MeshReference);
			}

			UStaticMesh* Mesh = MeshReference != nullptr ? LoadReferenced<UStaticMesh>(PathFromObjectPath(*MeshReference)) : nullptr;

			if (Mesh == nullptr) {
				UE_LOG(LogReflectionFoliageActor, Warning, TEXT("\"%s\" grows %d of something neither its type nor its mesh has been brought in for, so none of it is planted"), *Actor->GetName(), Transforms.Num());

				continue;
			}

			Info = Actor->AddMesh(Mesh, &Type);
		}

		if (Type == nullptr || Info == nullptr) {
			UE_LOG(LogReflectionFoliageActor, Warning, TEXT("\"%s\" could not be given anywhere to put its %d of \"%s\""), *Actor->GetName(), Transforms.Num(), *Entry.Key);

			continue;
		}

		for (const FTransform& Transform : Transforms) {
			FFoliageInstance Instance;

			Instance.Location = Transform.GetLocation();
			Instance.Rotation = Transform.GetRotation().Rotator();

#if ENGINE_UE5
			Instance.DrawScale3D = FVector3f(Transform.GetScale3D());
#else
			Instance.DrawScale3D = Transform.GetScale3D();
#endif

#if ENGINE_UE5
			Info->AddInstance(Type, Instance);
#elif UE4_22_BELOW
			Info->AddInstance(Actor, Type, Instance,   true);
#else
			Info->AddInstance(Actor, Type, Instance);
#endif

			Planted++;
		}

#if ENGINE_UE5
		Info->Refresh(false, true);
#elif !UE4_22_BELOW
		Info->Refresh(Actor, false, true);
#endif

		Kinds++;
	}

	if (Planted > 0) {
		UE_LOG(LogReflectionFoliageActor, Display, TEXT("\"%s\" was planted with %d instance(s) of %d kind(s)"), *Actor->GetName(), Planted, Kinds);
	}
}
