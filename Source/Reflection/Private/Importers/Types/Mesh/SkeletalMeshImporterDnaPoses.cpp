#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Mesh/SkeletalMeshImporter.h"

#include "Animation/AnimSequence.h"
#include "Animation/PoseAsset.h"
#include "Animation/Skeleton.h"
#include "Engine/AssetCompatibility.h"
#include "Engine/EngineUtilities.h"
#include "Engine/SkeletalMesh.h"

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 2
#include "Animation/AnimData/IAnimationDataController.h"
#endif

#include "Modules/Cloud/Cloud.h"
#include "Settings/Types/DNASettings.h"
#include "Utilities/JsonHelpers.h"

namespace {
	USkeleton* PoseMeshSkeleton(USkeletalMesh* Mesh) {
#if UE4_27_AND_UE5
		return Mesh->GetSkeleton();
#else
		return Mesh->Skeleton;
#endif
	}

	void ToAnimNodeAxes(TArray<float>& Delta) {
		for (const int32 Attribute : { 1, 3, 5 }) {
			if (Delta.IsValidIndex(Attribute)) {
				Delta[Attribute] = -Delta[Attribute];
			}
		}
	}

	FTransform ComposeDnaPoseJoint(const FTransform& Bind, const TArray<float>& Delta) {
		const auto Read = [&Delta](const int32 Index) {
			return Delta.IsValidIndex(Index) ? Delta[Index] : 0.0f;
		};

		const FVector Translation = Bind.GetTranslation() + FVector(Read(0), -Read(1), Read(2));

		const FQuat Rotation = Bind.GetRotation() * FQuat(FRotator(-Read(4), -Read(5), Read(3)));

		const FVector Scale = Bind.GetScale3D() + FVector(Read(6), Read(7), Read(8));

		return FTransform(Rotation, Translation, Scale);
	}

	TArray<float> ReadFloats(const TSharedPtr<FJsonValue>& Value) {
		TArray<float> Floats;

		const TArray<TSharedPtr<FJsonValue>>* Entries;

		if (Value.IsValid() && Value->TryGetArray(Entries)) {
			Floats.Reserve(Entries->Num());

			for (const TSharedPtr<FJsonValue>& Entry : *Entries) {
				Floats.Add(static_cast<float>(Entry->AsNumber()));
			}
		}

		return Floats;
	}
}

