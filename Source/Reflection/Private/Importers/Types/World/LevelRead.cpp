#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/World/LevelRead.h"

#include "Containers/Export.h"
#include "Containers/ExportContainer.h"
#include "Engine/Compatibility.h"
#if UE4_25_BELOW
#include "AssetRegistryModule.h"
#else
#include "AssetRegistry/AssetRegistryModule.h"
#endif
#include "Importers/Constructor/Asset.h"
#include "Importers/Constructor/ImportIssues.h"
#include "Importers/Constructor/Types.h"
#include "Modules/Toolbar/Tools/ImportFromPath.h"
#include "Settings/ReflectionSettings.h"
#include "Settings/SettingsAccess.h"
#include "Utilities/JsonHelpers.h"

#include "Engine/EngineUtilities.h"
#include "Engine/Log.h"
#include "Engine/Package.h"
#include "Utilities/AssetPaths.h"
#include "Importers/Constructor/TypesHelper.h"
#include "Modules/Toolbar/Tools/ImportFromPath.h"
#include "ThumbnailRendering/WorldThumbnailInfo.h"

#include "Editor.h"
#include "Engine/Brush.h"
#include "Engine/Level.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

#if UE5_5_BEYOND
#include "HLOD/HLODSetup.h"
#endif
#include "Engine/LODActor.h"
#include "Engine/HLODProxy.h"
#if !UE4_25_BELOW
#include "HLOD/HLODProxyDesc.h"
#endif
#include "HierarchicalLOD.h"

#if UE4_26
#include "Importers/Constructor/Importer.h"
#endif

static const FName GActorOuter = TEXT("PersistentLevel");

namespace {
	FString NamedClass(const FUObjectExport* Export) {
		FString Named = Export->GetString(TEXT("Class"));

		if (Named.Contains(TEXT("'"))) {
			Named.Split(TEXT("'"), nullptr, &Named);
			Named.Split(TEXT("'"), &Named, nullptr);
		}

		return Named;
	}

	bool IsBlueprintMade(const FUObjectExport* Export) {
		return NamedClass(Export).Contains(TEXT("/"));
	}

	FString DecidesClass(const FUObjectExport* Export) {
		if (Export->Has(TEXT("Template"))) {
			const FString Named = Export->GetObject(TEXT("Template")).GetString(TEXT("ObjectName"));

			if (!Named.IsEmpty()) {
				return Named;
			}
		}

		const FString Named = Export->GetString(TEXT("Class"));

		return Named.IsEmpty() ? Export->GetType().ToString() : Named;
	}

	FName NamedExport(const FUObjectJsonValueExport& Reference) {
		const FString Named = Reference.GetString(TEXT("ObjectName"));

		return Named.IsEmpty() ? FName() : FName(*GetObjectNameFromOuter(Named));
	}

	FString AssetOfClass(const FUObjectExport* Export) {
		FString Asset = NamedClass(Export);

		Asset.Split(TEXT("."), &Asset, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);

		return Asset;
	}
}

bool FLevelRead::Handles(const TArray<TSharedPtr<FJsonValue>>& Exports) {
	for (const TSharedPtr<FJsonValue>& Value : Exports) {
		const TSharedPtr<FJsonObject> Export = Value.IsValid() ? Value->AsObject() : nullptr;

		if (!Export.IsValid()) continue;

		if (FString Type; Export->TryGetStringField(TEXT("Type"), Type) && Type == TEXT("World")) {
			return true;
		}
	}

	return false;
}

bool FLevelRead::FromCloud(const TArray<TSharedPtr<FJsonValue>>& Exports, const FString& Path, FRLevelReadResult* OutResult) {
	if (!ImportTypes::Allowed(TEXT("World"))) {
		FImportIssues::ReportFor(FPaths::GetBaseFilename(Path), Path, TEXT("World"), EImportIssue::Setting,
			TEXT("This type is behind Enable Experiments"),
			TEXT("Reading a level is experimental, and Reflection reads one once Enable Experiments is turned on in its settings."));

		return false;
	}

	FLevelRead Reading;

	const FRLevelReadResult Result = Reading.Read(Exports, Path);

	if (OutResult != nullptr) {
		*OutResult = Result;
	}

	return true;
}

