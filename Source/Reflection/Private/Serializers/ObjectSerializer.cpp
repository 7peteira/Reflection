#include "Engine/SharedPtrCompatibility.h"
#include "Serializers/ObjectSerializer.h"

#include "Animation/AnimNodeBase.h"
#include "Animation/WidgetAnimation.h"
#include "Engine/Compatibility.h"

#include "Serializers/PropertySerializer.h"
#include "Serializers/VolumeBrush.h"
#include "Serializers/FoliageActor.h"
#include "Importers/Constructor/ImportIssues.h"
#include "Engine/Log.h"
#include "InstancedFoliageActor.h"
#include "UObject/Package.h"
#include "Utilities/JsonHelpers.h"
#include "Utilities/Containers.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/BrushComponent.h"
#include "Engine/Brush.h"
#include "Engine/Properties.h"
#include "MovieSceneSection.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleSystem.h"
#include "Particles/TypeData/ParticleModuleTypeDataGpu.h"
#include "Components/StaticMeshComponent.h"
#if __has_include("StaticMeshComponentLODInfo.h")
#include "StaticMeshComponentLODInfo.h"
#endif
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#if !UE4_23_BELOW
#include "StaticMeshAttributes.h"
#endif
#include "StaticMeshResources.h"
#include "Misc/Parse.h"
#include "Settings/Runtime.h"

DECLARE_LOG_CATEGORY_CLASS(LogReflectionObjectSerializer, All, All);

#if UE5_2_BEYOND
UE_DISABLE_OPTIMIZATION
#else
PRAGMA_DISABLE_OPTIMIZATION
#endif

UObjectSerializer::UObjectSerializer(): Parent(nullptr), PropertySerializer(nullptr) { }

void UObjectSerializer::SetupExports(const TArray<TSharedPtr<FJsonValue>>& InObjects) {
	Exports = InObjects;
}

namespace {
	UObject* AlreadyThere(UObject* Outer, const FName Named, const UClass* Wanted) {
		if (Outer == nullptr || Named.IsNone()) return nullptr;

		UObject* Standing = StaticFindObjectFast(UObject::StaticClass(), Outer, Named);

		if (Standing == nullptr) return nullptr;

		if (Wanted != nullptr && !Standing->IsA(Wanted) && Standing->IsDefaultSubobject()) {
			UE_LOG(LogReflection, Warning, TEXT("\"%s\" is a %s here and a %s in the export; it is a default subobject, so it stays what the native class makes"), *Named.ToString(), *Standing->GetClass()->GetName(), *Wanted->GetName());

			return Standing;
		}

		if (Wanted != nullptr && !Standing->IsA(Wanted)) {
			MoveToTransientPackageAndRename(Standing);

			return nullptr;
		}

		return Standing;
	}

	bool CanBeMade(const UClass* Class, const FString& Named, const FString& Type) {
		if (Class == nullptr) return false;

		if (Class->IsChildOf(UClass::StaticClass())) {
			UE_LOG(LogReflection, Error, TEXT("\"%s\" is written as a %s and this build has no class of that name, so nothing was made for it"), *Named, *Type);

			FImportIssues::Report(
				EImportIssue::MissingClass,
				FString::Printf(TEXT("\"%s\" could not be made"), *Named),
				FString::Printf(TEXT("The export says it is a %s and this build has no class of that name. What is left to go on names a class rather than an object, and a class is not something that can be made: it would come out with no parent and no default object and take a later compile down with it. Nothing was put in its place and the rest of the import carried on."), *Type)
			);

			return false;
		}

		if (!Class->HasAnyClassFlags(CLASS_Abstract)) return true;

		UE_LOG(LogReflection, Error, TEXT("\"%s\" is written as a %s and that read back as %s, which cannot be made"), *Named, *Type, *Class->GetName());

		FImportIssues::Report(
			EImportIssue::Data,
			FString::Printf(TEXT("\"%s\" could not be made"), *Named),
			FString::Printf(TEXT("The export says it is a %s. The class of that name in this engine is %s, which is abstract and so is not something that can be made. Nothing was put in the object's place and the rest of the import carried on."), *Type, *Class->GetName())
		);

		return false;
	}
}

namespace {
	TArray<FUObjectExport*> GBeingMade;

	struct FSpawnScope {
		explicit FSpawnScope(FUObjectExport* InExport) : Export(InExport) { GBeingMade.Add(Export); }