UPoseAsset* ISkeletalMeshImporter::BakeDnaPoseAssetFromCloud(USkeletalMesh* SkeletalMesh, const FString& FetchPath) {
	USkeleton* Skeleton = SkeletalMesh != nullptr ? PoseMeshSkeleton(SkeletalMesh) : nullptr;
	if (Skeleton == nullptr) return nullptr;

	const FRDnaSettings& DnaSettings = GetModdingAssetSettings().MetaHuman;

	const TSharedPtr<FJsonObject> Response = Cloud::Export::GetDnaPosesBlocking(FetchPath, WantsLegacyCurves(DnaSettings.Curves));

	if (!Response.IsValid()) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The head's poses didn't come back"),
			TEXT("This engine has no RigLogic, so the face has to arrive already worked out, and the Cloud returned nothing for it. The mesh imports and its face stays still.")
		);

		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* JointsJson = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* PosesJson = nullptr;

	if (
		!Response->TryGetArrayField(TEXT("joints"), JointsJson) ||
		!Response->TryGetArrayField(TEXT("poses"), PosesJson) ||
		PosesJson->Num() == 0
	) {
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* ParentsJson = nullptr;
	Response->TryGetArrayField(TEXT("parents"), ParentsJson);

	TArray<TSharedPtr<FJsonValue>> Poses = *PosesJson;

	if (KeepsRigControls(DnaSettings.Curves) && WantsLegacyCurves(DnaSettings.Curves)) {
		const TSharedPtr<FJsonObject> Controls = Cloud::Export::GetDnaPosesBlocking(FetchPath, false);

		const TArray<TSharedPtr<FJsonValue>>* ControlPoses = nullptr;

		if (Controls.IsValid() && Controls->TryGetArrayField(TEXT("poses"), ControlPoses)) {
			Poses.Append(*ControlPoses);
		} else {
			FImportIssues::Report(
				EImportIssue::Data,
				TEXT("The rig's own poses didn't come back"),
				TEXT("Both sets of names were asked for and only the older head's arrived, so the face plays for that head and anything written against this rig's controls finds nothing to drive.")
			);
		}
	}

	TArray<FString> PoseLabels;
	TArray<TMap<int32, TArray<float>>> PoseDeltas;

	TSet<int32> DrivenJoints;

	for (const TSharedPtr<FJsonValue>& Entry : Poses) {
		const TSharedPtr<FJsonObject> Pose = Entry->AsObject();
		if (!Pose.IsValid()) continue;

		TMap<int32, TArray<float>> Deltas;

		const TArray<TSharedPtr<FJsonValue>>* Moved = nullptr;

		if (Pose->TryGetArrayField(TEXT("joints"), Moved)) {
			for (const TSharedPtr<FJsonValue>& JointEntry : *Moved) {
				const TSharedPtr<FJsonObject> Joint = JointEntry->AsObject();
				if (!Joint.IsValid()) continue;

				TArray<float> Values = ReadFloats(Joint->TryGetField(TEXT("values")));

				ToAnimNodeAxes(Values);

				const int32 Index = Joint->GetIntegerField(TEXT("index"));

				DrivenJoints.Add(Index);
				Deltas.Add(Index, MoveTemp(Values));
			}
		}

		const FString Label = Pose->GetStringField(TEXT("name")).Replace(TEXT("."), TEXT("_"));

		if (PoseLabels.Contains(Label)) continue;

		PoseLabels.Add(Label);
		PoseDeltas.Add(MoveTemp(Deltas));
	}

	const FReferenceSkeleton& RefSkeleton = MeshRefSkeleton(SkeletalMesh);
	const FReferenceSkeleton& SkeletonBones = Skeleton->GetReferenceSkeleton();

	TArray<FName> BoneNames;
	TArray<int32> BoneJoints;
	TArray<FTransform> BoneBindPose;

	int32 Untouched = 0;

	for (int32 Joint = 0; Joint < JointsJson->Num(); ++Joint) {
		if (!DrivenJoints.Contains(Joint)) {
			++Untouched;

			continue;
		}

		const FName BoneName(*(*JointsJson)[Joint]->AsString());

		const int32 Bone = RefSkeleton.FindBoneIndex(BoneName);

		if (Bone == INDEX_NONE || SkeletonBones.FindBoneIndex(BoneName) == INDEX_NONE) continue;

		BoneNames.Add(BoneName);
		BoneJoints.Add(Joint);
		BoneBindPose.Add(RefSkeleton.GetRefBonePose()[Bone]);
	}

	if (WantsLegacyCurves(DnaSettings.Curves) && !Response->GetBoolField(TEXT("backported"))) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The legacy curve mapping didn't resolve"),
			TEXT("It says how this rig's controls line up with an older head's curves. Nothing in it named a control this head has, so its own controls are baked instead.")
		);
	}

	if (BoneNames.Num() == 0) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The head's joints aren't the mesh's bones"),
			TEXT("None of the joints the face drives are named in the skeleton, so a pose built out of them would move nothing.")
		);

		return nullptr;
	}

	const int32 FrameCount = PoseDeltas.Num() + 1;

#if UE5_4_BEYOND
	TArray<FName> PoseNames;
#else
	TArray<FSmartName> PoseNames;
#endif

	PoseNames.Reserve(FrameCount);

	TArray<TArray<FVector>> PositionKeys;
	TArray<TArray<FQuat>> RotationKeys;
	TArray<TArray<FVector>> ScaleKeys;

	PositionKeys.SetNum(BoneNames.Num());
	RotationKeys.SetNum(BoneNames.Num());
	ScaleKeys.SetNum(BoneNames.Num());

	const auto NamePose = [Skeleton, &PoseNames](const FString& Name) {
		const FName CurveName(*Name);

#if UE5_4_BEYOND
		Skeleton->AddCurveMetaData(CurveName);
		PoseNames.Add(CurveName);
#else
		FSmartName Smart;

		Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, CurveName, Smart);
		PoseNames.Add(Smart);