bool FLevelRead::Allows(const FUObjectExport* Export, bool& bOutMayFetch) const {
	const ERLevelActors Wanted = GetSettings()->Level.Actors;

	bOutMayFetch = false;

	switch (Wanted) {
		case ERLevelActors::Everything:
			bOutMayFetch = true;

			return true;

		case ERLevelActors::NoBlueprints:
		case ERLevelActors::StaticMeshesOnly:
			return !IsBlueprintMade(Export);

		case ERLevelActors::HLODsOnly:
			return Export->GetType() == TEXT("LODActor");

		default:
			return false;
	}
}

namespace {
	bool Spawnable(const UClass* Class) {
		if (!ClassIsFormed(Class)) return false;

		return !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated);
	}
}

UClass* FLevelRead::ClassOf(FUObjectExport* Export, const bool bMayFetch, TMap<FString, UClass*>& Known, FRLevelReadResult& Result) const {
	const FString Named = DecidesClass(Export);

	if (UClass** Cached = Known.Find(Named)) {
		return *Cached;
	}

	if (UClass* Standing = Export->GetClass()) {
		Known.Add(Named, Standing);

		return Standing;
	}

	if (!bMayFetch || !IsBlueprintMade(Export)) {
		Known.Add(Named, nullptr);

		return nullptr;
	}

	const FString Asset = AssetOfClass(Export);

	if (Asset.IsEmpty()) {
		Known.Add(Named, nullptr);

		return nullptr;
	}

	if (!TToolImportFromPath::Import(Asset)) {
		FImportIssues::Report(EImportIssue::MissingAsset, TEXT("Couldn't reflect ") + Asset, TEXT("Named as the class of an actor in this level"));
	}

	UClass* Fetched = Export->GetClass();

	Known.Add(Named, Fetched);

	return Fetched;
}

UWorld* FLevelRead::LevelFor(UPackage* Package) const {
	if (UWorld* Standing = UWorld::FindWorldInPackage(Package)) {
		int32 Removed = 0;

		TArray<AActor*> Held;

		if (const ULevel* Level = Standing->PersistentLevel) {
			Held = Level->Actors;
		}

		for (AActor* Actor : Held) {
			if (Actor == nullptr || Actor->IsA<AWorldSettings>() || Actor == Standing->GetDefaultBrush()) continue;

			if (Standing->EditorDestroyActor(Actor, true)) Removed++;
		}

		UE_LOG(LogReflection, Display, TEXT("reading over the level already at \"%s\", %d actor(s) cleared"), *Package->GetName(), Removed);

		return Standing;
	}

	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.ShouldSimulatePhysics(false)
		.EnableTraceCollision(true)
		.CreateNavigation(false)
		.CreateAISystem(false);

	#if ENGINE_UE5
	UWorld* Made = UWorld::CreateWorld(EWorldType::Inactive, false, FName(*FPackageName::GetShortName(Package)),
		Package,   false, ERHIFeatureLevel::Num, &Values);
	#else
	UWorld* Made = UWorld::CreateWorld(EWorldType::Inactive, false, FName(*FPackageName::GetShortName(Package)),
		Package,   false, ERHIFeatureLevel::Num);
	#endif

	if (Made != nullptr) {
		Made->SetFlags(RF_Public | RF_Standalone);

		if (GEditor != nullptr) GEditor->InitBuilderBrush(Made);

		Made->ThumbnailInfo = NewObject<UWorldThumbnailInfo>(Made, NAME_None, RF_Transactional);
	}

	return Made;
}

void FLevelRead::ReadWorldSettings(UWorld* World, FUObjectExport* Export) const {
	AWorldSettings* Settings = World != nullptr ? World->GetWorldSettings() : nullptr;

	if (Settings == nullptr) {
		FImportIssues::Report(EImportIssue::Failed, TEXT("The level has no world settings to write onto"));

		return;
	}

	GetObjectSerializer()->Parent = Settings;
	GetObjectSerializer()->DeserializeObjectProperties(Export->GetProperties(), Settings);

	Settings->Modify();
	Settings->PostEditChange();

	UE_LOG(LogReflection, Display, TEXT("world settings read onto \"%s\""), *Settings->GetName());
}