		~FSpawnScope() { GBeingMade.Remove(Export); }

		FUObjectExport* Export;
	};

	const UClass* GetClassToMake(FUObjectExport* Export) {
#if ENGINE_UE5
		if (Export->GetType() == TEXT("CommonWidgetSwitcher")) {
			if (const UClass* LegacyClass = FindClassByType(TEXT("CommonWidgetSwitcherLegacy"))) {
				return LegacyClass;
			}
		}
#endif

		return Export->GetClass();
	}

	FString GetObjectNameToMake(FUObjectExport* Export, const UClass* Class) {
		FString ObjectName = Export->GetName().ToString();

		if (Class->IsChildOf(UWidgetAnimation::StaticClass())) {
			ObjectName.Split("_INST", &ObjectName, nullptr, ESearchCase::CaseSensitive);
		}

		return ObjectName;
	}

	void MakeAheadOfOuter(FUObjectExportContainer* Container, FUObjectExport* Export) {
		const UClass* Class = GetClassToMake(Export);

		if (Container == nullptr || Class == nullptr) return;
		if (Class->IsChildOf(UClass::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) return;

		const TArray<FName> TreeSegments = Export->GetOuterTreeSegments(true);

		if (TreeSegments.Num() == 0) return;

		const FUObjectExport* OuterExport = Container->FindByTreeSegment(TreeSegments);
		UObject* ObjectOuter = OuterExport->IsJsonValid() ? OuterExport->Object : nullptr;

		if (ObjectOuter == nullptr) return;
		if (Class->ClassWithin != nullptr && !ObjectOuter->IsA(Class->ClassWithin)) return;

		const FName Named = StringToName(GetObjectNameToMake(Export, Class));

		if (UObject* Standing = AlreadyThere(ObjectOuter, Named, Class)) {
			Export->Object = Standing;

			return;
		}

		EObjectFlags Flags = RF_Public | RF_Transactional;

		if (Export->Has("Flags")) {
			Flags = ParseObjectFlags(Export->GetString("Flags"));
		}

		Export->Object = NewObject<UObject>(ObjectOuter, ToNewObjectClass(Class), Named, Flags);
	}
}

UObject* UObjectSerializer::SpawnExport(FUObjectExport* Export, const bool bOnlySerialize) {
	if (Export == nullptr) return nullptr;

	if (GBeingMade.Contains(Export)) {
		if (Export->Object == nullptr) {
			MakeAheadOfOuter(PropertySerializer->ExportsContainer, Export);
		}

		return Export->Object;
	}

	const FSpawnScope BeingMade(Export);

	if (!bOnlySerialize) {
		if (Export->Object != nullptr) return nullptr;
	}

	const UClass* Class = GetClassToMake(Export);

	if (!Class) return nullptr;

	const FString Outer = GetOuterFromObjectOuter(Export->JsonObject->TryGetField(TEXT("Outer")));
	UObject* ObjectOuter = nullptr;

	const TArray<FName> TreeSegments = Export->GetOuterTreeSegments(true);

	if (TreeSegments.Num() > 0) {
		if (FUObjectExport* OuterExport = PropertySerializer->ExportsContainer->FindByTreeSegment(TreeSegments); OuterExport->IsJsonValid() && !IsEmpty(OuterExport->JsonObject->Values)) {
			if (OuterExport->Object == nullptr) {
				SpawnExport(OuterExport);
			}

			if (OuterExport->Object) {
				ObjectOuter = OuterExport->Object;
			}
		}
	}

	const bool bWalkedToOuter = ObjectOuter != nullptr;

	if (!bWalkedToOuter) {
		if (FUObjectExport* OuterExport = PropertySerializer->ExportsContainer->FindOther(FName(*Outer), Export); !Outer.IsEmpty() && OuterExport->JsonObject.IsValid()) {
			if (OuterExport->Object == nullptr) {
				SpawnExport(OuterExport);
			}

			ObjectOuter = OuterExport->Object;
		}
	}

	if (!bWalkedToOuter && !Outer.IsEmpty()) {
		FString PotentialBPName;
		Outer.Split("_C", &PotentialBPName, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);

		if (FUObjectExport* OuterExport = PropertySerializer->ExportsContainer->Find(PotentialBPName); OuterExport->JsonObject.IsValid() && !IsEmpty(OuterExport->JsonObject->Values)) {
			if (OuterExport->Object == nullptr) {
				SpawnExport(OuterExport);
			}

			if (OuterExport->Object) {
				ObjectOuter = OuterExport->Object;
			}
		}
	}

	if (!ObjectOuter && !Export->Object) return nullptr;

	EObjectFlags Flags = RF_Public | RF_Transactional;

	if (Export->Has("Flags")) {
		Flags = ParseObjectFlags(Export->GetString("Flags"));
	}

	if (!Export->Object) {
		const FString ObjectName = GetObjectNameToMake(Export, Class);
		const FName Named = StringToName(ObjectName);

		if (ObjectOuter != nullptr && Class->ClassWithin != nullptr && !ObjectOuter->IsA(Class->ClassWithin)) {
			if (Class->IsChildOf(UFunction::StaticClass()) && ObjectOuter->IsA<UBlueprint>()) {
				UE_LOG(LogReflection, Verbose, TEXT("\"%s\" is a function of %s, left for the compiler to make from the graph"), *ObjectName, *ObjectOuter->GetFullName());

				return nullptr;
			}

			UE_LOG(LogReflection, Error, TEXT("\"%s\" is a %s, which has to live in a %s, and its outer is %s, so nothing was made for it"), *ObjectName, *Class->GetName(), *Class->ClassWithin->GetName(), *ObjectOuter->GetFullName());

			FImportIssues::Report(
				EImportIssue::Data,
				FString::Printf(TEXT("\"%s\" could not be made"), *ObjectName),
				FString::Printf(TEXT("A %s can only be made inside a %s, and the outer this export resolved to is %s. Making it there would stop the editor, so nothing was put in its place and the rest of the import carried on."), *Class->GetName(), *Class->ClassWithin->GetName(), *ObjectOuter->GetFullName())
			);

			return nullptr;
		}

		if (UObject* Standing = AlreadyThere(ObjectOuter, Named, Class)) {
			Export->Object = Standing;
		} else if (CanBeMade(Class, ObjectName, Export->GetType().ToString())) {
			Export->Object = NewObject<UObject>(ObjectOuter, ToNewObjectClass(Class), Named, Flags);
		} else {
			return nullptr;
		}
	}

	if (UParticleSystem* ParticleSystem = Cast<UParticleSystem>(Export->Object)) {
		ParticleSystem->PreEditChange(nullptr);
	}

	DeserializeObjectProperties(Export->GetProperties(), Export->Object);

	if (UParticleEmitter* ParticleEmitter = Cast<UParticleEmitter>(Export->Object)) {
		ParticleEmitter->EmitterEditorColor = FColor::MakeRandomColor();
		ParticleEmitter->EmitterEditorColor.A = 255;

		ParticleEmitter->UpdateModuleLists();
		ParticleEmitter->PostEditChange();

		if (!GReflectionRuntime.IsUE5OrNewer()) {
#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 3
			if (ParticleEmitter->DetailModeBitmask & 1 << EParticleDetailMode::PDM_High) {
				ParticleEmitter->DetailModeBitmask |= 1 << EParticleDetailMode::PDM_Epic;
			}
#endif
		}
	}

	if (UParticleLODLevel* ParticleLODLevel = Cast<UParticleLODLevel>(Export->Object)) {
		ParticleLODLevel->ConvertedModules = true;
	}

	if (UParticleSystem* ParticleSystem = Cast<UParticleSystem>(Export->Object)) {
		ParticleSystem->PostEditChange();
		ParticleSystem->SetupSoloing();
	}

	if (UParticleModule* ParticleModule = Cast<UParticleModule>(Export->Object)) {
		ParticleModule->PostLoad();
	}

	return Export->Object;
}

void UObjectSerializer::SetPropertySerializer(UPropertySerializer* NewPropertySerializer) {
	check(NewPropertySerializer);

	PropertySerializer = NewPropertySerializer;
	NewPropertySerializer->ObjectSerializer = this;
}

void UObjectSerializer::SetExportForDeserialization(const TSharedPtr<FJsonObject>& JsonObject, UObject* Object) {
	ExportsToNotDeserialize.Add(JsonObject->GetStringField(TEXT("Name")));
	ConstructedObjects.Add(JsonObject->GetStringField(TEXT("Name")), Object);
}

void UObjectSerializer::DeserializeExports(FUObjectExportContainer* Container, const bool CreateObjects) {
	if (Container == nullptr) return;

	if (PropertySerializer != nullptr) {
		PropertySerializer->ExportsContainer = Container;
	}

	if (CreateObjects) {
		TMap<TSharedPtr<FJsonObject>, UObject*> ExportsMap;

		for (FUObjectExport* Export : Container->Exports) {
			FString Type = Export->GetType().ToString();

			if (ExportsToNotDeserialize.Contains(Export->GetName().ToString())) continue;

			if (WhitelistedTypes.Num() > 0) {
				bool bMatchFound = false;

				for (const FString& Whitelisted : WhitelistedTypes) {
					if (Type.Contains(Whitelisted)) {
						bMatchFound = true;
						break;
					}
				}

				if (!bMatchFound) {
					continue;
				}
			}

			if (Type == "NavCollision") continue;

			DeserializeExport(Export, ExportsMap);
		}

		for (const auto& Pair : ExportsMap) {
			TSharedPtr<FJsonObject> Properties = Pair.Key;
			UObject* Object = Pair.Value;

			DeserializeObjectProperties(Properties, Object);
		}
	}
}

void UObjectSerializer::DeserializeExport(FUObjectExport* Export, TMap<TSharedPtr<FJsonObject>, UObject*>& ExportsMap) {
	if (Export->Object != nullptr) return;

	const TSharedPtr<FJsonObject> ExportObject = Export->JsonObject;

	if (!ExportObject->HasField(TEXT("Name"))) return;

	const FString Name = ExportObject->GetStringField(TEXT("Name"));
	const FString Type = ExportObject->GetStringField(TEXT("Type")).Replace(TEXT("CommonWidgetSwitcher"), TEXT("CommonActivatableWidgetSwitcher"));

	if (ExportsToNotDeserialize.Contains(Name)) return;
	if (Type == "BodySetup" || Type == "NavCollision") return;

	FString ClassName = ExportObject->GetStringField(TEXT("Class"));

	if (ExportObject->HasField(TEXT("Template"))) {
		auto TemplateObject = Export->GetObject("Template");
		ClassName = ReadPathFromObject(TemplateObject).Replace(TEXT("Default__"), TEXT(""));
	}

	if (ClassName.Contains("'")) {
		ClassName.Split("'", nullptr, &ClassName, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		ClassName.Split("'", &ClassName, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromStart);
	}

	const UClass* Class = FindClassByType(ClassName);

	if (!Class) {
		Class = FindClassByType(Type);
	}

	if (!Class) return;

	const FString Outer = GetOuterFromObjectOuter(ExportObject->TryGetField(TEXT("Outer")));
	UObject* ObjectOuter = nullptr;

	if (FUObjectExport* FoundExport = PropertySerializer->ExportsContainer->Find(Outer); FoundExport->JsonObject.IsValid()) {
		if (FoundExport->Object == nullptr) {
			DeserializeExport(FoundExport, ExportsMap);
		}

		UObject* FoundObject = FoundExport->Object;
		ObjectOuter = FoundObject;
	}

	const TArray<FName> TreeSegments = Export->GetOuterTreeSegments(true);

	if (TreeSegments.Num() > 0) {
		if (FUObjectExport* FoundExport = PropertySerializer->ExportsContainer->FindByTreeSegment(TreeSegments); FoundExport->IsJsonValid() && !IsEmpty(FoundExport->JsonObject->Values)) {
			if (FoundExport->Object == nullptr) {
				DeserializeExport(FoundExport, ExportsMap);
			}

			UObject* FoundObject = FoundExport->Object;
			ObjectOuter = FoundObject;
		}
	}

	if (UObject** ConstructedObject = ConstructedObjects.Find(Outer)) {
		ObjectOuter = *ConstructedObject;
	}

	if (Export->Object) return;

	if (ObjectOuter == nullptr) {
		ObjectOuter = Parent;
	}

	UObject* NewUObject = AlreadyThere(ObjectOuter, FName(*Name), Class);

	if (NewUObject == nullptr) {
		if (!CanBeMade(Class, Name, Type)) return;

		NewUObject = NewObject<UObject>(ObjectOuter, ToNewObjectClass(Class), FName(*Name), RF_Public | RF_Transactional);
	}

	if (ExportObject->HasField(TEXT("Properties"))) {
		const TSharedPtr<FJsonObject> Properties = ExportObject->GetObjectField(TEXT("Properties"));

		ExportsMap.Add(Properties, NewUObject);
	} else {
		ExportsMap.Add(ExportObject, NewUObject);
	}

	Export->Object = NewUObject;
}

static TArray<FPaintedVertex> BuildPaintedVertices(UStaticMesh* StaticMesh, const int32 LodIndex, const TArray<TSharedPtr<FJsonValue>>& Colors) {
	TArray<FPaintedVertex> Painted;

	if (StaticMesh == nullptr) return Painted;

	FMeshDescription* MeshDescription = StaticMesh->IsMeshDescriptionValid(LodIndex) ? StaticMesh->GetMeshDescription(LodIndex) : nullptr;

	if (MeshDescription == nullptr) {
		UE_LOG(LogReflectionObjectSerializer, Warning, TEXT("\"%s\" LOD%d holds no mesh description, so there is nothing to say where its %d cooked colour(s) were painted"), *StaticMesh->GetName(), LodIndex, Colors.Num());

		return Painted;
	}

	if (MeshDescription->Vertices().Num() != Colors.Num()) {
		UE_LOG(LogReflectionObjectSerializer, Warning, TEXT("\"%s\" LOD%d describes %d vertices and the game cooked %d colour(s) for it, which are not the same mesh"), *StaticMesh->GetName(), LodIndex, MeshDescription->Vertices().Num(), Colors.Num());

		return Painted;
	}

	FStaticMeshAttributes Attributes(*MeshDescription);

	const TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	const TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();

	TArray<FVector3f> VertexNormals;
	VertexNormals.SetNumZeroed(Colors.Num());

	for (const FVertexInstanceID InstanceID : MeshDescription->VertexInstances().GetElementIDs()) {
		const int32 Vertex = MeshDescription->GetVertexInstanceVertex(InstanceID).GetValue();

		if (VertexNormals.IsValidIndex(Vertex)) {
			VertexNormals[Vertex] = Normals[InstanceID];
		}
	}

	Painted.Reserve(Colors.Num());

	for (int32 Vertex = 0; Vertex < Colors.Num(); ++Vertex) {
		const FString Color = Colors[Vertex]->AsString();

		if (Color.Len() < 8) {
			UE_LOG(LogReflectionObjectSerializer, Warning, TEXT("\"%s\" LOD%d spells a colour in %d character(s) where eight were expected, so its paint is left where the game indexed it"), *StaticMesh->GetName(), LodIndex, Color.Len());

			return TArray<FPaintedVertex>();
		}

		auto Channel = [&Color](const int32 Offset) {
			return static_cast<uint8>(FParse::HexDigit(Color[Offset]) * 16 + FParse::HexDigit(Color[Offset + 1]));
		};

		const FVector3f Position = Positions[FVertexID(Vertex)];
		const FVector3f Normal = VertexNormals[Vertex];

		FPaintedVertex& Entry = Painted.AddDefaulted_GetRef();

		Entry.Position = FVector(Position.X, Position.Y, Position.Z);
		Entry.Normal = FVector4(Normal.X, Normal.Y, Normal.Z, 1.0f);
		Entry.Color = FColor(Channel(2), Channel(4), Channel(6), Channel(0));
	}

	return Painted;
}

static TArray<FColor> RepaintOverrideColors(UStaticMesh* StaticMesh, const int32 LodIndex, const TArray<FPaintedVertex>& Painted) {
	TArray<FColor> Repainted;

	if (StaticMesh == nullptr || Painted.Num() == 0) return Repainted;

	FStaticMeshRenderData* RenderData = GetStaticMeshRenderData(StaticMesh);

	if (RenderData == nullptr || !RenderData->LODResources.IsValidIndex(LodIndex)) return Repainted;

	FStaticMeshLODResources& Resources = RenderData->LODResources[LodIndex];
	FStaticMeshVertexBuffers& Buffers = Resources.VertexBuffers;

	const int32 BuiltVertices = Resources.GetNumVertices();

	if (BuiltVertices == 0) {
		UE_LOG(LogReflectionObjectSerializer, Warning, TEXT("\"%s\" LOD%d has not been built yet, so its %d cooked colour(s) stay where the game indexed them"), *StaticMesh->GetName(), LodIndex, Painted.Num());

		return Repainted;
	}

	RemapPaintedVertexColors(
		Painted,
		nullptr,
		Buffers.PositionVertexBuffer,
		Buffers.StaticMeshVertexBuffer,
		Buffers.PositionVertexBuffer,
		&Buffers.StaticMeshVertexBuffer,
		Repainted
	);

	if (Repainted.Num() != BuiltVertices) {
		UE_LOG(LogReflectionObjectSerializer, Warning, TEXT("\"%s\" LOD%d found colours for %d of the %d vertices the editor built, so the ones the game cooked stay as they are"), *StaticMesh->GetName(), LodIndex, Repainted.Num(), BuiltVertices);

		Repainted.Empty();

		return Repainted;
	}

	if (BuiltVertices != Painted.Num()) {
		UE_LOG(LogReflectionObjectSerializer, Display, TEXT("\"%s\" LOD%d was built with %d vertices where the game cooked %d, and its colours were placed by where they were painted"), *StaticMesh->GetName(), LodIndex, BuiltVertices, Painted.Num());
	}

	return Repainted;
}

void UObjectSerializer::DeserializeObjectProperties(const TSharedPtr<FJsonObject>& Properties, UObject* Object) const {
	if (Object == nullptr) return;

	if (Cast<UParticleSystem>(Object)) {
		Object->PreEditChange(nullptr);
	}

	const UClass* ObjectClass = Object->GetClass();

	const TWeakObjectPtr<UObject> Alive(Object);

	for (FProperty* Property = ObjectClass->PropertyLink; Property; Property = Property->PropertyLinkNext) {
		if (!Property->IsValidLowLevel()) continue;

		FString PropertyName = Property->GetName();

		if (!PropertySerializer->ShouldDeserializeProperty(Property)) continue;

		if (!Alive.IsValid()) {
			UE_LOG(LogReflection, Warning, TEXT("\"%s\" was taken apart while its properties were being read, and the rest were left"), *ObjectClass->GetName());

			return;
		}

		void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Object);
		const bool HasHandledProperty = PassthroughPropertyHandler(Property, PropertyName, PropertyValue, Properties, PropertySerializer);

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property)) {
			if (StructProperty->Struct->IsChildOf(FAnimNode_Base::StaticStruct())) {
				void* StructPtr = StructProperty->ContainerPtrToValuePtr<void>(Object);

				if (static_cast<FAnimNode_Base*>(StructPtr)) {
					PropertySerializer->DeserializeStruct(StructProperty->Struct, Properties.ToSharedRef(), PropertyValue, Object);
				}
			}
		}

#if ENGINE_UE5
		if (Object->IsA<UExponentialHeightFogComponent>()) {
			if (Property->NamePrivate == "FogInscatteringLuminance" && !Properties->HasField(TEXT("FogInscatteringLuminance"))) {
				PropertyName = "FogInscatteringColor";
			}

			if (Property->NamePrivate == "DirectionalInscatteringLuminance" && !Properties->HasField(TEXT("DirectionalInscatteringLuminance"))) {
				PropertyName = "DirectionalInscatteringColor";
			}
		}
#endif

