#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Toolbar/Tools/ImportFromPath.h"

#include "Containers/Export.h"
#include "Importers/Constructor/ImportIssues.h"
#include "Importers/Constructor/ImportReader.h"
#include "Importers/Types/Texture/TextureImporter.h"
#include "Importers/Types/World/LevelRead.h"
#include "Importers/Types/World/LevelRead.h"
#include "Importers/Types/Texture/TextureTypes.h"
#include "Modules/Cloud/Cloud.h"
#include "Modules/Cloud/Remote.h"
#include "Modules/Toolbar/Tools/ImportFolder.h"
#include "Modules/UI/Reflect/SReflectPathsDialog.h"
#include "Engine/EngineUtilities.h"

void TToolImportFromPath::Execute() {
	TArray<FString> Paths;

	for (;;) {
		FString Folder;

		const EReflectPathsChoice Choice = SReflectPathsDialog::Open(Paths, Folder);

		if (Choice == EReflectPathsChoice::Folder) {
			TToolImportFolder Tool;

			if (Tool.Execute(Folder,   true,   true)) {
				continue;
			}

			return;
		}

		if (Choice != EReflectPathsChoice::Reflect) {
			return;
		}

		break;
	}

	if (!Cloud::Status::IsOpened()) {
		Cloud::Status::NotifyNotRunning();

		return;
	}

	if (!Cloud::EnsureMetadataBlocking()) {
		return;
	}

	int32 Reflected = 0;
	const int32 Attempted = Paths.Num();

	FImportIssues::Begin();

	for (const FString& Path : Paths) {
		if (Import(Path)) {
			Reflected++;
		}
	}

	const bool Successful = Reflected == Attempted;

	AppendNotification(
		FText::FromString(Successful ? "Reflected From Path" : "Reflected With Failures"),
		FText::FromString(FString::Printf(TEXT("%d of %d"), Reflected, Attempted)),
		Successful ? 2.0f : 5.0f,
		Successful ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail,
		true,
		310.0f
	);

	FImportIssues::Finish();
}

bool TToolImportFromPath::Import(const FString& InPath, const TSet<FString>& AllowedTypes, bool* bOutFiltered) {
	FString PackagePath = StripObjectOuter(InPath.TrimStartAndEnd());
	PackagePath.ReplaceInline(TEXT("\\"), TEXT("/"));

	int32 Dot;

	if (PackagePath.FindChar(TEXT('.'), Dot)) {
		LeftInline(PackagePath, Dot);
	}

	PackagePath.TrimStartAndEndInline();

	if (PackagePath.IsEmpty()) {
		return false;
	}

	FString AssetName;
	PackagePath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

	const auto ReportUnreachable = [&AssetName, &PackagePath, &InPath](const EImportIssue Kind, const TCHAR* Summary) {
		UE_LOG(LogReflection, Error, TEXT("%s: \"%s\""), Summary, *PackagePath);

		const FString Pasted = InPath.TrimStartAndEnd();

		FImportIssues::ReportFor(
			AssetName.IsEmpty() ? PackagePath : AssetName,
			PackagePath,
			FString(),
			Kind,
			Summary,
			Pasted == PackagePath ? FString() : TEXT("Reflected from ") + Pasted
		);
	};

	const FBlockingRequestScope BlockingScope(FText::Format(
		NSLOCTEXT("Reflection", "ReflectingPath", "Reflecting {0}"),
		FText::FromString(PackagePath)
	));

	const TSharedPtr<FJsonObject> Response = Cloud::Export::GetRawBlocking(PackagePath);
	if (Response == nullptr || !Response->HasField(TEXT("exports"))) {
		ReportUnreachable(EImportIssue::MissingAsset, TEXT("Cloud has nothing at this path"));

		return false;
	}

	const TArray<TSharedPtr<FJsonValue>> Exports = Response->GetArrayField(TEXT("exports"));
	if (Exports.Num() == 0) {
		ReportUnreachable(EImportIssue::MissingAsset, TEXT("Cloud has nothing at this path"));

		return false;
	}

	const TSharedPtr<FJsonObject> Export = Exports[0]->AsObject();

	FString Type;
	if (!Export.IsValid() || !Export->TryGetStringField(TEXT("Type"), Type)) {
		ReportUnreachable(EImportIssue::Data, TEXT("Cloud returned an export with no type"));

		return false;
	}

	if (AllowedTypes.Num() > 0 && !AllowedTypes.Contains(Type)) {
		if (bOutFiltered != nullptr) {
			*bOutFiltered = true;
		}

		return false;
	}

	if (FLevelRead::Handles(Exports)) {
		return FLevelRead::FromCloud(Exports, PackagePath);
	}

	if (FTextureTypes::IsSupported(Type)) {
		UTexture* Texture = nullptr;

		return FTextureImport::FromCloud(PackagePath + "." + AssetName, PackagePath, Texture);
	}

	IImporter* OutImporter = nullptr;

	return IImportReader::ReadExportsAndImport(Exports, PackagePath, OutImporter, false, false);
}
