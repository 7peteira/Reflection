#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Font/FontImporter.h"

#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/EngineUtilities.h"

#include "Modules/Cloud/Cloud.h"

void IFontImporter::Repair(UObject* Asset) const {
	UFont* Font = Cast<UFont>(Asset);

	if (Font == nullptr || Font->FontCacheType != EFontCacheType::Runtime) return;

	TArray<UFontFace*> Wanting;

	for (const FTypefaceEntry& Entry : Font->CompositeFont.DefaultTypeface.Fonts) {
		UFontFace* Face = const_cast<UFontFace*>(Cast<UFontFace>(Entry.Font.GetFontFaceAsset()));

		if (Face == nullptr || Face->GetFontFaceData()->HasData()) continue;

		Wanting.AddUnique(Face);
	}

	if (Wanting.Num() == 0) return;

	const FUObjectJsonValueExport Export = GetAssetAsValue();

	FString FetchPath = Export.Has(TEXT("Package")) ? Export.GetString(TEXT("Package")) : FString();

	if (FetchPath.IsEmpty()) {
		FetchPath = GetPackage()->GetPathName();

		FRRedirects::Reverse(FetchPath);
	}

	const FBlockingRequestScope BlockingScope(FText::Format(
		NSLOCTEXT("Reflection", "FetchingTypeface", "Reading the typeface for {0}"),
		FText::FromString(FetchPath)
	));

	const TArray<uint8> Typeface = Cloud::Export::GetFontFaceBlocking(FetchPath);

	if (Typeface.Num() == 0) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The font's faces have no typeface"),
			FString::Printf(
				TEXT("'%s' keeps %d face(s) inside itself and the typeface behind them didn't come back, so the font carries its settings and draws nothing."),
				*GetAssetName(), Wanting.Num())
		);

		return;
	}

	for (UFontFace* Face : Wanting) {
		Face->InitializeFromBulkData(Face->SourceFilename, Face->Hinting, Typeface.GetData(), Typeface.Num());

		Face->PostEditChange();
	}

	UE_LOG(LogReflection, Display, TEXT("\"%s\" filled %d face(s) it holds with %d byte(s) of typeface"),
		*GetAssetName(), Wanting.Num(), Typeface.Num());
}

void IFontImporter::Validate(UObject* Asset) const {
	const UFont* Font = Cast<UFont>(Asset);
	if (Font == nullptr) return;

	if (Font->FontCacheType != EFontCacheType::Runtime) {
		UE_LOG(LogReflection, Display, TEXT("\"%s\" is an offline font with %d texture page(s) and %d character(s)"),
			*GetAssetName(), Font->Textures.Num(), Font->Characters.Num());

		return;
	}

	const int32 Entries = Font->CompositeFont.DefaultTypeface.Fonts.Num();

	const int32 Missing = FImportIssues::ReportIncomplete(
		Font->CompositeFont.DefaultTypeface.Fonts,
		[](const FTypefaceEntry& Entry) { return Entry.Font.GetFontFaceAsset() == nullptr; },
		TEXT("typefaces didn't come through as a font face"),
		FString::Printf(TEXT("'%s' names them and hasn't got them, so those draw nothing."), *GetAssetName())
	);

	UE_LOG(LogReflection, Display, TEXT("\"%s\" is a runtime font naming %d typeface(s), %d of them resolved"),
		*GetAssetName(), Entries, Entries - Missing);
}