void FLevelRead::SubLevelsNamed(const TArray<TSharedPtr<FJsonValue>>& Exports, FUObjectExport* World, TArray<FString>& OutNamed) {
	const auto Named = [&OutNamed](FString Path) {
		if (Path.IsEmpty()) return;

		FString Package;
		Path = Path.Split(TEXT("."), &Package, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd) ? Package : Path;

		if (!Path.IsEmpty()) OutNamed.AddUnique(Path);
	};

	for (const TSharedPtr<FJsonValue>& Value : Exports) {
		const TSharedPtr<FJsonObject> Export = Value.IsValid() ? Value->AsObject() : nullptr;

		if (!Export.IsValid()) continue;

		const TSharedPtr<FJsonObject>* Properties;

		if (!Export->TryGetObjectField(TEXT("Properties"), Properties) || !Properties->IsValid()) continue;

		const TArray<TSharedPtr<FJsonValue>>* Worlds;

		if (!(*Properties)->TryGetArrayField(TEXT("AdditionalWorlds"), Worlds)) continue;

		for (const TSharedPtr<FJsonValue>& Each : *Worlds) {
			const TSharedPtr<FJsonObject> Entry = Each.IsValid() ? Each->AsObject() : nullptr;

			if (!Entry.IsValid()) continue;

			FString Asset;

			if (Entry->TryGetStringField(TEXT("AssetPathName"), Asset)) Named(Asset);
		}
	}

	if (World == nullptr || !World->Has(TEXT("StreamingLevels"))) {
		return;
	}

	for (const FUObjectJsonValueExport& Each : World->GetArray(TEXT("StreamingLevels"))) {
		FString Package = Each.GetString(TEXT("PackageName"));

		if (Package.IsEmpty()) Package = Each.GetString(TEXT("ObjectPath"));

		Named(Package);
	}
}

void FLevelRead::ReadHLODSetup(UWorld* World, const TArray<ALODActor*>& Actors) const {
	AWorldSettings* Settings = World != nullptr ? World->GetWorldSettings() : nullptr;

	#if !UE4_26
	if (Settings == nullptr || Actors.Num() == 0) return;
	#else
	if (Settings == nullptr || Actors.Max() == 0) return;
	#endif

	int32 Levels = 0;

	for (const ALODActor* Actor : Actors) {
		if (Actor != nullptr) Levels = FMath::Max(Levels, Actor->LODLevel);
	}

	if (Levels <= 0) return;

	TArray<FHierarchicalSimplification>& Setup = Settings->GetHierarchicalLODSetup();

	if (Setup.Num() >= Levels) return;

	Setup.SetNum(Levels);

	for (const ALODActor* Actor : Actors) {
		if (Actor == nullptr || !Setup.IsValidIndex(Actor->LODLevel - 1)) continue;

		FHierarchicalSimplification& Level = Setup[Actor->LODLevel - 1];

		Level.bUseOverrideDrawDistance = true;
		Level.OverrideDrawDistance = Actor->GetLODDrawDistance();
	}

	Settings->MarkPackageDirty();

	UE_LOG(LogReflection, Display, TEXT("%d HLOD level(s) said on the world settings"), Levels);
}

