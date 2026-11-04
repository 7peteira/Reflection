#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Constructor/Asset.h"

#include "Animation/Skeleton.h"

#include "Importers/Constructor/Importer.h"

#include "Importers/Types/Texture/TextureImporter.h"
#include "Importers/Types/Texture/TextureTypes.h"

#include "Curves/CurveLinearColor.h"
#include "Engine/TextureLightProfile.h"
#include "Sound/SoundNode.h"
#include "Engine/SubsurfaceProfile.h"
#include "Materials/MaterialParameterCollection.h"
#include "Settings/ReflectionSettings.h"
#include "Settings/SettingsAccess.h"
#include "Dom/JsonObject.h"

#include "Engine/FontFace.h"
#include "Importers/Constructor/ImportIssues.h"
#include "Importers/Constructor/ImportReader.h"
#include "Modules/Cloud/Cloud.h"
#include "Settings/Runtime.h"

UPackage* FAssetUtilities::CreateAssetPackage(const FString& Path) {
	if (!FPackageName::IsValidLongPackageName(Path)) {
		FImportIssues::Report(
			EImportIssue::Failed,
			TEXT("Not a package path"),
			FString::Printf(TEXT("\"%s\" is not one. Reflect from inside the export folder so the path resolves against the game's own."), *Path)
		);

		return nullptr;
	}

	UPackage* Package = CreatePackage(
#if UE4_25_BELOW || (UE4_26_0)
		nullptr,
#endif
		*Path);
	Package->FullyLoad();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AssetRegistry.AddPath(FPackageName::GetLongPackagePath(Path));

	return Package;
}

