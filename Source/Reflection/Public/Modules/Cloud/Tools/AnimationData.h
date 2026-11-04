#pragma once

#include "Importers/Types/Animation/LegacyCurves.h"
#include "Settings/SettingsAccess.h"
#include "CoreMinimal.h"
#include "Animation/AnimMontage.h"
#include "Importers/Constructor/Importer.h"
#include "Modules/Tools/SelectedAssetsBase.h"
#include "Utilities/JsonHelpers.h"
#include "Engine/Package.h"
#include "Importers/Constructor/ImportIssues.h"
#include "Modules/Cloud/Cloud.h"
#include "Modules/Cloud/Remote.h"
#include "Utilities/AssetPaths.h"

inline bool ReadAnimationCurves(USerializerContainer* Container, UAnimSequenceBase* AnimSequenceBase) {
	if (AnimSequenceBase == nullptr) return false;

	USkeleton* Skeleton = AnimSequenceBase->GetSkeleton();

	if (!Skeleton) {
		UE_LOG(LogReflection, Error, TEXT("Could not get valid Skeleton"));
		return false;
	}

#if UE5_2_BEYOND
	IAnimationDataController& Controller = AnimSequenceBase->GetController();
#if ENGINE_MINOR_VERSION >= 3
	IAnimationDataModel* DataModel = AnimSequenceBase->GetDataModel();
#endif
#endif

#if UE5_2_BEYOND
	Controller.RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Float, false);
#endif

	const TSharedPtr<FJsonObject>* RawCurveData;

	TArray<TSharedPtr<FJsonValue>> FloatCurves;
	TArray<TSharedPtr<FJsonValue>> Notifies;

	if (Container->GetAssetData()->TryGetObjectField(TEXT("RawCurveData"), RawCurveData)) {
		FloatCurves = Container->GetAssetData()->GetObjectField(TEXT("RawCurveData"))->GetArrayField(TEXT("FloatCurves"));
	}

	if (Container->GetAssetExport()->TryGetObjectField(TEXT("CompressedCurveData"), RawCurveData)) {
		FloatCurves = Container->GetAssetExport()->GetObjectField(TEXT("CompressedCurveData"))->GetArrayField(TEXT("FloatCurves"));
	}

	if (const ERDnaCurves Curves = GetModdingAssetSettings().MetaHuman.Curves; WantsLegacyCurves(Curves)) {
		FReflectionLegacyCurves::Rewrite(FloatCurves, Container->GetAssetName(), KeepsRigControls(Curves));
	}

	int32 UnnamedCurves = 0;

	for (const auto& FloatCurveObject : FloatCurves) {
		FString DisplayName = "";
		if (FloatCurveObject->AsObject()->HasField(TEXT("Name"))) {
			DisplayName = FloatCurveObject->AsObject()->GetObjectField(TEXT("Name"))->GetStringField(TEXT("DisplayName"));
		} else {
			DisplayName = FloatCurveObject->AsObject()->GetStringField(TEXT("CurveName"));
		}

		if (FName(*DisplayName).IsNone()) {
			++UnnamedCurves;

			continue;
		}

		int CurveTypeFlags = FloatCurveObject->AsObject()->GetIntegerField(TEXT("CurveTypeFlags"));

#if ENGINE_UE4
		FSmartName NewTrackName;

		Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, FName(*DisplayName), NewTrackName);
		ensureAlways(Skeleton->GetSmartNameByUID(USkeleton::AnimCurveMappingName, NewTrackName.UID, NewTrackName));
#endif

#if ENGINE_UE5
#if ENGINE_MINOR_VERSION <= 3
		FSmartName NewTrackName;

		Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, FName(*DisplayName), NewTrackName);

		ensureAlways(Skeleton->GetSmartNameByUID(USkeleton::AnimCurveMappingName, NewTrackName.UID, NewTrackName));
		FAnimationCurveIdentifier CurveId = FAnimationCurveIdentifier(NewTrackName, ERawCurveTrackTypes::RCT_Float);
#endif

