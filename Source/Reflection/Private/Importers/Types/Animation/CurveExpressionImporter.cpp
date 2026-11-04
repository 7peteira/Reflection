#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Animation/CurveExpressionImporter.h"
#include "Importers/Types/Blueprint/BlueprintCompile.h"
#include "Importers/Types/Animation/CurveExpressionRig.h"
#include "Modules/Cloud/Cloud.h"
#include "Settings/SettingsAccess.h"

#if REFLECTION_CURVE_EXPRESSION
#include "CurveExpressionsDataAsset.h"
#endif

#if REFLECTION_RIGVM
#include "Engine/ControlRigCompatibility.h"
#include "ControlRig.h"

#if !REFLECTION_CONTROL_RIG
#include "ControlRigBlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif
#endif

UObject* ICurveExpressionImporter::CreateAsset(UObject* CreatedAsset) {
#if REFLECTION_RIGVM
	if (ImportsCurveMappingAsRig()) {
		if (UControlRigBlueprint* Existing = LoadObject<UControlRigBlueprint>(nullptr, *GetPackage()->GetPathName())) {
			return IImporter::CreateAsset(Existing);
		}

#if REFLECTION_CONTROL_RIG
		UFactory* Factory = RigBlueprintFactory(UControlRig::StaticClass());

		return IImporter::CreateAsset(Factory != nullptr ? Factory->FactoryCreateNew(
			UControlRigBlueprint::StaticClass(),
			GetPackage(),
			FName(*GetAssetName()),
			RF_Public | RF_Standalone,
			nullptr,
			GWarn
		) : nullptr);
#else
		UControlRigBlueprint* Made = Cast<UControlRigBlueprint>(CreateBlueprintGuarded(
			UControlRig::StaticClass(),
			GetPackage(),
			FName(*GetAssetName()),
			BPTYPE_Normal,
			UControlRigBlueprint::StaticClass(),
			UControlRigBlueprintGeneratedClass::StaticClass()
		));

		if (Made != nullptr) {
#if UE4_25_BELOW
			Made->InitializeModelIfRequired();
#else
			Made->InitializeModelIfRequired(false);
#endif
		}

		return IImporter::CreateAsset(Made);
#endif
	}
#endif

#if REFLECTION_CURVE_EXPRESSION
	return IImporter::CreateAsset(NewObject<UCurveExpressionsDataAsset>(GetPackage(), GetAssetClass(), StringToName(GetAssetName()), RF_Public | RF_Standalone));
#else
	return IImporter::CreateAsset(CreatedAsset);
#endif
}

bool ICurveExpressionImporter::ImportAsControlRig(const TArray<TSharedPtr<FJsonValue>>& Expressions) {
#if REFLECTION_RIGVM
	UControlRigBlueprint* Blueprint = Create<UControlRigBlueprint>();

	if (Blueprint == nullptr) return false;

	auto _ = Blueprint->MarkPackageDirty();

	FCurveExpressionRigStats Stats;

	if (!FCurveExpressionRig::Build(Blueprint, Expressions, GetAssetName(), Stats)) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("Nothing in the mapping could be drawn as a rig"),
			TEXT("A rig is built out of what each curve is worth to the ones it drives, and the Cloud found nothing here it could put a number on. The asset is created with an empty graph.")
		);
	}

	Blueprint->RecompileVM();

	return OnAssetCreation(Blueprint);
#else
	FImportIssues::Report(
		EImportIssue::Failed,
		TEXT("Control Rig is not in this engine"),
		TEXT("A curve mapping can only be drawn as a rig where there is a rig to draw it in. Bring it in as a data asset instead.")
	);

	return false;
#endif
}