UPackage* FAssetUtilities::CreateAssetPackage(const FString& Name, const FString& OutputPath, FString& FailureReason) {
	const FString& ProjectName = GReflectionRuntime.Profile.ProjectName;

	FString ModifiablePath = OutputPath;

	if (!ModifiablePath.StartsWith("/Game/") && !ModifiablePath.StartsWith("/Plugins/") && ModifiablePath.Contains("/Content/")) {
		if (!ProjectName.IsEmpty()) {
			ModifiablePath = ModifiablePath.Replace(*(ProjectName + "/Content"), TEXT("/Game"));
			ModifiablePath.Split(*(GReflectionRuntime.ExportDirectory.Path + "/"), nullptr, &ModifiablePath, ESearchCase::IgnoreCase, ESearchDir::FromStart);
			ModifiablePath.Split("/", &ModifiablePath, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			ModifiablePath += "/";
		}

		if (!ModifiablePath.StartsWith("/Game/") && !ModifiablePath.StartsWith("/Plugins/") && ModifiablePath.Contains("/Content/")) {
			ModifiablePath.Split(*(GReflectionRuntime.ExportDirectory.Path + "/"), nullptr, &ModifiablePath, ESearchCase::IgnoreCase, ESearchDir::FromStart);
			ModifiablePath.Split("/", nullptr, &ModifiablePath, ESearchCase::IgnoreCase, ESearchDir::FromStart);
			ModifiablePath.Split("/", &ModifiablePath, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			const bool IsPlugin = ModifiablePath.StartsWith("Plugins");

			if (IsPlugin) {
				FString PluginName = ModifiablePath;
				FString RemainingPath;
				ModifiablePath.Split("/Content/", &PluginName, &RemainingPath, ESearchCase::IgnoreCase, ESearchDir::FromStart);
				PluginName.Split("/", nullptr, &PluginName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

				ModifiablePath = PluginName + "/" + RemainingPath;
			}
			else {
				ModifiablePath = ModifiablePath.Replace(TEXT("Content"), TEXT("Game"));
			}

			ModifiablePath = "/" + ModifiablePath + "/";

			FRRedirects::Redirect(ModifiablePath);

			if (IsPlugin && !ModifiablePath.StartsWith("/Game/")) {
				FString PluginName;
				ModifiablePath.Split("/", nullptr, &PluginName, ESearchCase::IgnoreCase, ESearchDir::FromStart);
				PluginName.Split("/", &PluginName, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromStart);

				if (GetPlugin(PluginName) == nullptr) {
					CreatePlugin(PluginName);
				}
			}
		}
		else {
			FRRedirects::Redirect(ModifiablePath);

			if (!ModifiablePath.StartsWith("/Game/") && !ModifiablePath.StartsWith("/Engine/")) {
				FString PluginName;
				ModifiablePath.Split("/", nullptr, &PluginName, ESearchCase::IgnoreCase, ESearchDir::FromStart);
				PluginName.Split("/", &PluginName, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromStart);

				if (GetPlugin(PluginName) == nullptr) {
					CreatePlugin(PluginName);
				}
			}
		}
	} else {
		FString RootName; {
			ModifiablePath.Split("/", nullptr, &RootName, ESearchCase::IgnoreCase, ESearchDir::FromStart);
			RootName.Split("/", &RootName, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		}

		if (RootName != "Game" && RootName != "Engine" && GetPlugin(RootName) == nullptr) {
			CreatePlugin(RootName);
		}

		ModifiablePath.Split("/", &ModifiablePath, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

		ModifiablePath = ModifiablePath + "/";

		FRRedirects::Redirect(ModifiablePath);
	}

	const FString PathWithGame = ModifiablePath + Name;

	if (PathWithGame.Contains(TEXT("//"), ESearchCase::CaseSensitive) || PathWithGame == "None" || PathWithGame.IsEmpty()) {
		FailureReason = "Attempted to create a package with name containing double slashes.\n\nUpdate your configuration to use a valid Export Directory.";
		return nullptr;
	}

	UPackage* Package = CreateAssetPackage(*PathWithGame);

	if (Package == nullptr) {
		FailureReason = "\"" + PathWithGame + "\" is not a package path.\n\nReflect from inside the export folder so the path resolves against the game's own.";

		return nullptr;
	}

	Package->FullyLoad();

	return Package;
}

UPackage* FAssetUtilities::CreateAssetPackage(const FString& Name, const FString& OutputPath) {
	FString StringIgnore = "";

	return CreateAssetPackage(Name, OutputPath, StringIgnore);
}

template bool FAssetUtilities::ConstructAsset<UMaterialInterface>(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<UMaterialInterface>& OutObject, bool& bSuccess);
template bool FAssetUtilities::ConstructAsset<USubsurfaceProfile>(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<USubsurfaceProfile>& OutObject, bool& bSuccess);
template bool FAssetUtilities::ConstructAsset<UTexture>(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<UTexture>& OutObject, bool& bSuccess);
template bool FAssetUtilities::ConstructAsset<UAnimSequence>(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<UAnimSequence>& OutObject, bool& bSuccess);
template bool FAssetUtilities::ConstructAsset<UMaterialParameterCollection>(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<UMaterialParameterCollection>& OutObject, bool& bSuccess);
template bool FAssetUtilities::ConstructAsset<USoundWave>(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<USoundWave>& OutObject, bool& bSuccess);
template bool FAssetUtilities::ConstructAsset<UObject>(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<UObject>& OutObject, bool& bSuccess);
template bool FAssetUtilities::ConstructAsset<UMaterialFunctionInterface>(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<UMaterialFunctionInterface>& OutObject, bool& bSuccess);
template bool FAssetUtilities::ConstructAsset<USoundNode>(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<USoundNode>& OutObject, bool& bSuccess);
template bool FAssetUtilities::ConstructAsset<UCurveLinearColor>(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<UCurveLinearColor>& OutObject, bool& bSuccess);
template bool FAssetUtilities::ConstructAsset<UTextureLightProfile>(const FString&, const FString&, const FString&, TObjectPtr<UTextureLightProfile>&, bool&);
template bool FAssetUtilities::ConstructAsset<UFontFace>(const FString&, const FString&, const FString&, TObjectPtr<UFontFace>&, bool&);

template bool FAssetUtilities::ConstructAsset<USkeleton>(const FString&, const FString&, const FString&, TObjectPtr<USkeleton>&, bool&);

namespace {
	TArray<FString> GAssetsUnderConstruction;

	FString FileOf(const FString& Path) {
		FString Named;

		return Path.Split(TEXT("."), &Named, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromStart) ? Named : Path;
	}

	struct FConstructionScope {
		explicit FConstructionScope(const FString& InPath)
			: Path(FileOf(InPath))
			, bOwned(!GAssetsUnderConstruction.Contains(Path))
		{
			if (bOwned) {
				GAssetsUnderConstruction.Add(Path);
			}
		}

		~FConstructionScope() {
			if (bOwned) {
				GAssetsUnderConstruction.Remove(Path);
			}
		}

		FString Path;
		bool bOwned;
	};

	template <typename T>
	TObjectPtr<T> FindByReference(const FString& Path) {
		FString Redirected = Path;
		FRRedirects::Redirect(Redirected);

		if (TObjectPtr<T> Found = LoadObjectByPath<T>(Redirected)) {
			return Found;
		}

		FString Folder, File, Asset;

		if (!Redirected.Split(TEXT("."), &Folder, &Asset, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) return nullptr;
		if (!Folder.Split(TEXT("/"), &Folder, &File, ESearchCase::IgnoreCase, ESearchDir::FromEnd)) return nullptr;

		if (File == Asset) return nullptr;

		if (Asset.Contains(TEXT(":"))) return nullptr;

		return LoadObjectByPath<T>(Folder + TEXT("/") + Asset + TEXT(".") + Asset);
	}
}

template <typename T>
bool FAssetUtilities::ConstructAsset(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<T>& OutObject, bool& bSuccess) {
	if (Type.IsEmpty()) {
		return false;
	}

	if ((!bImportReferences || !GetSettings()->AssetSettings.ImportReferences) && Type != TEXT("Skeleton")) {
		return false;
	}

	const bool IsTexture = FTextureTypes::IsSupported(Type);

	const UClass* Meant = FindClassByType(Type);
	const bool bUnknowable = Meant != nullptr && (Meant->HasAnyClassFlags(CLASS_Abstract) || Meant == UClass::StaticClass());

	const FConstructionScope ConstructionScope(Path);

	if (!ConstructionScope.bOwned && (CanImport(Type) || bUnknowable)) {
		OutObject = FindByReference<T>(RealPath);
		bSuccess = OutObject != nullptr;

		return true;
	}

	if (!Cloud::Status::IsOpened()) {
		return false;
	}

	const FBlockingRequestScope BlockingScope(FText::Format(
		NSLOCTEXT("Reflection", "CloudReflecting", "Reflecting {0}"),
		FText::FromString(Path)
	));

	FString GamePath = Path;

	if (CanImport(Type, true) || IsTexture || bUnknowable) {
		if (IsTexture) {
			UTexture* Texture = nullptr;

			bSuccess = FTextureImport::FromCloud(RealPath, Path, Texture);
			if (bSuccess) OutObject = Cast<T>(Texture);

			return true;
		}

		const TSharedPtr<FJsonObject> Response = Cloud::Export::GetRawBlocking(Path);

		if (Response == nullptr || Path.IsEmpty()) return true;

		if (Response->HasField(TEXT("errored"))) {
			UE_LOG(LogReflection, Log, TEXT("Error from response \"%s\""), *Path);
			return true;
		}

		const TSharedPtr<FJsonObject> JsonObject = Response->GetArrayField(TEXT("exports"))[0]->AsObject();
		FString PackagePath;
		FString AssetName;
		RealPath.Split(".", &PackagePath, &AssetName);

		if (JsonObject) {
			const FString NewPath = PackagePath;

			FString RootName; {
				NewPath.Split("/", nullptr, &RootName, ESearchCase::IgnoreCase, ESearchDir::FromStart);
				RootName.Split("/", &RootName, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromStart);
			}

			if (RootName != "Game" && RootName != "Engine" && GetPlugin(RootName) == nullptr) {
				CreatePlugin(RootName);
			}

			IImporter* OutImporter;
			bSuccess = IImportReader::ReadExportsAndImport(Response->GetArrayField(TEXT("exports")), PackagePath, OutImporter, true);

			OutObject = FindByReference<T>(RealPath);

			return OutObject != nullptr;
		}
	}

	return false;
}

bool FAssetUtilities::Fast_Construct_TypeTexture(const TSharedPtr<FJsonObject>& JsonExport, const FString& Path, const FString& Type, TArray<uint8> Data, UTexture*& OutTexture) {
	return FTextureImport::FromExport(JsonExport, Path, Type, Data, OutTexture);
}
