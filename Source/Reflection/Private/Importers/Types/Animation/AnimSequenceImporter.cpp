#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Animation/AnimSequenceImporter.h"
#include "Importers/Constructor/ImportJob.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Importers/Constructor/ImportIssues.h"
#include "Modules/Cloud/Cloud.h"
#include "Modules/Cloud/Remote.h"
#include "Importers/Types/Animation/SAdditiveBaseDialog.h"
#include "Engine/EngineUtilities.h"
#include "Utilities/JsonHelpers.h"
#include "Modules/Cloud/Tools/AnimationData.h"

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 2
#include "Animation/AnimData/IAnimationDataController.h"
#endif

static const FString AnimationURL = TEXT("/api/export/animation");

static TSharedPtr<FJsonObject> GetAnimation(const FString& Path, const FString& BasePath = FString()) {
	TMap<FString, FString> Parameters = { { TEXT("path"), Path } };

	if (!BasePath.IsEmpty()) {
		Parameters.Add(TEXT("base"), BasePath);
	}

	const TSharedPtr<FJsonObject> Response = Cloud::GetBlocking(AnimationURL, Parameters, {});

	if (!Response.IsValid() || !Response->HasField(TEXT("tracks"))) {
		return nullptr;
	}

	return Response;
}

static float ReadKeyFloat(const TArray<TSharedPtr<FJsonValue>>* Values, const int32 Index) {
	return Values != nullptr && Values->IsValidIndex(Index) ? static_cast<float>((*Values)[Index]->AsNumber()) : 0.0f;
}

static FFrameRate FramesLaidOutAt(const int32 Spans, const float SequenceLength);

static FFrameRate LaidOutAt(UAnimSequence* Sequence) {
#if ENGINE_UE5
	return Sequence->GetSamplingFrameRate();
#else
	return FramesLaidOutAt(Sequence->GetNumberOfFrames() - 1, Sequence->GetPlayLength());
#endif
}

static FFrameRate FramesLaidOutAt(const int32 Spans, const float SequenceLength) {
	if (Spans <= 0 || SequenceLength <= 0.0f) return FFrameRate(30, 1);

	const double Exact = Spans / static_cast<double>(SequenceLength);
	const int32 Whole = FMath::Max(1, FMath::RoundToInt(Exact));

	if (FMath::IsNearlyEqual(Exact, static_cast<double>(Whole), 0.001)) return FFrameRate(Whole, 1);

	constexpr int64 Tick = 100000;

	int64 Top = static_cast<int64>(Spans) * Tick;
	int64 Bottom = static_cast<int64>(FMath::RoundToDouble(static_cast<double>(SequenceLength) * Tick));

	if (Bottom <= 0) return FFrameRate(Whole, 1);

	int64 Left = Top;
	int64 Right = Bottom;

	while (Right != 0) {
		const int64 Rest = Left % Right;

		Left = Right;
		Right = Rest;
	}

	Top /= Left;
	Bottom /= Left;

	if (Top > MAX_int32 || Bottom > MAX_int32) return FFrameRate(Whole, 1);

	return FFrameRate(static_cast<int32>(Top), static_cast<int32>(Bottom));
}

UObject* IAnimSequenceImporter::CreateAsset(UObject* CreatedAsset) {
	if (UAnimSequence* Existing = Cast<UAnimSequence>(StaticFindObjectFast(UAnimSequence::StaticClass(), GetPackage(), StringToName(GetAssetName())))) {
#if ENGINE_UE4
#if !UE4_24_BELOW
		Existing->WaitOnExistingCompression(false);
#endif
		Existing->RemoveAllTracks();
#endif

		Existing->Notifies.Empty();
		Existing->AuthoredSyncMarkers.Empty();

		return IImporter::CreateAsset(Existing);
	}

	return IImporter::CreateAsset(NewObject<UAnimSequence>(GetPackage(), UAnimSequence::StaticClass(), StringToName(GetAssetName()), RF_Public | RF_Standalone | RF_Transactional));
}