#if ENGINE_MINOR_VERSION >= 4
		Controller.OpenBracket(FText::FromString("Curve Import"));

		FName CurveName = FName(*DisplayName);
		FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);

		Skeleton->AddCurveMetaData(CurveName);
		const FFloatCurve* ExistingCurve = DataModel->FindFloatCurve(CurveId);
		if (ExistingCurve != nullptr)
		{
			Controller.RemoveCurve(CurveId);
		}
		Controller.AddCurve(CurveId, CurveTypeFlags);
#endif
#if ENGINE_MINOR_VERSION == 3 && ENGINE_PATCH_VERSION < 2
		Controller->AddCurve(CurveId, CurveTypeFlags);
#elif (ENGINE_MINOR_VERSION == 3 && ENGINE_PATCH_VERSION == 2)
		Controller.AddCurve(CurveId, CurveTypeFlags);
#endif
#if ENGINE_MINOR_VERSION < 3
		AnimSequenceBase->Modify(true);

		IAnimationDataController& LocalOneController = AnimSequenceBase->GetController();
		LocalOneController.AddCurve(FAnimationCurveIdentifier(NewTrackName, ERawCurveTrackTypes::RCT_Float), CurveTypeFlags);

		TArray<FLinearColor> RandomizedColorArray = {
			FLinearColor(.904, .323, .539),
			FLinearColor(.552, .737, .328),
			FLinearColor(.947, .418, .219),
			FLinearColor(.156, .624, .921),
			FLinearColor(.921, .314, .337),
			FLinearColor(.361, .651, .332),
			FLinearColor(.982, .565, .254),
			FLinearColor(.246, .223, .514),
			FLinearColor(.208, .386, .687),
			FLinearColor(.223, .590, .337),
			FLinearColor(.230, .291, .591)
		}; { 
			auto Index = rand() % RandomizedColorArray.Num();

			if (RandomizedColorArray.IsValidIndex(Index)) {
				LocalOneController.SetCurveColor(FAnimationCurveIdentifier(NewTrackName, ERawCurveTrackTypes::RCT_Float), RandomizedColorArray[Index]);
			}
		}

		AnimSequenceBase->PostEditChange();
#endif
#endif

		TArray<TSharedPtr<FJsonValue>> Keys = FloatCurveObject->AsObject()->GetObjectField(TEXT("FloatCurve"))->GetArrayField(TEXT("Keys"));

		for (const auto& JsonKey : Keys) {
			TSharedPtr<FJsonObject> Key = JsonKey->AsObject();

			FRichCurveKey RichKey = ObjectToRichCurveKey(Key);

#if UE5_2_BEYOND
			Controller.SetCurveKey(CurveId, RichKey);
#endif
#if ENGINE_UE4
			FRawCurveTracks& Tracks = AnimSequenceBase->RawCurveData;
#endif
#if ENGINE_UE4
			Tracks.AddFloatCurveKey(NewTrackName, CurveTypeFlags, RichKey.Time, RichKey.Value);

			for (FFloatCurve& Track : Tracks.FloatCurves) {
				if (Track.Name == NewTrackName) {
					const int32 LastIndex = Track.FloatCurve.Keys.Num() - 1;
					Track.FloatCurve.Keys[LastIndex].ArriveTangent = RichKey.ArriveTangent;
					Track.FloatCurve.Keys[LastIndex].LeaveTangent = RichKey.LeaveTangent;
					Track.FloatCurve.Keys[LastIndex].InterpMode = RichKey.InterpMode;
				}
			}
#endif
#if UE5_1_BELOW
			AnimSequenceBase->GetController().SetCurveKey(
				FAnimationCurveIdentifier(NewTrackName, ERawCurveTrackTypes::RCT_Float), RichKey, false);
#endif
		}
#if UE5_2_BEYOND
		Controller.CloseBracket();
#endif
	}

	if (UnnamedCurves > 0) {
		UE_LOG(LogReflection, Warning, TEXT("\"%s\" left %d curve(s) out, the export naming none of them"),
			*AnimSequenceBase->GetName(), UnnamedCurves);
	}

	return true;
}

