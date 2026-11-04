#include "Engine/SharedPtrCompatibility.h"
#include "BlueprintImportCommandlet.h"

#include "FileHelpers.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "ScriptDisassembler.h"
#if __has_include("Misc/StringOutputDevice.h")
#include "Misc/StringOutputDevice.h"
#else
#include "Containers/UnrealString.h"
#endif
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Importers/Types/Blueprint/MacroPattern.h"
#include "Importers/Constructor/Asset.h"
#include "Importers/Constructor/ImportReader.h"
#include "Importers/Constructor/Importer.h"
#include "Modules/Cloud/Cloud.h"
#include "Settings/Runtime.h"
#if ENGINE_UE5
#include "UObject/SavePackage.h"
#endif
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatform.h"
#if UE5_1_BEYOND
#include "Serialization/ArchiveCookData.h"
#include "UObject/ArchiveCookContext.h"
#endif

DECLARE_LOG_CATEGORY_CLASS(LogBlueprintImport, All, All);

int32 UBlueprintImportCommandlet::Main(const FString& Params) {
	UE_LOG(LogBlueprintImport, Display, TEXT("asked for: %s"), *Params);

	if (FString Macro; FParse::Value(*Params, TEXT("macro="), Macro) && !Macro.IsEmpty()) {
		UEdGraph* Graph = MacroReading::StandardMacro(*Macro);

		if (Graph == nullptr) {
			UE_LOG(LogBlueprintImport, Error, TEXT("no macro called \"%s\" in StandardMacros"), *Macro);

			return 1;
		}

		UE_LOG(LogBlueprintImport, Display, TEXT("macro \"%s\", %d node(s)"), *Macro, Graph->Nodes.Num());

		for (const UEdGraphNode* Node : Graph->Nodes) {
			if (Node == nullptr) continue;

			UE_LOG(LogBlueprintImport, Display, TEXT("  %s [%s]"), *Node->GetName(), *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

			for (const UEdGraphPin* Pin : Node->Pins) {
				if (Pin == nullptr) continue;

				FString Leads;

				for (const UEdGraphPin* To : Pin->LinkedTo) {
					if (To != nullptr) Leads += FString::Printf(TEXT("%s.%s "), *To->GetOwningNode()->GetName(), *To->PinName.ToString());
				}

				UE_LOG(LogBlueprintImport, Display, TEXT("      %s %s (%s) -> %s"),
					Pin->Direction == EGPD_Input ? TEXT("in ") : TEXT("out"),
					*Pin->PinName.ToString(),
					*Pin->PinType.PinCategory.ToString(),
					Leads.IsEmpty() ? TEXT("-") : *Leads);
			}
		}

		return 0;
	}

	FString Path;

	if (!FParse::Value(*Params, TEXT("path="), Path) || Path.IsEmpty()) {
		UE_LOG(LogBlueprintImport, Error, TEXT("no asset to import: pass -path=<cloud asset path>"));

		return 1;
	}

	if (FParse::Param(*Params, TEXT("norefs"))) {
		FAssetUtilities::bImportReferences = false;

		UE_LOG(LogBlueprintImport, Display, TEXT("not reflecting referenced assets"));
	}

	if (FParse::Param(*Params, TEXT("nocode"))) {
		GetSettings()->AssetSettings.ImportBlueprintCode = false;

		UE_LOG(LogBlueprintImport, Display, TEXT("leaving the graphs empty"));
	}

	Cloud::EnsureMetadataBlocking();

	const TSharedPtr<FJsonObject> Response = Cloud::Export::GetRawBlocking(Path);

	if (!Response.IsValid() || !Response->HasField(TEXT("exports"))) {
		UE_LOG(LogBlueprintImport, Error, TEXT("the cloud had nothing at \"%s\""), *Path);

		return 1;
	}

	const TArray<TSharedPtr<FJsonValue>> Exports = Response->GetArrayField(TEXT("exports"));

	UE_LOG(LogBlueprintImport, Display, TEXT("importing \"%s\", %d export(s)"), *Path, Exports.Num());

	if (FString Dump; FParse::Value(*Params, TEXT("json="), Dump) && !Dump.IsEmpty()) {
		FString Written;

		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Written);

		if (FJsonSerializer::Serialize(Response.ToSharedRef(), Writer) && FFileHelper::SaveStringToFile(Written, *Dump)) {
			UE_LOG(LogBlueprintImport, Display, TEXT("what the cloud gave back was kept at \"%s\""), *Dump);
		} else {
			UE_LOG(LogBlueprintImport, Error, TEXT("could not keep what the cloud gave back at \"%s\""), *Dump);
		}
	}

	IImporter* Importer = nullptr;

	FString Into = Path;

	if (FString Elsewhere; FParse::Value(*Params, TEXT("into="), Elsewhere) && !Elsewhere.IsEmpty()) {
		Into = Elsewhere;

		UE_LOG(LogBlueprintImport, Display, TEXT("made at \"%s\" rather than where the cloud has it"), *Into);
	}

	const bool bImported = IImportReader::ReadExportsAndImport(Exports, Into, Importer, true, false);

	if (!bImported || Importer == nullptr) {
		UE_LOG(LogBlueprintImport, Error, TEXT("nothing was made from \"%s\""), *Path);

		return 1;
	}

	TArray<UPackage*> Dirty;
	FEditorFileUtils::GetDirtyContentPackages(Dirty);
	FEditorFileUtils::GetDirtyWorldPackages(Dirty);

	int32 Saved = 0;

	const UPackage* Imported = Importer->GetPackage();

	for (UPackage* Package : Dirty) {
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

		if (Package != Imported && FPaths::FileExists(FileName)) {
			UE_LOG(LogBlueprintImport, Display, TEXT("left alone %s, it was already on disk"), *FileName);

			continue;
		}

#if ENGINE_UE5
		Package->ClearPackageFlags(PKG_ContainsNoAsset);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

		if (UPackage::SavePackage(Package, nullptr, *FileName, SaveArgs)) {
#else
		if (UPackage::SavePackage(Package, nullptr, RF_Public | RF_Standalone, *FileName)) {
#endif
			Saved++;

			UE_LOG(LogBlueprintImport, Display, TEXT("saved %s"), *FileName);
		} else {
			UE_LOG(LogBlueprintImport, Warning, TEXT("could not save %s"), *FileName);
		}
	}

	UE_LOG(LogBlueprintImport, Display, TEXT("%d package(s) saved"), Saved);

	if (FParse::Param(*Params, TEXT("disasm"))) {
		UPackage* Package = Importer->GetPackage();

		TArray<UObject*> Inside;

		if (Package != nullptr) {
			GetObjectsWithOuter(Package, Inside, false);
		}

		for (UObject* Object : Inside) {
			UBlueprintGeneratedClass* Compiled = Cast<UBlueprintGeneratedClass>(Object);

			if (Compiled == nullptr) continue;

			for (TFieldIterator<UFunction> It(Compiled, EFieldIteratorFlags::ExcludeSuper); It; ++It) {
				if (It->Script.Num() == 0) continue;

				FStringOutputDevice Written;

				FKismetBytecodeDisassembler Disassembler(Written);
				Disassembler.DisassembleStructure(*It);

				UE_LOG(LogBlueprintImport, Display, TEXT("[disasm %s] %d byte(s)") LINE_TERMINATOR TEXT("%s"), *It->GetName(), It->Script.Num(), *Written);
			}
		}
	}

	if (FString GraphsFile; FParse::Value(*Params, TEXT("graphs="), GraphsFile) && !GraphsFile.IsEmpty()) {
		FString Written;

		UPackage* Package = Importer->GetPackage();
		UBlueprint* Blueprint = Package != nullptr ? FindObject<UBlueprint>(Package, *FPackageName::GetShortName(Package)) : nullptr;

		if (Blueprint != nullptr) {
			TArray<UEdGraph*> Graphs;
			Blueprint->GetAllGraphs(Graphs);

			for (const UEdGraph* Graph : Graphs) {
				if (Graph == nullptr) continue;

				Written += FString::Printf(TEXT("== %s (%d node(s))") LINE_TERMINATOR, *Graph->GetName(), Graph->Nodes.Num());

				for (const UEdGraphNode* Node : Graph->Nodes) {
					if (Node == nullptr) continue;

					Written += FString::Printf(TEXT("  %s [%s]") LINE_TERMINATOR, *Node->GetName(), *Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT("")));

					for (const UEdGraphPin* Pin : Node->Pins) {
						if (Pin == nullptr || Pin->bHidden) continue;

						FString Leads;

						for (const UEdGraphPin* To : Pin->LinkedTo) {
							if (To != nullptr) Leads += FString::Printf(TEXT("%s.%s "), *To->GetOwningNode()->GetName(), *To->PinName.ToString());
						}

						Written += FString::Printf(TEXT("      %s %s (%s)%s -> %s") LINE_TERMINATOR,
							Pin->Direction == EGPD_Input ? TEXT("in ") : TEXT("out"),
							*Pin->PinName.ToString(),
							*Pin->PinType.PinCategory.ToString(),
							Pin->DefaultValue.IsEmpty() && Pin->DefaultObject == nullptr ? TEXT("") : *FString::Printf(TEXT(" = %s"), Pin->DefaultObject != nullptr ? *Pin->DefaultObject->GetName() : *Pin->DefaultValue),
							Leads.IsEmpty() ? TEXT("-") : *Leads);
					}
				}
			}
		}

		if (FFileHelper::SaveStringToFile(Written, *GraphsFile)) {
			UE_LOG(LogBlueprintImport, Display, TEXT("the graphs as saved were written to \"%s\""), *GraphsFile);
		} else {
			UE_LOG(LogBlueprintImport, Error, TEXT("could not write the graphs to \"%s\""), *GraphsFile);
		}
	}

	return Saved > 0 ? 0 : 1;
}