USkeleton* IAnimSequenceImporter::ResolveSkeleton() {
	const FUObjectJsonValueExport SkeletonReference = GetAssetDataAsValue().GetObject(TEXT("Skeleton"));

	if (!SkeletonReference.JsonObject.IsValid() || !SkeletonReference.Has(TEXT("ObjectName"))) {
		return nullptr;
	}

	TObjectPtr<USkeleton> Skeleton;
	LoadExport<USkeleton>(&SkeletonReference.JsonObject, Skeleton);

	return Skeleton.Get();
}

bool IAnimSequenceImporter::Import() {
	USkeleton* Skeleton = ResolveSkeleton();

	if (Skeleton == nullptr) {
		FImportIssues::Report(
			EImportIssue::MissingAsset,
			TEXT("The sequence's skeleton isn't in this project"),
			TEXT("An animation is keys on the bones of the skeleton it names, so the import stops here rather than building one with nothing to play on.")
		);

		return false;
	}

	FString FetchPath = GetAssetExport()->HasField(TEXT("Package"))
		? GetAssetExport()->GetStringField(TEXT("Package"))
		: FString();

	if (FetchPath.IsEmpty()) {
		FetchPath = GetPackage()->GetPathName();

		FRRedirects::Reverse(FetchPath);
	}

	const FBlockingRequestScope BlockingScope(FText::Format(
		NSLOCTEXT("Reflection", "FetchingAnimation", "Reading animation from {0}"),
		FText::FromString(FetchPath)
	));

	TSharedPtr<FJsonObject> Payload = GetAnimation(FetchPath);

	if (Payload.IsValid() && Payload->HasField(TEXT("needsBase")) && Payload->GetBoolField(TEXT("needsBase"))) {
		FString BasePath;

		const bool bAsk = !FImportJob::IsRunning() && !IsRunningCommandlet();

		if (!bAsk) {
			UE_LOG(LogReflection, Display, TEXT("\"%s\" is a difference with nothing to put it over, and a run of assets is no place to ask what"), *GetAssetName());
		}

		if (bAsk && SAdditiveBaseDialog::Open(GetAssetName(), BasePath) && !BasePath.IsEmpty()) {
			if (const TSharedPtr<FJsonObject> Rebuilt = GetAnimation(FetchPath, BasePath)) {
				Payload = Rebuilt;

				UE_LOG(LogReflection, Display, TEXT("\"%s\" was built over \"%s\""), *GetAssetName(), *BasePath);
			} else {
				FImportIssues::Report(
					EImportIssue::Data,
					TEXT("The animation it was built over didn't come back"),
					FString::Printf(TEXT("\"%s\" is not something the Cloud could read, so the difference is built over the skeleton's own pose instead."), *BasePath)
				);
			}
		}
	}

	if (!Payload.IsValid()) {
		FImportIssues::Report(
			EImportIssue::Failed,
			TEXT("No animation from the Cloud"),
			TEXT("The Cloud has to be running, and the sequence's keys have to be in a format it can read back.")
		);

		return false;
	}

	UAnimSequence* AnimSequence = Create<UAnimSequence>();
	if (AnimSequence == nullptr) return false;

	AnimSequence->SetSkeleton(Skeleton);

	DeserializeExports(AnimSequence);

	GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(GetAssetData(), {
		"NumFrames",
		"SequenceLength",
		"RawAnimationData",
		"TrackToSkeletonMapTable",

		"CompressedDataStructure",
		"CompressedTrackToSkeletonMapTable",
		"CompressedRawDataSize",
		"CompressedCurveData",
		"CompressedCurveNames",
		"CompressedByteStream",
		"BoneCompressionSettings",
		"CurveCompressionSettings",

		"Skeleton",
		"SkeletonGuid",
	}), AnimSequence);

	const TArray<FAnimSyncMarker> AuthoredMarkers = AnimSequence->AuthoredSyncMarkers;
	const TArray<FAnimNotifyEvent> AuthoredNotifies = AnimSequence->Notifies;

	const int32 WrittenTracks = BuildTracks(AnimSequence, Skeleton, Payload);

	if (WrittenTracks == 0) {
		FImportIssues::Report(
			EImportIssue::Failed,
			TEXT("None of the animation's bones are on this skeleton"),
			TEXT("Every track the Cloud sent names a bone the skeleton hasn't got, so there is nothing for the sequence to move.")
		);

		return false;
	}

	UE_LOG(LogReflection, Display, TEXT("\"%s\" built %d track(s) over %d frame(s), %.4fs at %s, against skeleton \"%s\""),
		*GetAssetName(), WrittenTracks, static_cast<int32>(Payload->GetIntegerField(TEXT("numFrames"))),
		AnimSequence->GetPlayLength(), *LaidOutAt(AnimSequence).ToPrettyText().ToString(), *Skeleton->GetName());

	AnimSequence->AuthoredSyncMarkers = AuthoredMarkers;
	AnimSequence->Notifies = AuthoredNotifies;

	AnimSequence->SortSyncMarkers();

	BuildAnimNotifyTracks(AnimSequence);

	if (!ReadAnimationCurves(this, AnimSequence)) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The animation's curves weren't read"),
			TEXT("The sequence imports without them, so anything driven by a curve stays still.")
		);
	}

	AnimSequence->PostEditChange();

	return OnAssetCreation(AnimSequence);
}