#endif
	};

	const auto WriteFrame = [&](const TMap<int32, TArray<float>>& Deltas) {
		static const TArray<float> None;

		for (int32 Bone = 0; Bone < BoneNames.Num(); ++Bone) {
			const TArray<float>* Delta = Deltas.Find(BoneJoints[Bone]);

			const FTransform Local = ComposeDnaPoseJoint(BoneBindPose[Bone], Delta != nullptr ? *Delta : None);

			PositionKeys[Bone].Add(Local.GetTranslation());
			RotationKeys[Bone].Add(Local.GetRotation());
			ScaleKeys[Bone].Add(Local.GetScale3D());
		}
	};

	WriteFrame({});
	NamePose(TEXT("base_pose"));

	for (int32 Pose = 0; Pose < PoseDeltas.Num(); ++Pose) {
		WriteFrame(PoseDeltas[Pose]);
		NamePose(PoseLabels[Pose]);
	}

	const FString Folder = FPackageName::GetLongPackagePath(GetPackage()->GetName());

	const bool bBackported = Response->GetBoolField(TEXT("backported"));

	const FString SequenceName = GetAssetName() + (bBackported
		? TEXT("_DNA_Facial_Pose_Backport")
		: TEXT("_DNA_Facial_Pose_Export"));
	const FString PoseAssetName = SequenceName + TEXT("_PoseAsset");

	UPackage* SequencePackage = CreatePackage(
#if UE4_25_BELOW || (UE4_26_0)
		nullptr,
#endif
		*(Folder / SequenceName));
	if (SequencePackage == nullptr) return nullptr;

	SequencePackage->FullyLoad();

	UAnimSequence* Sequence = NewObject<UAnimSequence>(SequencePackage, FName(*SequenceName), RF_Public | RF_Standalone);
	Sequence->SetSkeleton(Skeleton);
	Sequence->SetPreviewMesh(SkeletalMesh);

	constexpr int32 FrameRate = 30;

#if ENGINE_UE5 && ENGINE_MINOR_VERSION >= 2
	{
		IAnimationDataController& Controller = Sequence->GetController();

		Controller.OpenBracket(NSLOCTEXT("Reflection", "BuildDnaPoses", "Building poses from DNA"), false);
		Controller.InitializeModel();

		Controller.SetFrameRate(FFrameRate(FrameRate, 1), false);
		Controller.SetNumberOfFrames(FFrameNumber(FMath::Max(1, FrameCount - 1)), false);

		for (int32 Bone = 0; Bone < BoneNames.Num(); ++Bone) {
			Controller.AddBoneCurve(BoneNames[Bone], false);
			Controller.SetBoneTrackKeys(BoneNames[Bone], PositionKeys[Bone], RotationKeys[Bone], ScaleKeys[Bone], false);
		}

		Controller.NotifyPopulated();
		Controller.CloseBracket(false);
	}
#elif ENGINE_UE4
	Sequence->SetRawNumberOfFrame(FrameCount);
	Sequence->SequenceLength = FrameCount > 1 ? static_cast<float>(FrameCount - 1) / FrameRate : 1.0f;

	for (int32 Bone = 0; Bone < BoneNames.Num(); ++Bone) {
		FRawAnimSequenceTrack Track;

		Track.PosKeys = PositionKeys[Bone];
		Track.RotKeys = RotationKeys[Bone];
		Track.ScaleKeys = ScaleKeys[Bone];

		Sequence->AddNewRawTrack(BoneNames[Bone], &Track);
	}

	Sequence->PostProcessSequence();
#else
	UE_LOG(LogReflection, Warning, TEXT("\"%s\" cannot be built on this engine version"), *SequenceName);

	return nullptr;
#endif

	UPackage* PosePackage = CreatePackage(
#if UE4_25_BELOW || (UE4_26_0)
		nullptr,
#endif
		*(Folder / PoseAssetName));
	if (PosePackage == nullptr) return nullptr;

	PosePackage->FullyLoad();

	UPoseAsset* PoseAsset = NewObject<UPoseAsset>(PosePackage, FName(*PoseAssetName), RF_Public | RF_Standalone);

	PoseAsset->SetSkeleton(Skeleton);
	PoseAsset->SetPreviewMesh(SkeletalMesh);
	PoseAsset->CreatePoseFromAnimation(Sequence, &PoseNames);

	PoseAsset->ConvertSpace(true, 0);

	PoseAsset->SourceAnimation = Sequence;

	HandleAssetCreation(Sequence, SequencePackage);
	HandleAssetCreation(PoseAsset, PosePackage);

	if (GetSettings()->AssetSettings.SaveAssets) {
		SavePackage(SequencePackage);
		SavePackage(PosePackage);
	}

	UE_LOG(LogReflection, Display,
		TEXT("\"%s\" built %d pose(s) over %d bone(s) out of the Cloud's reading of its DNA%s"),
		*GetAssetName(), PoseDeltas.Num(), BoneNames.Num(),
		bBackported ? TEXT(", read back into an older head's curves") : TEXT(""));

	if (Untouched > 0) {
		UE_LOG(LogReflection, Display,
			TEXT("\"%s\" left %d joint(s) untracked, no control of this face moves them"),
			*GetAssetName(), Untouched);
	}

	return PoseAsset;
}