void FLevelRead::ReadHLODs(UWorld* World, const TArray<ALODActor*>& Actors, FRLevelReadResult& Result) const {

	#if !UE4_26
	if (World == nullptr || Actors.Num() == 0) return;
	#else
	if (World == nullptr || Actors.Max() == 0) return;
	#endif

	TSet<UHLODProxy*> Proxies;

	for (const ALODActor* Actor : Actors) {
		if (Actor != nullptr && Actor->GetProxy() != nullptr) Proxies.Add(Actor->GetProxy());
	}

	for (UHLODProxy* Proxy : Proxies) {
		Proxy->SetMap(World);
		Proxy->MarkPackageDirty();
	}

#if !UE4_25_BELOW
	if (!GetDefault<UHierarchicalLODSettings>()->bSaveLODActorsToHLODPackages) {
		return;
	}
#endif

	const FObjectProperty* Held = FindFProperty<FObjectProperty>(ALODActor::StaticClass(), TEXT("ProxyDesc"));
	const FMapProperty* Described = FindFProperty<FMapProperty>(UHLODProxy::StaticClass(), TEXT("HLODActors"));

	if (Held == nullptr || Described == nullptr) return;

	const FObjectProperty* Describes = CastField<FObjectProperty>(Described->KeyProp);
	const FStructProperty* Beside = CastField<FStructProperty>(Described->ValueProp);

	if (Describes == nullptr || Beside == nullptr || Beside->Struct == nullptr) return;

	const FNameProperty* Built = FindFProperty<FNameProperty>(Beside->Struct, TEXT("Key"));

	if (Built == nullptr) return;

	TMap<FName, UObject*> ByKey;

	for (UHLODProxy* Proxy : Proxies) {
		FScriptMapHelper Walk(Described, Described->ContainerPtrToValuePtr<void>(Proxy));

		for (int32 At = 0; At < Walk.GetMaxIndex(); ++At) {
			if (!Walk.IsValidIndex(At)) continue;

			UObject* Desc = Describes->GetObjectPropertyValue(Walk.GetKeyPtr(At));

			if (Desc == nullptr) continue;

			const FName Key = Built->GetPropertyValue_InContainer(Walk.GetValuePtr(At));

			if (!Key.IsNone()) ByKey.Add(Key, Desc);
		}
	}

	TMap<ALODActor*, UObject*> Tie;

	for (ALODActor* Actor : Actors) {
		if (Actor == nullptr || Actor->GetProxy() == nullptr) continue;

		if (UObject** Standing = ByKey.Find(Actor->GetKey()); Standing != nullptr && *Standing != nullptr) {
			Tie.Add(Actor, *Standing);
		}
	}

	for (bool bSettled = false; !bSettled; ) {
		bSettled = true;

		for (const TPair<ALODActor*, UObject*>& Pair : Tie) {
			bool bStands = true;

			for (AActor* Under : Pair.Key->SubActors) {
				if (const ALODActor* Below = Cast<ALODActor>(Under); Below != nullptr && !Tie.Contains(Below)) {
					bStands = false;

					break;
				}
			}

			if (!bStands) {
				Tie.Remove(Pair.Key);

				bSettled = false;

				break;
			}
		}
	}

	for (ALODActor* Actor : Actors) {
		if (Actor == nullptr || Actor->GetProxy() == nullptr) continue;

		UObject** Standing = Tie.Find(Actor);

		if (Standing == nullptr) {
			Result.HLODsUntied++;

			continue;
		}

		Held->SetObjectPropertyValue_InContainer(Actor, *Standing);

		Result.HLODsTied++;
	}

	TSet<UObject*> Claimed;

	for (const TPair<ALODActor*, UObject*>& Pair : Tie) {
		Claimed.Add(Pair.Value);
	}

	for (UHLODProxy* Proxy : Proxies) {
		bool bWhole = true;

		FScriptMapHelper Walk(Described, Described->ContainerPtrToValuePtr<void>(Proxy));

		for (int32 At = 0; At < Walk.GetMaxIndex() && bWhole; ++At) {
			if (!Walk.IsValidIndex(At)) continue;

			UObject* Desc = Describes->GetObjectPropertyValue(Walk.GetKeyPtr(At));

			bWhole = Desc != nullptr && Claimed.Contains(Desc);
		}

		if (!bWhole) {
			FImportIssues::Report(EImportIssue::Data,
				TEXT("The HLOD proxy was left as it came"),
				FString::Printf(TEXT("\"%s\" describes HLOD actors this level could not account for, and writing it back would take their geometry with it."),
					*Proxy->GetName()));

			continue;
		}

#if !UE4_25_BELOW
		Proxy->UpdateHLODDescs(World->PersistentLevel);
#endif

		if (UPackage* Written = Proxy->GetOutermost(); Written != nullptr && GetSettings()->AssetSettings.SaveAssets) {
			SavePackage(Written);
		}
	}

	UE_LOG(LogReflection, Display, TEXT("%d HLOD actor(s) tied to a description, %d with none, across %d proxy(s)"),
		Result.HLODsTied, Result.HLODsUntied, Proxies.Num());
}

void FLevelRead::ReadSubLevels(const TArray<FString>& Named, FRLevelReadResult& Result) const {
	for (const FString& Package : Named) {
		if (PackageHoldsAsset(Package)) continue;

		if (TToolImportFromPath::Import(ToCloudPackagePath(Package))) {
			Result.SubLevels++;

			continue;
		}

		Result.MissingSubLevels++;

		FImportIssues::Report(EImportIssue::MissingAsset,
			TEXT("Couldn't read the level ") + FPackageName::GetShortName(Package),
			TEXT("Named at ") + Package);
	}
}