int32 IAnimSequenceImporter::BuildTracks(UAnimSequence* AnimSequence, USkeleton* Skeleton, const TSharedPtr<FJsonObject>& Payload) const {
	const TArray<TSharedPtr<FJsonValue>>* TracksJson = nullptr;

	if (!Payload->TryGetArrayField(TEXT("tracks"), TracksJson)) return 0;

	const int32 NumFrames = FMath::Max(1, Payload->GetIntegerField(TEXT("numFrames")));

	const float SequenceLength = Payload->HasField(TEXT("sequenceLength"))
		? static_cast<float>(Payload->GetNumberField(TEXT("sequenceLength")))
		: 0.0f;

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();

	TArray<FName> BoneNames;
	TArray<TArray<FVector>> PositionKeys;
	TArray<TArray<FQuat>> RotationKeys;
	TArray<TArray<FVector>> ScaleKeys;

	for (const TSharedPtr<FJsonValue>& TrackValue : *TracksJson) {
		const TSharedPtr<FJsonObject> TrackJson = TrackValue.IsValid() ? TrackValue->AsObject() : nullptr;
		if (!TrackJson.IsValid()) continue;

		FString BoneName;
		if (!TrackJson->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty()) continue;

		const int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*BoneName));
		if (BoneIndex == INDEX_NONE) continue;

		const FTransform RefPose = RefSkeleton.GetRefBonePose()[BoneIndex];

		const TArray<TSharedPtr<FJsonValue>>* Positions = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Rotations = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Scales = nullptr;

		TrackJson->TryGetArrayField(TEXT("positions"), Positions);
		TrackJson->TryGetArrayField(TEXT("rotations"), Rotations);
		TrackJson->TryGetArrayField(TEXT("scales"), Scales);

		const int32 PositionCount = Positions != nullptr ? Positions->Num() / 3 : 0;
		const int32 RotationCount = Rotations != nullptr ? Rotations->Num() / 4 : 0;
		const int32 ScaleCount = Scales != nullptr ? Scales->Num() / 3 : 0;

		TArray<FVector> TrackPositions;
		TArray<FQuat> TrackRotations;
		TArray<FVector> TrackScales;

		if (PositionCount == 0) {
			TrackPositions.Add(RefPose.GetTranslation());
		} else {
			TrackPositions.Reserve(PositionCount);

			for (int32 Key = 0; Key < PositionCount; ++Key) {
				TrackPositions.Add(FVector(
					ReadKeyFloat(Positions, Key * 3),
					ReadKeyFloat(Positions, Key * 3 + 1),
					ReadKeyFloat(Positions, Key * 3 + 2)
				));
			}
		}

		if (RotationCount == 0) {
			TrackRotations.Add(RefPose.GetRotation());
		} else {
			TrackRotations.Reserve(RotationCount);

			for (int32 Key = 0; Key < RotationCount; ++Key) {
				FQuat Rotation(
					ReadKeyFloat(Rotations, Key * 4),
					ReadKeyFloat(Rotations, Key * 4 + 1),
					ReadKeyFloat(Rotations, Key * 4 + 2),
					ReadKeyFloat(Rotations, Key * 4 + 3)
				);

				Rotation.Normalize();

				TrackRotations.Add(Rotation);
			}
		}

		if (ScaleCount == 0) {
			TrackScales.Add(RefPose.GetScale3D());
		} else {
			TrackScales.Reserve(ScaleCount);

			for (int32 Key = 0; Key < ScaleCount; ++Key) {
				TrackScales.Add(FVector(
					ReadKeyFloat(Scales, Key * 3),
					ReadKeyFloat(Scales, Key * 3 + 1),
					ReadKeyFloat(Scales, Key * 3 + 2)
				));
			}
		}

		const int32 TrackKeys = FMath::Max3(TrackPositions.Num(), TrackRotations.Num(), TrackScales.Num());

		auto HoldLastKey = [](auto& Keys, const int32 Count) {
			if (Keys.Num() >= Count) return;

			const auto Held = Keys.Last();

			Keys.Reserve(Count);

			while (Keys.Num() < Count) {
				Keys.Add(Held);
			}
		};

		HoldLastKey(TrackPositions, TrackKeys);
		HoldLastKey(TrackRotations, TrackKeys);
		HoldLastKey(TrackScales, TrackKeys);

		BoneNames.Add(FName(*BoneName));
		PositionKeys.Add(MoveTemp(TrackPositions));
		RotationKeys.Add(MoveTemp(TrackRotations));
		ScaleKeys.Add(MoveTemp(TrackScales));
	}

	if (BoneNames.Num() == 0) return 0;

	const int32 FrameSpans = FMath::Max(1, NumFrames - 1);

	const FFrameRate FrameRate = FramesLaidOutAt(NumFrames > 1 ? FrameSpans : 0, SequenceLength);

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 2
	IAnimationDataController& Controller = AnimSequence->GetController();

	Controller.OpenBracket(NSLOCTEXT("Reflection", "BuildAnimationSequence", "Building animation sequence"), false);
	Controller.InitializeModel();

	Controller.SetFrameRate(FrameRate, false);
	Controller.SetNumberOfFrames(FFrameNumber(FrameSpans), false);

	for (int32 Track = 0; Track < BoneNames.Num(); ++Track) {
		Controller.AddBoneCurve(BoneNames[Track], false);
		Controller.SetBoneTrackKeys(BoneNames[Track], PositionKeys[Track], RotationKeys[Track], ScaleKeys[Track], false);
	}

	Controller.NotifyPopulated();
	Controller.CloseBracket(false);