		if (Properties->HasField(PropertyName) && !HasHandledProperty && PropertyName != "LODParentPrimitive" && PropertyName != "bIsCookedForEditor") {
			const TSharedPtr<FJsonValue>& ValueObject = Properties->Values.FindChecked(StringToJsonKey(PropertyName));

			if (Property->ArrayDim == 1 || ValueObject->Type == EJson::Array) {
				PropertySerializer->DeserializePropertyValue(Property, ValueObject.ToSharedRef(), PropertyValue, Object);
			}
		}
	}

	{
		static const UClass* Effects = nullptr;

		if (Effects == nullptr) {
			Effects = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayEffect"));
		}

		if (Effects != nullptr && Object != nullptr && Object->IsA(Effects) && Object->HasAnyFlags(RF_ClassDefaultObject)) {
			const FStructProperty* Stamped = FindFProperty<FStructProperty>(Effects, TEXT("DataVersion"));

			if (Stamped != nullptr && Stamped->Struct != nullptr && Stamped->Struct->GetFName() == TEXT("GameplayEffectVersion")) {
				const UObject* Inherited = Object->GetArchetype();

				if (Inherited != nullptr && Inherited != Object && Inherited->IsA(Effects)) {
					static const TCHAR* Superseded[] = {
						TEXT("GrantedAbilities"),
						TEXT("InheritableGameplayEffectTags"),
						TEXT("InheritableOwnedTagsContainer"),
						TEXT("InheritableBlockedAbilityTagsContainer"),
						TEXT("OngoingTagRequirements"),
						TEXT("ApplicationTagRequirements"),
						TEXT("RemovalTagRequirements"),
						TEXT("RemoveGameplayEffectsWithTags"),
						TEXT("GrantedApplicationImmunityTags"),
						TEXT("GrantedApplicationImmunityQuery"),
						TEXT("RemoveGameplayEffectQuery")
					};

					for (const TCHAR* Field : Superseded) {
						const FProperty* Old = FindFProperty<FProperty>(Effects, Field);

						if (Old == nullptr || Properties->HasField(Field)) continue;

						Old->CopyCompleteValue_InContainer(Object, Inherited);
					}
				}

				*Stamped->ContainerPtrToValuePtr<uint8>(Object) = 0;

				if (const FArrayProperty* Parts = FindFProperty<FArrayProperty>(Effects, TEXT("GEComponents"))) {
					if (const FObjectPropertyBase* Each = CastField<FObjectPropertyBase>(Parts->Inner)) {
						FScriptArrayHelper Held(Parts, Parts->ContainerPtrToValuePtr<void>(Object));

						for (int32 At = 0; At < Held.Num(); ++At) {
							UObject* Part = Each->GetObjectPropertyValue(Held.GetRawPtr(At));

							if (Part == nullptr || Part->GetTypedOuter(const_cast<UClass*>(Effects)) == Object) continue;

							UE_LOG(LogReflection, Display, TEXT("\"%s\" was kept outside the effect whose list it is in, and was moved under it"), *Part->GetName());

							Part->Rename(nullptr, Object, REN_DontCreateRedirectors | REN_NonTransactional);
						}
					}
				}

#if ENGINE_UE5
				const UObject* Made = Object->GetArchetype();

				if (Made != nullptr && Made != Object && Made->IsA(Effects)) {
#if UE5_1_BEYOND
					FPostCDOCompiledContext Brought;

					Object->PostCDOCompiled(Brought);
#else
					Object->PostCDOCompiled();
#endif

					Stamped->CopyCompleteValue_InContainer(Object, Effects->GetDefaultObject());
				} else {
					FImportIssues::Report(
						EImportIssue::Data,
						FString::Printf(TEXT("\"%s\" was not brought forward"), *Object->GetName()),
						TEXT("An effect keeps its tags in the old places and the engine moves them into components of their own, reading the effect this one was made from as it goes. That one is not settled yet, which happens where a reference opened this import partway through another. The tags are left where they were, so the effect grants none of them, and importing it on its own will bring them forward.")
					);
				}
#endif
			}
		}
	}

	{
		static const UClass* Sprites = FindObject<UClass>(nullptr, TEXT("/Script/Paper2D.PaperSprite"));

		if (Sprites != nullptr && Object != nullptr && Object->IsA(Sprites)) {
			UObject* Cut = Object;

			const FObjectPropertyBase* Sheet = FindFProperty<FObjectPropertyBase>(Sprites, TEXT("BakedSourceTexture"));
			const FSoftObjectProperty* From = FindFProperty<FSoftObjectProperty>(Sprites, TEXT("SourceTexture"));
			const FStructProperty* BakedAt = FindFProperty<FStructProperty>(Sprites, TEXT("BakedSourceUV"));
			const FStructProperty* At = FindFProperty<FStructProperty>(Sprites, TEXT("SourceUV"));
			const FStructProperty* BakedBig = FindFProperty<FStructProperty>(Sprites, TEXT("BakedSourceDimension"));
			const FStructProperty* Big = FindFProperty<FStructProperty>(Sprites, TEXT("SourceDimension"));

			const UScriptStruct* Flat = TBaseStructure<FVector2D>::Get();

			const bool bKnown = Sheet != nullptr && From != nullptr
				&& At != nullptr && BakedAt != nullptr && At->Struct == Flat && BakedAt->Struct == Flat
				&& Big != nullptr && BakedBig != nullptr && Big->Struct == Flat && BakedBig->Struct == Flat;

			if (bKnown) {
				UObject* Texture = Sheet->GetObjectPropertyValue_InContainer(Cut);

				const bool bAsked = !From->GetPropertyValue_InContainer(Cut).IsNull();

				const bool bFits = Texture != nullptr && From->PropertyClass != nullptr && Texture->IsA(From->PropertyClass);

				if (!bAsked && bFits) {
					From->SetPropertyValue_InContainer(Cut, FSoftObjectPtr(Texture));

					At->Struct->CopyScriptStruct(At->ContainerPtrToValuePtr<void>(Cut), BakedAt->ContainerPtrToValuePtr<void>(Cut));
					Big->Struct->CopyScriptStruct(Big->ContainerPtrToValuePtr<void>(Cut), BakedBig->ContainerPtrToValuePtr<void>(Cut));
				}
			}
		}
	}

	if (Cast<UStaticMeshComponent>(Object)
		|| Cast<UParticleSystem>(Object)
		|| Cast<UParticleLODLevel>(Object)
		|| Cast<UParticleModule>(Object)
		|| Cast<UParticleEmitter>(Object)
		|| Cast<UMovieSceneSection>(Object)) {
		Object->PostEditImport();
	}