bool ICurveExpressionImporter::Import() {
	const TArray<TSharedPtr<FJsonValue>> Expressions = FetchExpressions();

#if REFLECTION_RIGVM
	if (ImportsCurveMappingAsRig()) {
		return ImportAsControlRig(Expressions);
	}
#endif

#if REFLECTION_CURVE_EXPRESSION
	UCurveExpressionsDataAsset* Asset = Create<UCurveExpressionsDataAsset>();
	auto _ = Asset->MarkPackageDirty();

	DeserializeExports(Asset);
	GetObjectSerializer()->DeserializeObjectProperties(GetAssetData(), Asset);

	TArray<FName> Targets;

	const FString Assignments = GetAssignments(Expressions, Targets);

	if (Assignments.IsEmpty()) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("No expressions from the Cloud"),
			TEXT("The asset is created without them, and drives nothing until its expressions are written in by hand.")
		);
	} else {
		Asset->Expressions.AssignmentExpressions = Assignments;

		if (FProperty* Property = FCurveExpressionList::StaticStruct()->FindPropertyByName(
			GET_MEMBER_NAME_CHECKED(FCurveExpressionList, AssignmentExpressions))) {
			FPropertyChangedEvent PropertyChangedEvent(Property);

			Asset->PostEditChangeProperty(PropertyChangedEvent);
		}

		TArray<FName> Rejected;

#if UE5_4_BEYOND
		if (const TSharedPtr<const FExpressionData> Data = Asset->GetCompiledExpressionData()) {
			for (const FName& Target : Targets) {
				if (!Data->ExpressionMap.Contains(Target)) Rejected.Add(Target);
			}
		}
#else
		for (const FName& Target : Targets) {
			if (!Asset->GetCompiledExpressionMap().Contains(Target)) Rejected.Add(Target);
		}
#endif

		if (Rejected.Num() > 0) {
			TArray<FString> Names;

			for (const FName& Target : Rejected) Names.Add(Target.ToString());

			FImportIssues::Report(
				EImportIssue::Data,
				FString::Printf(TEXT("%d of %d expressions were turned down"), Rejected.Num(), Targets.Num()),
				FString::Printf(TEXT("The engine's expression parser would not take them, so those curves are driven by nothing: %s"), *FString::Join(Names, TEXT(", ")))
			);

			UE_LOG(LogReflection, Warning, TEXT("\"%s\" had %d of %d expression(s) turned down by the parser"), *GetAssetName(), Rejected.Num(), Targets.Num());
		}
	}

	return OnAssetCreation(Asset);
#else
	FImportIssues::Report(
		EImportIssue::Failed,
		TEXT("CurveExpression is not in this engine"),
		TEXT("The plugin that compiles curve expressions ships with Unreal Engine 5. Nothing here can read the asset without it.")
	);

	return false;
#endif
}

TArray<TSharedPtr<FJsonValue>> ICurveExpressionImporter::FetchExpressions() {
	FString FetchPath = GetAssetExport()->HasField(TEXT("Package"))
		? GetAssetExport()->GetStringField(TEXT("Package"))
		: FString();

	if (FetchPath.IsEmpty()) {
		FetchPath = GetPackage()->GetPathName();

		FRRedirects::Reverse(FetchPath);
	}

	const FBlockingRequestScope BlockingScope(FText::Format(
		NSLOCTEXT("Reflection", "FetchingExpressions", "Reading expressions from {0}"),
		FText::FromString(FetchPath)
	));

	const TSharedPtr<FJsonObject> Payload = Cloud::Export::GetCurveExpressionsBlocking(FetchPath);

	const TArray<TSharedPtr<FJsonValue>>* Expressions = nullptr;

	if (!Payload.IsValid() || !Payload->TryGetArrayField(TEXT("expressions"), Expressions)) {
		return {};
	}

	return *Expressions;
}

FString ICurveExpressionImporter::GetAssignments(const TArray<TSharedPtr<FJsonValue>>& Expressions, TArray<FName>& OutTargets) {
	TArray<FString> Lines;
	Lines.Reserve(Expressions.Num());

	for (const TSharedPtr<FJsonValue>& Value : Expressions) {
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Entry.IsValid()) continue;

		FString Target, Expression;

		if (!Entry->TryGetStringField(TEXT("target"), Target) || Target.IsEmpty()) continue;
		if (!Entry->TryGetStringField(TEXT("expression"), Expression) || Expression.IsEmpty()) continue;

		Lines.Add(FString::Printf(TEXT("%s = %s"), *Target, *Expression));
		OutTargets.Add(FName(*Target));
	}

	return FString::Join(Lines, TEXT("\n"));
}