#elif ENGINE_UE4
	AnimSequence->SetRawNumberOfFrame(NumFrames);
	AnimSequence->SequenceLength = SequenceLength > 0.0f
		? SequenceLength
		: (NumFrames > 1 ? (NumFrames - 1) / 30.0f : 1.0f);

	for (int32 Track = 0; Track < BoneNames.Num(); ++Track) {
		FRawAnimSequenceTrack RawTrack;

		RawTrack.PosKeys = PositionKeys[Track];
		RawTrack.RotKeys = RotationKeys[Track];
		RawTrack.ScaleKeys = ScaleKeys[Track];

		AnimSequence->AddNewRawTrack(BoneNames[Track], &RawTrack);
	}

	AnimSequence->PostProcessSequence();
#else
	IAnimationDataController& Controller = AnimSequence->GetController();

	Controller.OpenBracket(NSLOCTEXT("Reflection", "BuildAnimationSequence", "Building animation sequence"), false);
	Controller.ResetModel(false);

	Controller.SetFrameRate(FrameRate, false);
	Controller.SetPlayLength(static_cast<float>(FrameRate.AsSeconds(FFrameNumber(FrameSpans))), false);

	for (int32 Track = 0; Track < BoneNames.Num(); ++Track) {
		Controller.AddBoneTrack(BoneNames[Track], false);
		Controller.SetBoneTrackKeys(BoneNames[Track], PositionKeys[Track], RotationKeys[Track], ScaleKeys[Track], false);
	}

	Controller.NotifyPopulated();
	Controller.CloseBracket(false);

#endif

	return BoneNames.Num();
}
