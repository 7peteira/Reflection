#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/ImportReader.h"

#include "UObject/MetaData.h"

#include "Importers/Constructor/ImportIssues.h"
#include "Importers/Constructor/Importer.h"
#include "Importers/Constructor/TemplatedImporter.h"
#include "Importers/Constructor/Types.h"
#include "Importers/Types/DataAssetImporter.h"
#include "Importers/Types/Texture/TextureImporter.h"
#include "Settings/Runtime.h"
#include "Styling/SlateIconFinder.h"
#include "Importers/Constructor/Asset.h"
#include "Engine/EngineUtilities.h"
#include "Utilities/JsonHelpers.h"
#include "Modules/Cloud/Remote.h"
#include "UObject/UObjectHash.h"
#include "Settings/SettingsAccess.h"

#if REFLECTION_RIGVM
#include "Engine/ControlRigCompatibility.h"
#endif

namespace {
	void ReadSiblings(const TSharedPtr<FJsonValue>& Value, const FString& Package, const int32 Count, TSet<int32>& Out) {
		if (!Value.IsValid()) return;

		if (Value->Type == EJson::Object) {
			const TSharedPtr<FJsonObject> Object = Value->AsObject();

			FString Path;

			if (Object->TryGetStringField(TEXT("ObjectPath"), Path)) {
				FString Named, Index;

				if (Path.Split(TEXT("."), &Named, &Index, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
					&& Named == Package && Index.IsNumeric()) {
					const int32 At = FCString::Atoi(*Index);

					if (At >= 0 && At < Count) Out.Add(At);
				}
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values) {
				ReadSiblings(Pair.Value, Package, Count, Out);
			}

			return;
		}

		if (Value->Type == EJson::Array) {
			for (const TSharedPtr<FJsonValue>& One : Value->AsArray()) {
				ReadSiblings(One, Package, Count, Out);
			}
		}
	}

	TArray<FUObjectExport*> InDependencyOrder(FUObjectExportContainer* Container) {
		const int32 Count = Container->Exports.Num();

		TArray<FUObjectExport*> Ordered;
		Ordered.Reserve(Count);

		if (Count < 2) {
			Ordered = Container->Exports;

			return Ordered;
		}

		FString Package;

		for (const FUObjectExport* Export : Container->Exports) {
			if (Export->IsJsonValid() && Export->JsonObject->TryGetStringField(TEXT("Package"), Package)) break;
		}

		if (Package.IsEmpty()) {
			Ordered = Container->Exports;

			return Ordered;
		}

		TArray<TSet<int32>> Wants;
		Wants.SetNum(Count);

		for (int32 Index = 0; Index < Count; ++Index) {
			const FUObjectExport* Export = Container->Exports[Index];

			if (!Export->IsJsonValid()) continue;

			ReadSiblings(MakeShared<FJsonValueObject>(Export->JsonObject), Package, Count, Wants[Index]);

			Wants[Index].Remove(Index);
		}

		TArray<uint8> Done;
		Done.SetNumZeroed(Count);

		TArray<int32> Open;

		TFunction<void(int32)> Walk = [&](const int32 Index) {
			if (Done[Index] != 0 || Open.Contains(Index)) return;

			Open.Push(Index);

			for (const int32 Wanted : Wants[Index]) {
				Walk(Wanted);
			}

			Open.Pop();

			Done[Index] = 1;

			Ordered.Add(Container->Exports[Index]);
		};

		for (int32 Index = 0; Index < Count; ++Index) {
			Walk(Index);
		}

		return Ordered;
	}
}

bool IImportReader::ReadExportsAndImport(const TArray<TSharedPtr<FJsonValue>>& Exports, const FString& File, IImporter*& OutImporter, const bool HideNotifications, bool bUseRelativePath) {
	const FBlockingRequestScope BlockingScope(FText::Format(
		NSLOCTEXT("Reflection", "CloudImporting", "Reflecting {0}"),
		FText::FromString(FPaths::GetCleanFilename(File))
	));

	const FAssetUtilities::FRunScope RunScope;

	FUObjectExportContainer* Container = new FUObjectExportContainer(Exports);

	const bool IsBlueprint = Container->FindByType(FString("BlueprintGeneratedClass"))->IsJsonValid();

	for (FUObjectExport* Export : InDependencyOrder(Container)) {
		if (IsBlueprint) {
			if (Export->GetType() != "BlueprintGeneratedClass") continue;
		}

		if (IImporter* Importer = ReadExportAndImport(Container, Export, File, HideNotifications, bUseRelativePath)) OutImporter = Importer;
	}

	if (OutImporter == nullptr) {
		const FString Name = FPaths::GetBaseFilename(File);

		FString Type;

		for (FUObjectExport* Export : Container->Exports) {
			const FString ExportType = Export->GetType().ToString();

			if (ExportType.IsEmpty()) continue;

			const FString ExportName = Export->GetName().ToString();

			if (ExportName == Name || ExportName == Name + TEXT("_C")) {
				Type = ExportType;

				break;
			}

			if (Type.IsEmpty()) Type = ExportType;
		}

		if (ImportTypes::Experimental.Contains(Type) && !GetSettings()->EnableExperiments) {
			FImportIssues::ReportFor(Name, File, Type, EImportIssue::Setting,
				TEXT("This type is behind Enable Experiments"),
				FString::Printf(
					TEXT("'%s' is a %s, which Reflection imports once Enable Experiments is turned on in its settings."),
					*Name, *Type)
			);

			return false;
		}

		FImportIssues::ReportFor(Name, File, Type, EImportIssue::MissingClass,
			FString::Printf(TEXT("Nothing here that can be imported")),
			FString::Printf(
				TEXT("'%s' is a %s, and this build has no importer for one. The file was read and nothing was made from it."),
				*Name, Type.IsEmpty() ? TEXT("type it doesn't recognise") : *Type)
		);

		return false;
	}

	return true;
}

IImporter* IImportReader::ReadExportAndImport(FUObjectExportContainer* Container, FUObjectExport* Export, FString File, const bool HideNotifications, bool bUseRelativePath) {
	const FString Type = Export->GetType().ToString();
	FString Name = Export->GetName().ToString();

	const bool IsBlueprint = Type.Contains("BlueprintGeneratedClass");

	if (IsBlueprint) {
		Name.Split("_C", &Name, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	}

	static const TArray<FString> HeldByTheirOuter = { TEXT("FontFace"), TEXT("NiagaraScript"), TEXT("NiagaraEmitter") };

	if (HeldByTheirOuter.Contains(Type) && Container->Find(Export->GetOuter())->IsJsonValid()) {
		return nullptr;
	}

	const UClass* Class = FindClassByType(Type);

#if REFLECTION_RIGVM
	if (Class == nullptr && Type == TEXT("CurveExpressionsDataAsset") && ImportsCurveMappingAsRig()) {
		Class = UControlRigBlueprint::StaticClass();
	}
#endif

	if (Class == nullptr) return nullptr;

	const bool InheritsDataAsset = Class->IsChildOf(UDataAsset::StaticClass());
	if (!CanImport(Type, false, Class)) return nullptr;

	if (bUseRelativePath) {
		if (FPaths::IsRelative(File)) File = FPaths::ConvertRelativePathToFull(File);
	}

	const FString PackageName = GetSettings()->AssetSettings.PackagedAssets == ERPackagedAssets::Separate
		? Name
		: FPaths::GetBaseFilename(File);

	FString FailureReason;
	UPackage* LocalPackage = FAssetUtilities::CreateAssetPackage(PackageName, File, FailureReason);

	if (LocalPackage == nullptr) {
        UReflectionSettings* PluginSettings = GetSettings();

		GReflectionRuntime.Update();
		LocalPackage = FAssetUtilities::CreateAssetPackage(Name, File, FailureReason);

		if (LocalPackage == nullptr) {
			FString ExportDirectoryCache = GReflectionRuntime.ExportDirectory.Path;

			if (FString DirectoryPathFix; File.Split(TEXT("Output/Exports/"), &DirectoryPathFix, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd)) {
				DirectoryPathFix = DirectoryPathFix + TEXT("Output/Exports");

				GReflectionRuntime.ExportDirectory.Path = DirectoryPathFix;
				SavePluginSettings(PluginSettings);

				LocalPackage = FAssetUtilities::CreateAssetPackage(Name, File, FailureReason);

				if (LocalPackage == nullptr) {
					GReflectionRuntime.ExportDirectory.Path = ExportDirectoryCache;

					SavePluginSettings(PluginSettings);
				}
			}
		}
	}

	if (LocalPackage == nullptr) {
		FImportIssues::ReportFor(Name, FString(), Type, EImportIssue::Failed, TEXT("Couldn't create a package for this asset"), FailureReason);

		return nullptr;
	}

	IImporter* Importer = nullptr;

	if (const FImporterFactoryDelegate* Factory = FindFactoryForAssetType(Type)) {
		Importer = (*Factory)();
	}

	if (Importer == nullptr && InheritsDataAsset) {
		Importer = new IDataAssetImporter();
	}

	if (Importer == nullptr) {
		Importer = new ITemplatedImporter<UObject>();
	}

	Export->Package = LocalPackage;
	Importer->Initialize(Export, Container);

	const FImportIssueScope IssueScope(Name, LocalPackage->GetName(), Type);

	bool Successful = false; {
		try {
			Successful = Importer->Import();
		} catch (const char* Exception) {
			UE_LOG(LogReflection, Error, TEXT("Importer exception: %s"), *FString(Exception));

			FImportIssues::Report(EImportIssue::Failed, TEXT("The importer threw partway through"), FString(Exception));
		}
	}

	if (!Successful) {
		FImportIssues::Report(EImportIssue::Failed, TEXT("The importer couldn't build this asset"));

		TArray<UObject*> Held;
		GetObjectsWithOuter(LocalPackage, Held, false);

		int32 Built = 0;

		for (const UObject* One : Held) {
			if (One == nullptr) continue;

#if !UE5_6_BEYOND
			if (One->IsA<UMetaData>()) continue;
#endif

			Built++;
		}

		if (Built == 0) {
			LocalPackage->SetDirtyFlag(false);
		}
	}

	if (HideNotifications) {
		return Importer;
	}

	FString ClassIconType = Type;

	if (Type.Contains("GeneratedClass")) {
		Type.Split("GeneratedClass", &ClassIconType, nullptr);
	}

	if (Successful) {
		UE_LOG(LogReflection, Log, TEXT("Successfully reflected \"%s\" as \"%s\""), *Name, *Type);

		AppendNotification(
			FText::FromString("Reflected: " + Name),
			FText::FromString(Type),
			2.0f,
			FSlateIconFinder::FindCustomIconBrushForClass(FindObject<UClass>(nullptr, *("/Script/Engine." + ClassIconType)), TEXT("ClassThumbnail")),
			SNotificationItem::CS_Success,
			false,
			350.0f
		);
	}

	return Importer;
}

IImporter* IImportReader::ImportReference(const FString& File) {
	FString FilePath = File;
	if (FilePath.Contains("\\")) {
		FilePath = File.Replace(TEXT("\\"), TEXT("/"));
	}

	TArray<TSharedPtr<FJsonValue>> DataObjects; {
		DeserializeJSON(FilePath, DataObjects);
	}

	IImporter* Importer = nullptr;
	ReadExportsAndImport(DataObjects, FilePath, Importer);

	return Importer;
}