FRLevelReadResult FLevelRead::Read(const TArray<TSharedPtr<FJsonValue>>& Exports, const FString& Path) {
	FRLevelReadResult Result;

	const FRLevelSettings& Options = GetSettings()->Level;

	const FString Name = FPaths::GetBaseFilename(Path);

	FImportIssues::Push(Name, Path, TEXT("World"));

	if (Options.Actors == ERLevelActors::HLODsOnly && !Options.LODActors) {
		FImportIssues::Report(EImportIssue::Setting,
			TEXT("Nothing will be placed"),
			TEXT("The actors setting asks for HLOD actors only and the LOD Actors switch leaves them out, so the two together allow nothing."));
	}

	FString FailureReason;

	UPackage* Package = FAssetUtilities::CreateAssetPackage(Name, Path, FailureReason);

	if (Package == nullptr) {
		FImportIssues::Report(EImportIssue::Failed, TEXT("Couldn't make a package for the level"), FailureReason);
		FImportIssues::Pop();

		return Result;
	}

	UWorld* World = LevelFor(Package);

	if (World == nullptr) {
		FImportIssues::Report(EImportIssue::Failed, TEXT("Couldn't make a level in that package"));
		FImportIssues::Pop();

		return Result;
	}

	if (GEditor != nullptr && World == GEditor->GetEditorWorldContext().World()) {
		FImportIssues::Report(EImportIssue::Failed,
			TEXT("This level is the one open in the editor"),
			TEXT("Open another level first, and read this one again."));

		FImportIssues::Pop();

		return Result;
	}

	SetPackage(Package);

	Result.Level = Package->GetName();

	CreateSerializer();

	FUObjectExportContainer Container(Exports);

	GetObjectSerializer()->Exports = Exports;
	GetObjectSerializer()->GetPropertySerializer()->ExportsContainer = &Container;

	static const TSet<FName> Unreconstructable = { TEXT("Landscape"), TEXT("LandscapeStreamingProxy") };

	TMap<FString, UClass*> Known;

	TArray<TPair<FUObjectExport*, AActor*>> Placed;

	TArray<ALODActor*> HLODs;

	FUObjectExport* WorldExport = nullptr;
	FUObjectExport* LevelExport = nullptr;
	FUObjectExport* WorldSettingsExport = nullptr;

	for (FUObjectExport* Export : Container.Exports) {
		if (!Export->IsJsonValid()) continue;

		if (Export->GetType() == TEXT("World")) WorldExport = Export;
		else if (Export->GetType() == TEXT("Level")) LevelExport = Export;
	}

	FName WorldSettingsNamed;
	FName LevelScriptNamed;

	if (LevelExport != nullptr) {
		const FUObjectJsonValueExport Says = LevelExport->GetPropertiesAsValue();

		WorldSettingsNamed = NamedExport(Says.GetObject(TEXT("WorldSettings")));
		LevelScriptNamed = NamedExport(Says.GetObject(TEXT("LevelScriptActor")));
	}

	for (FUObjectExport* Export : Container.Exports) {
		if (!Export->IsJsonValid()) continue;

		if (Export->GetOuter() != GActorOuter) continue;

		if (Unreconstructable.Contains(Export->GetType())) continue;

		if (Export->GetName().ToString().StartsWith(TEXT("Default__"))) continue;

		if (!LevelScriptNamed.IsNone() && Export->GetName() == LevelScriptNamed) continue;

		if (!WorldSettingsNamed.IsNone() && Export->GetName() == WorldSettingsNamed) {
			WorldSettingsExport = Export;

			continue;
		}

		bool bMayFetch = false;

		if (!Allows(Export, bMayFetch)) {
			Result.LeftOut++;

			continue;
		}

		UClass* ActorClass = ClassOf(Export, bMayFetch, Known, Result);

		if (ActorClass == nullptr) {
			if (IsBlueprintMade(Export)) {
				Result.MissingBlueprints++;

				FImportIssues::Report(EImportIssue::MissingClass, TEXT("No class for ") + NamedClass(Export), TEXT("Actors of it were left out of the level"));
			} else {
				Result.UnusableClasses++;

				FImportIssues::Report(EImportIssue::MissingClass, TEXT("This engine has no ") + NamedClass(Export), TEXT("Actors of it were left out of the level"));
			}

			continue;
		}

		if (!ActorClass->IsChildOf(AActor::StaticClass())) continue;

		if (!Spawnable(ActorClass)) {
			Result.MissingBlueprints++;

			FImportIssues::Report(EImportIssue::MissingClass,
				TEXT("Nothing was made of ") + NamedClass(Export),
				TEXT("Its class is in memory but was never finished, so actors of it were left out rather than spawned."));

			continue;
		}

		if (!Options.LODActors && ActorClass->GetName() == TEXT("LODActor")) {
			Result.LeftOut++;

			continue;
		}

		if (Options.Actors == ERLevelActors::StaticMeshesOnly && !ActorClass->IsChildOf(AStaticMeshActor::StaticClass())) {
			Result.LeftOut++;

			continue;
		}

		FActorSpawnParameters Named;

		Named.Name = Export->GetName();
#if !UE4_24_BELOW
		Named.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
#endif

		AActor* Actor = World->SpawnActor<AActor>(ActorClass, Named);

		if (Actor == nullptr) {
			Result.Failed++;

			FImportIssues::Report(EImportIssue::Failed, TEXT("The level wouldn't take an actor of ") + ActorClass->GetName());

			continue;
		}

		Actor->SetActorLabel(Export->GetName().ToString());

		Export->Object = Actor;

		Placed.Add(TPair<FUObjectExport*, AActor*>(Export, Actor));
	}

	for (const TPair<FUObjectExport*, AActor*>& Pair : Placed) {
		GetObjectSerializer()->Parent = Pair.Value;

		GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(Pair.Key->GetProperties(), {
			"MinDrawDistance",
			"Layers",
			"ResourceType"
		}), Pair.Value);

		Pair.Value->Modify();
		Pair.Value->PostEditChange();

		if (Pair.Value->GetRootComponent() != nullptr) {
			Pair.Value->GetRootComponent()->ConditionalUpdateComponentToWorld();
		}

		Pair.Value->PostLoad();

		Result.Placed++;

		if (ALODActor* Stand = Cast<ALODActor>(Pair.Value)) {
			HLODs.Add(Stand);
		}
	}

	for (const TPair<FUObjectExport*, AActor*>& Pair : Placed) {
		TInlineComponentArray<USceneComponent*> Components;
		Pair.Value->GetComponents(Components);

		for (USceneComponent* Component : Components) {
			USceneComponent* Above = Component->GetAttachParent();

			if (Above == nullptr || Above == Component || Above->GetAttachChildren().Contains(Component)) continue;

			Component->AttachToComponent(Above, FAttachmentTransformRules::KeepRelativeTransform, Component->GetAttachSocketName());
		}
	}

	if (Options.WorldSettings && WorldSettingsExport != nullptr) {
		ReadWorldSettings(World, WorldSettingsExport);
	}

	if (Options.LODActors) {
		ReadHLODSetup(World, HLODs);

		ReadHLODs(World, HLODs, Result);
	}

	TArray<FString> SubLevels;

	if (Options.SubLevels) {
		SubLevelsNamed(Exports, WorldExport, SubLevels);
	}

	if (Result.Missing() > 0 || Result.LeftOut > 0) {
		const int32 Listed = Result.Placed + Result.Missing() + Result.LeftOut;

		FImportIssues::Report(
			Result.Missing() > 0 ? EImportIssue::MissingAsset : EImportIssue::Setting,
			FString::Printf(TEXT("%d of %d actor(s) are not in the level"), Result.Missing() + Result.LeftOut, Listed),
			FString::Printf(TEXT("%d missing a blueprint, %d missing a class, %d the level refused, %d left out by the level settings"),
				Result.MissingBlueprints, Result.UnusableClasses, Result.Failed, Result.LeftOut)
		);
	}

	UE_LOG(LogReflection, Display, TEXT("\"%s\": %d actor(s) placed, %d missing, %d left out (actors=%s, sub levels=%s, world settings=%s, lod actors=%s)"),
		*Name, Result.Placed, Result.Missing(), Result.LeftOut,
		*StaticEnum<ERLevelActors>()->GetNameStringByValue(static_cast<int64>(Options.Actors)),
		Options.SubLevels ? TEXT("on") : TEXT("off"),
		Options.WorldSettings ? TEXT("on") : TEXT("off"),
		Options.LODActors ? TEXT("on") : TEXT("off"));

	FAssetRegistryModule::AssetCreated(World);

	World->MarkPackageDirty();
	Package->SetDirtyFlag(true);

	BrowseToAsset(World);

	if (GetSettings()->AssetSettings.SaveAssets) {
		SavePackage(Package);
	} else {
		FImportIssues::Report(EImportIssue::Setting,
			TEXT("The level was read but not saved"),
			TEXT("Save Assets is off, and a level has to be on disk before it can be opened."));
	}

	FImportIssues::Pop();

	#if !UE4_26
	if (SubLevels.Num() > 0) {
	#else
	if (SubLevels.Max() != 0) {
	#endif
		ReadSubLevels(SubLevels, Result);

		UE_LOG(LogReflection, Display, TEXT("\"%s\" brings in %d level(s): %d read, %d already here, %d could not be"),
			*Name, SubLevels.Num(), Result.SubLevels, SubLevels.Num() - Result.SubLevels - Result.MissingSubLevels,
			Result.MissingSubLevels);
	}

	return Result;
}