#if 0
	if (UParticleModuleTypeDataGpu* ParticleModuleTypeDataGPU = Cast<UParticleModuleTypeDataGpu>(Object)) {
		ParticleModuleTypeDataGPU->GetOutermost()->bIsCookedForEditor = true;
	}
#endif

	if (UBrushComponent* BrushComponent = Cast<UBrushComponent>(Object)) {
		FVolumeBrush::Rebuild(BrushComponent, Properties, PropertySerializer);
	}

	if (ABrush* BrushActor = Cast<ABrush>(Object)) {
		FVolumeBrush::Rebuild(BrushActor);
	}

	if (AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(Object)) {
		FFoliageActor::Rebuild(FoliageActor, PropertySerializer);
	}

	if (Properties->HasField(TEXT("LODData")) && Cast<UStaticMeshComponent>(Object)) {
		UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Object);
		if (!StaticMeshComponent) return;

		TArray<TSharedPtr<FJsonValue>> ObjectLODData = Properties->GetArrayField(TEXT("LODData"));
		int CurrentLOD = -1;

		int32 ColouredLods = 0;
		bool bPaintedEveryLod = true;

		for (const auto& CurrentLODValue : ObjectLODData) {
			CurrentLOD++;

			const TSharedPtr<FJsonObject> CurrentLODObject = CurrentLODValue->AsObject();

			if (!CurrentLODObject->HasField(TEXT("OverrideVertexColors"))) continue;

			const TSharedPtr<FJsonObject> OverrideVertexColorsObject = CurrentLODObject->GetObjectField(TEXT("OverrideVertexColors"));

			if (!OverrideVertexColorsObject->HasField(TEXT("Data"))) continue;

			const int32 NumVertices = OverrideVertexColorsObject->GetIntegerField(TEXT("NumVertices"));
			const TArray<TSharedPtr<FJsonValue>> DataArray = OverrideVertexColorsObject->GetArrayField(TEXT("Data"));

			TArray<FPaintedVertex> Painted = BuildPaintedVertices(StaticMeshComponent->GetStaticMesh(), CurrentLOD, DataArray);

			ColouredLods++;
			bPaintedEveryLod &= Painted.Num() > 0;

			const TArray<FColor> Repainted = RepaintOverrideColors(StaticMeshComponent->GetStaticMesh(), CurrentLOD, Painted);

			FString Output = FString::Printf(TEXT("CustomLODData LOD=%d ColorVertexData(%d)=("), CurrentLOD, Repainted.Num() > 0 ? Repainted.Num() : NumVertices);

			if (Repainted.Num() > 0) {
				for (int32 i = 0; i < Repainted.Num(); ++i) {
					const FColor& Color = Repainted[i];

					Output.Append(FString::Printf(TEXT("%02X%02X%02X%02X"), Color.A, Color.R, Color.G, Color.B));

					if (i < Repainted.Num() - 1) {
						Output.Append(TEXT(","));
					}
				}
			} else {
				for (int32 i = 0; i < DataArray.Num(); ++i) {
					FString ColorValue = DataArray.operator[](i)->AsString();
					Output.Append(ColorValue);

					if (i < DataArray.Num() - 1) {
						Output.Append(TEXT(","));
					}
				}
			}

			Output.Append(TEXT(")"));

			StaticMeshComponent->ImportCustomProperties(*Output, GWarn);

			if (StaticMeshComponent->LODData.IsValidIndex(CurrentLOD)) {
				StaticMeshComponent->LODData[CurrentLOD].PaintedVertices = MoveTemp(Painted);
			}
		}

		if (ColouredLods > 1 && bPaintedEveryLod) {
			StaticMeshComponent->bCustomOverrideVertexColorPerLOD = true;
		}

	}
}

#if UE5_2_BEYOND
UE_ENABLE_OPTIMIZATION
#else
PRAGMA_ENABLE_OPTIMIZATION
#endif