inline bool ReadAnimationData(USerializerContainer* Container, const bool UseSelectedAsset, const IImporter* Importer = nullptr) {
	UAnimSequenceBase* AnimSequenceBase = nullptr;

	if (UseSelectedAsset) {
		if (Container->GetAsset()) {
			AnimSequenceBase = Cast<UAnimSequenceBase>(Container->GetAsset());
		}

		if (!AnimSequenceBase && Container->GetPackage() != nullptr) {
			AnimSequenceBase = LoadObjectByPath<UAnimSequenceBase>(Container->GetPackage()->GetName() + TEXT(".") + Container->GetAssetName());
		}

		if (!AnimSequenceBase) {
			AnimSequenceBase = GetSelectedAsset<UAnimSequenceBase>(true, Container->GetAssetName());
		}
	} else {
		if (Container->GetAsset()) {
			AnimSequenceBase = Cast<UAnimSequenceBase>(Container->GetAsset());
		}
	}

	if (!AnimSequenceBase && Container->GetAssetClass()->IsChildOf<UAnimMontage>()) {
		AnimSequenceBase = NewObject<UAnimMontage>(Container->GetPackage(), Container->GetAssetClass(), *Container->GetAssetName(), RF_Public | RF_Standalone);
	}

	if (UseSelectedAsset && !AnimSequenceBase) {
		FImportIssues::Report(
			EImportIssue::MissingAsset,
			TEXT("Nothing to reflect the animation onto"),
			FString::Printf(TEXT("An animation's own data is not in the export, so \"%s\" has to be in the project already. Import it first, or select one to reflect onto."), *Container->GetAssetName())
		);

		return false;
	}

	if (!AnimSequenceBase) return false;

	AnimSequenceBase->Notifies.Empty();

	if (UAnimSequence* CastedAnimSequence = Cast<UAnimSequence>(AnimSequenceBase)) {
		CastedAnimSequence->AuthoredSyncMarkers.Empty();
	}

	Container->DeserializeExports(AnimSequenceBase);

	if (Cast<UAnimMontage>(AnimSequenceBase) == nullptr) {
		if (const auto& Data = Container->GetAssetData(); Data->HasField(TEXT("SequenceLength"))) {
			const float SequenceLength = Data->GetNumberField(TEXT("SequenceLength"));

			SetAnimSequenceLength(AnimSequenceBase, SequenceLength);
		}
	}

	Container->GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(Container->GetAssetData(), {
		"NumFrames",
		"TrackToSkeletonMapTable",
		"SequenceLength",
		"SkeletonGuid",
		"CompressedTrackToSkeletonMapTable",
		"CompressedDataStructure",
		"CompressedRawDataSize",
		"RawCurveData"
	}), AnimSequenceBase);

	if (UAnimSequence* CastedAnimSequence = Cast<UAnimSequence>(AnimSequenceBase)) {
		CastedAnimSequence->SortSyncMarkers();
	}

	if (UAnimMontage* Montage = Cast<UAnimMontage>(AnimSequenceBase)) {
		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks) {
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments) {
#if UE5_2_BEYOND
				UAnimSequenceBase* Plays = Segment.GetAnimReference();
#else
				UAnimSequenceBase* Plays = Segment.AnimReference;
#endif

				if (Plays == nullptr || Plays->GetPlayLength() + KINDA_SMALL_NUMBER >= Segment.AnimEndTime) continue;

				const UPackage* Holds = Plays->GetOutermost();

				if (Holds == nullptr) continue;

				const FBlockingRequestScope SettlingScope(FText::Format(
					NSLOCTEXT("Reflection", "CloudSettling", "Measuring {0}"),
					FText::FromString(Plays->GetName())
				));

				const TSharedPtr<FJsonObject> Response = Cloud::Export::GetBlocking(ToCloudPackagePath(Holds->GetName()), false);

				if (!Response.IsValid() || !Response->HasTypedField<EJson::Array>(TEXT("exports"))) continue;

				for (const TSharedPtr<FJsonValue>& Value : Response->GetArrayField(TEXT("exports"))) {
					const TSharedPtr<FJsonObject>* Export;

					if (!Value->TryGetObject(Export)) continue;

					const TSharedPtr<FJsonObject>* Properties;

					if (!(*Export)->TryGetObjectField(TEXT("Properties"), Properties)) continue;

					double Length = 0.0;

					if (!(*Properties)->TryGetNumberField(TEXT("SequenceLength"), Length) || Length <= 0.0) continue;

					SetAnimSequenceLength(Plays, static_cast<float>(Length));

					break;
				}
			}
		}

		TArray<FString> Short;

		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks) {
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments) {
#if UE5_2_BEYOND
				UAnimSequenceBase* Plays = Segment.GetAnimReference();
#else
				UAnimSequenceBase* Plays = Segment.AnimReference;
#endif

				if (Plays == nullptr) {
					Short.Add(TEXT("a segment naming nothing"));

					continue;
				}

				const float Wants = Segment.AnimEndTime;
				const float Has = Plays->GetPlayLength();

				if (Has + KINDA_SMALL_NUMBER < Wants) {
					Short.Add(FString::Printf(TEXT("%s (%.3fs of %.3fs)"), *Plays->GetName(), Has, Wants));
				}
			}
		}

		if (Short.Num() > 0) {
			FImportIssues::Report(
				EImportIssue::Data,
				FString::Printf(TEXT("%d of the montage's segment(s) play an animation shorter than the montage asks for"), Short.Num()),
				FString::Printf(TEXT("The engine trims a segment back to the animation it names, which leaves the montage shorter than the times written in it and stacks the notifies at the end. Import these at their full length first: %s."), *FString::Join(Short, TEXT(", ")))
			);
		}

		for (FAnimNotifyEvent& Notify : Montage->Notifies) {
			Notify.RefreshSegmentOnLoad();
			Notify.Link(Montage, Notify.GetTime());

			if (Notify.Duration != 0.0f) {
				Notify.EndLink.Link(Montage, Notify.GetTime() + Notify.Duration);
			}
		}

		for (FCompositeSection& Section : Montage->CompositeSections) {
			Section.RefreshSegmentOnLoad();
			Section.Link(Montage, Section.GetTime());
		}

		float Lasts = 0.0f;

		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks) {
			Lasts = FMath::Max(Lasts, Slot.AnimTrack.GetLength());
		}

		int32 Past = 0;

		for (const FAnimNotifyEvent& Notify : Montage->Notifies) {
			if (Notify.GetSegmentIndex() == INDEX_NONE || Notify.GetTime() > Lasts) Past++;
		}

		if (Past > 0) {
			FImportIssues::Report(
				EImportIssue::Data,
				FString::Printf(TEXT("%d of the montage's %d notifies sit past the end of it"), Past, Montage->Notifies.Num()),
				FString::Printf(TEXT("The montage plays for %.3fs, and these were written for one longer than that, so the editor draws them stacked at the end."), Lasts)
			);
		}
	}

	BuildAnimNotifyTracks(AnimSequenceBase);

	if (!ReadAnimationCurves(Container, AnimSequenceBase)) return false;

	UpdateAnimationCaching(AnimSequenceBase);

	AnimSequenceBase->Modify();
	AnimSequenceBase->PostEditChange();

	if (Importer) {
		return Importer->OnAssetCreation(AnimSequenceBase);
	}

	return true;
}

class REFLECTION_API TToolAnimationData : public TSelectedAssetsBase {
public:
	virtual void Process(UObject* Object, const TArray<TSharedPtr<FJsonValue>>& Exports) override;

	virtual FName GetSupportedClass() const override { return FName("AnimSequence"); }

	virtual FText GetDisplayName() const override { return FText::FromString("Animations"); }
	virtual FText GetTooltip() const override { return FText::FromString("Reflects curve data, notifies and other properties"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.Animation_24x"); }
};

REGISTER_TOOL(TToolAnimationData)
