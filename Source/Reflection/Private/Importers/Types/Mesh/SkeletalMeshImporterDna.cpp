#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Mesh/SkeletalMeshImporter.h"

#if REFLECTION_RIG_LOGIC
#include "DNAAsset.h"

#if REFLECTION_DNA_USER_DATA
#include "DNA.h"
#include "DNAAssetUserData.h"
#endif

#include "DNAUtils.h"
#include "Engine/RigLogicCompatibility.h"
#include "SkelMeshDNAUtils.h"
#include "FMemoryResource.h"
#include "riglogic/RigLogic.h"
#include "dna/BinaryStreamReader.h"
#include "dna/BinaryStreamWriter.h"
#include "trio/streams/MemoryStream.h"
#include "Animation/AnimSequence.h"
#include "Animation/PoseAsset.h"

#include "Importers/Types/Animation/LegacyCurves.h"
#include "Animation/AnimData/IAnimationDataController.h"
#endif

#include "Engine/AssetUserData.h"
#include "Engine/EngineUtilities.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Utilities/JsonHelpers.h"

#include "Modules/Cloud/Cloud.h"

bool ISkeletalMeshImporter::ExportNamesDna() {
	const TArray<TSharedPtr<FJsonValue>>* UserData;

	if (!GetAssetData()->TryGetArrayField(TEXT("AssetUserData"), UserData)) {
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Entry : *UserData) {
		const TSharedPtr<FJsonObject> Reference = Entry.IsValid() ? Entry->AsObject() : nullptr;
		if (!Reference.IsValid()) continue;

		FString ObjectName;

		if (Reference->TryGetStringField(TEXT("ObjectName"), ObjectName) && ObjectName.StartsWith(TEXT("DNAAsset"))) {
			return true;
		}
	}

	return false;
}

TArray<uint8> ISkeletalMeshImporter::RewriteDnaForAnimNode(USkeletalMesh* SkeletalMesh, const TArray<uint8>& Dna) {
#if REFLECTION_RIG_LOGIC
	static constexpr int32 AttributesPerJoint = 9;

	TArray<uint8> Source = Dna;

	auto InStream = rl4::makeScoped<trio::MemoryStream>();
	InStream->write(reinterpret_cast<const char*>(Source.GetData()), Source.Num());
	InStream->seek(0);

	auto Reader = rl4::makeScoped<dna::BinaryStreamReader>(
		InStream.get(), dna::DataLayer::All, dna::UnknownLayerPolicy::Preserve, 0u, FMemoryResource::Instance());

	Reader->read();

	if (!rl4::Status::isOk()) {
		UE_LOG(LogReflection, Warning, TEXT("Reading the DNA to rewrite it failed: %s"), ANSI_TO_TCHAR(rl4::Status::get().message));

		return Dna;
	}

	auto OutStream = rl4::makeScoped<trio::MemoryStream>();
	auto Writer = rl4::makeScoped<dna::BinaryStreamWriter>(OutStream.get(), FMemoryResource::Instance());

	Writer->setFrom(Reader.get(), dna::DataLayer::All, dna::UnknownLayerPolicy::Preserve, FMemoryResource::Instance());

	const uint16 JointCount = Reader->getJointCount();

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();

	TArray<dna::Vector3> Translations;
	TArray<dna::Vector3> Rotations;

	Translations.Reserve(JointCount);
	Rotations.Reserve(JointCount);

	int32 Copied = 0;

	for (uint16 Joint = 0; Joint < JointCount; ++Joint) {
		const dna::StringView JointName = Reader->getJointName(Joint);

		const int32 Bone = RefSkeleton.FindBoneIndex(FName(FString(static_cast<int32>(JointName.size()), JointName.data())));

		if (Bone != INDEX_NONE) {
			const FTransform& Bound = RefSkeleton.GetRefBonePose()[Bone];
			const FRotator Rotation = Bound.GetRotation().Rotator();

			Translations.Add(dna::Vector3{
				static_cast<float>(Bound.GetTranslation().X),
				static_cast<float>(-Bound.GetTranslation().Y),
				static_cast<float>(Bound.GetTranslation().Z)
			});

			Rotations.Add(dna::Vector3{
				static_cast<float>(Rotation.Roll),
				static_cast<float>(-Rotation.Pitch),
				static_cast<float>(-Rotation.Yaw)
			});

			Copied++;

			continue;
		}

		dna::Vector3 Translation = Reader->getNeutralJointTranslation(Joint);
		dna::Vector3 Rotation = Reader->getNeutralJointRotation(Joint);

		Translation.y = -Translation.y;

		Rotation.x = -Rotation.x;
		Rotation.z = -Rotation.z;

		Translations.Add(Translation);
		Rotations.Add(Rotation);
	}

	Writer->setNeutralJointTranslations(Translations.GetData(), JointCount);
	Writer->setNeutralJointRotations(Rotations.GetData(), JointCount);

	int32 NegatedRows = 0;

	for (uint16 Group = 0; Group < Reader->getJointGroupCount(); ++Group) {
		const auto Values = Reader->getJointGroupValues(Group);
		const auto Outputs = Reader->getJointGroupOutputIndices(Group);

		if (Values.size() == 0 || Outputs.size() == 0) continue;

		const int32 Columns = static_cast<int32>(Values.size() / Outputs.size());
		if (Columns == 0) continue;

		TArray<float> Patched;
		Patched.Append(Values.data(), static_cast<int32>(Values.size()));

		for (int32 Row = 0; Row < static_cast<int32>(Outputs.size()); ++Row) {
			const int32 Attribute = Outputs[Row] % AttributesPerJoint;

			if (Attribute != 1 && Attribute != 3 && Attribute != 5) continue;

			for (int32 Column = 0; Column < Columns; ++Column) {
				const int32 Index = Row * Columns + Column;

				if (Patched.IsValidIndex(Index)) {
					Patched[Index] = -Patched[Index];
				}
			}

			NegatedRows++;
		}

		Writer->setJointGroupValues(Group, Patched.GetData(), static_cast<uint32>(Patched.Num()));
	}

	Writer->write();

	if (!rl4::Status::isOk()) {
		UE_LOG(LogReflection, Warning, TEXT("Writing the rewritten DNA failed: %s"), ANSI_TO_TCHAR(rl4::Status::get().message));

		return Dna;
	}

	TArray<uint8> Rewritten;
	Rewritten.AddUninitialized(static_cast<int32>(OutStream->size()));

	OutStream->seek(0);
	OutStream->read(reinterpret_cast<char*>(Rewritten.GetData()), OutStream->size());

	UE_LOG(LogReflection, Display, TEXT("\"%s\" rewrote its DNA into the anim node's axes: %d joint(s), %d from the mesh's own bind pose, %d delta row(s)"),
		*GetAssetName(), JointCount, Copied, NegatedRows);

	return Rewritten;
#else
	return Dna;
#endif
}

bool ISkeletalMeshImporter::ApplyDna(USkeletalMesh* SkeletalMesh, const FString& FetchPath) {
#if REFLECTION_RIG_LOGIC
	TArray<uint8> Cooked = Cloud::Export::GetDnaBlocking(FetchPath);

	if (Cooked.Num() == 0) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The mesh's DNA did not come back"),
			TEXT("The Cloud returned nothing for this head's DNA, so there is no face rig to apply.")
		);

		return false;
	}

	UE_LOG(LogReflection, Display, TEXT("\"%s\" fetched %d byte(s) of DNA"), *GetAssetName(), Cooked.Num());

	{
		int32 Start = INDEX_NONE;

		for (int32 Index = 0; Index + 2 < Cooked.Num() && Index < 64; ++Index) {
			if (Cooked[Index] == 'D' && Cooked[Index + 1] == 'N' && Cooked[Index + 2] == 'A') {
				Start = Index;

				break;
			}
		}

		if (Start == INDEX_NONE) {
			FImportIssues::Report(
				EImportIssue::Data,
				TEXT("The mesh's DNA isn't a DNA"),
				TEXT("What came back doesn't start with a DNA stream, so there is nothing RigLogic can read a face out of.")
			);

			return false;
		}

		if (Start > 0) {
#if UE5_4_BEYOND
			Cooked.RemoveAt(0, Start, EAllowShrinking::No);
#else
			Cooked.RemoveAt(0, Start, false);
#endif

			UE_LOG(LogReflection, Display, TEXT("\"%s\" trimmed %d byte(s) ahead of its DNA stream"), *GetAssetName(), Start);
		}
	}

	TArray<uint8> Dna = RewriteDnaForAnimNode(SkeletalMesh, Cooked);

	const TSharedPtr<IDNAReader> Behavior = ReadDNAFromBuffer(&Dna, EDNADataLayer::Behavior | EDNADataLayer::MachineLearnedBehavior, 0u);

	if (!Behavior.IsValid()) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("RigLogic would not read the mesh's DNA"),
			FString::Printf(
				TEXT("'%s' came back as %d byte(s) that this engine's RigLogic rejected: %s. Nothing is put on the mesh, so it has no DNA at all."),
				*GetAssetName(),
				Dna.Num(),
				ANSI_TO_TCHAR(rl4::Status::get().message)
			)
		);

		return false;
	}

	UE_LOG(LogReflection, Display,
		TEXT("\"%s\" read a DNA with %d joint group(s) and %d joint(s)"),
		*GetAssetName(), Behavior->GetJointGroupCount(), Behavior->GetJointCount());

#if REFLECTION_DNA_USER_DATA
	UDNAAssetUserData* DnaUserData = SkeletalMesh->GetAssetUserData<UDNAAssetUserData>();

	if (DnaUserData == nullptr) {
		DnaUserData = NewObject<UDNAAssetUserData>(SkeletalMesh, TEXT("DNAAssetUserData"), RF_Public);

		SkeletalMesh->AddAssetUserData(DnaUserData);
	}

	if (DnaUserData->DNAAsset == nullptr) {
		DnaUserData->DNAAsset = NewObject<UDNA>(SkeletalMesh, TEXT("DNA"), RF_Public);
	}

	DnaUserData->DNAAsset->SetDNAReader(Behavior);
#else
	UDNAAsset* DNAAsset = nullptr;

	if (const TArray<UAssetUserData*>* UserData = SkeletalMesh->GetAssetUserDataArray()) {
		for (UAssetUserData* Entry : *UserData) {
			if (UDNAAsset* Existing = Cast<UDNAAsset>(Entry)) {
				DNAAsset = Existing;

				break;
			}
		}
	}

	if (DNAAsset == nullptr) {
		DNAAsset = NewObject<UDNAAsset>(SkeletalMesh, TEXT("DNAAsset"), RF_Public);

		SkeletalMesh->AddAssetUserData(DNAAsset);
	}

	GetDnaFileName(DNAAsset) = SkeletalMesh->GetName() + TEXT(".dna");
	DNAAsset->SetBehaviorReader(Behavior);

	{
		const UDNAAsset* Found = SkeletalMesh->GetAssetUserData<UDNAAsset>();
		const TArray<UAssetUserData*>* All = SkeletalMesh->GetAssetUserDataArray();

		UE_LOG(LogReflection, Display,
			TEXT("\"%s\" attached DNA as \"MetaHuman DNA Data\": lookup %s, %d entr(ies) of asset user data"),
			*GetAssetName(),
			Found != nullptr ? TEXT("found it") : TEXT("FOUND NOTHING"),
			All != nullptr ? All->Num() : -1);

		if (All != nullptr) {
			for (int32 Index = 0; Index < All->Num(); ++Index) {
				const UAssetUserData* Entry = (*All)[Index];

				UE_LOG(LogReflection, Display, TEXT("    user data %d: %s"),
					Index,
					Entry != nullptr ? *Entry->GetClass()->GetName() : TEXT("<empty slot>"));
			}
		}
	}
#endif

	if (Behavior->GetJointGroupCount() == 0) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The mesh's DNA has no rig in it"),
			FString::Printf(
				TEXT("'%s' carries the face's joints and neutral pose but no behavior, so RigLogic has nothing to drive them with. The head imports and holds still."),
				*SkeletalMesh->GetName()
			)
		);
	}

#if !REFLECTION_DNA_USER_DATA
	if (const TSharedPtr<IDNAReader> Geometry = ReadDNAFromBuffer(&Dna, EDNADataLayer::Geometry, 0u)) {
		DNAAsset->SetGeometryReader(Geometry);
	}
#endif

	return true;
#else
	return false;
#endif
}

#if REFLECTION_RIG_LOGIC
static constexpr int32 GDnaJointAttributesEuler = 9;
static constexpr int32 GDnaJointAttributesQuaternion = 10;

static int32 DnaJointAttributeCount(const TArrayView<const float>& Neutral, const int32 JointCount) {
	if (JointCount <= 0) {
		return GDnaJointAttributesEuler;
	}

	return (Neutral.Num() / JointCount) >= GDnaJointAttributesQuaternion
		? GDnaJointAttributesQuaternion
		: GDnaJointAttributesEuler;
}

namespace {
	FTransform ComposeDnaJoint(const TArrayView<const float>& Neutral, const TArrayView<const float>& Delta, const int32 Attribute, const int32 Stride) {
		const auto Read = [](const TArrayView<const float>& Values, const int32 Index) {
			return Values.IsValidIndex(Index) ? Values[Index] : 0.0f;
		};

		if (Stride >= GDnaJointAttributesQuaternion) {
			const FVector QTranslation(
				Read(Neutral, Attribute + 0) + Read(Delta, Attribute + 0),
				Read(Neutral, Attribute + 1) + Read(Delta, Attribute + 1),
				Read(Neutral, Attribute + 2) + Read(Delta, Attribute + 2)
			);

			const auto ReadRotation = [&Read](const TArrayView<const float>& Values, const int32 At) {
				const FQuat Quaternion(Read(Values, At + 3), Read(Values, At + 4), Read(Values, At + 5), Read(Values, At + 6));

				return Quaternion.SizeSquared() > SMALL_NUMBER ? Quaternion.GetNormalized() : FQuat::Identity;
			};

			const FQuat QRotation = ReadRotation(Neutral, Attribute) * ReadRotation(Delta, Attribute);

			const FVector QScale(
				Read(Neutral, Attribute + 7) + Read(Delta, Attribute + 7),
				Read(Neutral, Attribute + 8) + Read(Delta, Attribute + 8),
				Read(Neutral, Attribute + 9) + Read(Delta, Attribute + 9)
			);

			return FTransform(QRotation, QTranslation, QScale);
		}

		const FVector Translation(
			Read(Neutral, Attribute + 0) + Read(Delta, Attribute + 0),
			-(Read(Neutral, Attribute + 1) + Read(Delta, Attribute + 1)),
			Read(Neutral, Attribute + 2) + Read(Delta, Attribute + 2)
		);

		const FQuat Rotation =
			FQuat(FRotator(-Read(Neutral, Attribute + 4), -Read(Neutral, Attribute + 5), Read(Neutral, Attribute + 3))) *
			FQuat(FRotator(-Read(Delta, Attribute + 4), -Read(Delta, Attribute + 5), Read(Delta, Attribute + 3)));

		const FVector Scale(
			Read(Neutral, Attribute + 6) + Read(Delta, Attribute + 6),
			Read(Neutral, Attribute + 7) + Read(Delta, Attribute + 7),
			Read(Neutral, Attribute + 8) + Read(Delta, Attribute + 8)
		);

		return FTransform(Rotation, Translation, Scale);
	}
}
#endif

bool ISkeletalMeshImporter::AlignBindPoseToDna(USkeletalMesh* SkeletalMesh) {
#if REFLECTION_RIG_LOGIC
	UDNAAsset* DNAAsset = USkelMeshDNAUtils::GetMeshDNA(SkeletalMesh);
	if (DNAAsset == nullptr) return false;

	const TSharedPtr<IDNAReader> Behavior = GetDnaBehaviorReader(DNAAsset);
	if (!Behavior.IsValid()) return false;

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (Skeleton == nullptr) return false;

	FRigLogic RigLogic = MakeDnaRigLogic(Behavior);

	const TArrayView<const float> Neutral = GetDnaNeutralJoints(RigLogic);
	const int32 Stride = DnaJointAttributeCount(Neutral, Behavior->GetJointCount());

	int32 Aligned = 0;

	{
	FReferenceSkeletonModifier Modifier(SkeletalMesh->GetRefSkeleton(), Skeleton);

	for (uint16 Joint = 0; Joint < Behavior->GetJointCount(); ++Joint) {
		if (Behavior->GetJointParentIndex(Joint) == Joint) continue;

		const int32 Bone = SkeletalMesh->GetRefSkeleton().FindBoneIndex(FName(*Behavior->GetJointName(Joint)));
		if (Bone == INDEX_NONE) continue;

		Modifier.UpdateRefPoseTransform(Bone, ComposeDnaJoint(Neutral, {}, Joint * Stride, Stride));

		Aligned++;
	}

	}

	SkeletalMesh->GetRefBasesInvMatrix().Reset();

	UE_LOG(LogReflection, Display, TEXT("\"%s\" bound %d joint(s) to its DNA's neutral"), *GetAssetName(), Aligned);

	return Aligned > 0;
#else
	return false;
#endif
}

struct FDnaPosePlan {
	FName Name;
	TArray<TPair<uint16, float>> Drive;
};

#if REFLECTION_RIG_LOGIC

namespace {
	TMap<FString, uint16> MapControlsByName(const TSharedPtr<IDNAReader>& Behavior) {
		const int32 ControlCount = Behavior->GetRawControlCount();

		TMap<FString, uint16> ByName;
		ByName.Reserve(ControlCount);

		for (int32 Control = 0; Control < ControlCount; ++Control) {
			ByName.Add(
				Behavior->GetRawControlName(static_cast<uint16>(Control)).Replace(TEXT("."), TEXT("_")),
				static_cast<uint16>(Control));
		}

		return ByName;
	}
}

bool ISkeletalMeshImporter::BuildBackportedPosePlan(const TSharedPtr<IDNAReader>& Behavior, TArray<FDnaPosePlan>& OutPlan) {
	TMap<FName, TArray<FLegacyCurveDrive>> ByCurve;

	if (!FReflectionLegacyCurves::Read(ByCurve)) return false;

	const TMap<FString, uint16> ByName = MapControlsByName(Behavior);

	TArray<FName> Ordered;
	ByCurve.GetKeys(Ordered);
	Ordered.Sort(FNameLexicalLess());

	int32 Unresolved = 0;

	for (const FName& Source : Ordered) {
		FDnaPosePlan Pose;
		Pose.Name = Source;

		for (const FLegacyCurveDrive& Drive : ByCurve[Source]) {
			const uint16* Control = ByName.Find(Drive.Control.ToString());

			if (Control == nullptr) {
				Unresolved++;

				continue;
			}

			Pose.Drive.Add({ *Control, Drive.Weight });
		}

		if (Pose.Drive.Num() == 0) continue;

		OutPlan.Add(MoveTemp(Pose));
	}

	if (OutPlan.Num() == 0) return false;

	UE_LOG(LogReflection, Display,
		TEXT("\"%s\" backporting %d pose(s) from the curve mapping%s"),
		*GetAssetName(), OutPlan.Num(),
		Unresolved > 0 ? *FString::Printf(TEXT(", %d control reference(s) this DNA hasn't got"), Unresolved) : TEXT(""));

	return true;
}
#else

bool ISkeletalMeshImporter::BuildBackportedPosePlan(const TSharedPtr<IDNAReader>&, TArray<FDnaPosePlan>&) {
	return false;
}

#endif

UPoseAsset* ISkeletalMeshImporter::BakeDnaPoseAsset(USkeletalMesh* SkeletalMesh) {
#if REFLECTION_RIG_LOGIC
	UDNAAsset* DNAAsset = USkelMeshDNAUtils::GetMeshDNA(SkeletalMesh);
	if (DNAAsset == nullptr) return nullptr;

	const TSharedPtr<IDNAReader> Behavior = GetDnaBehaviorReader(DNAAsset);
	if (!Behavior.IsValid()) return nullptr;

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (Skeleton == nullptr) return nullptr;

	const int32 ControlCount = Behavior->GetRawControlCount();
	if (ControlCount == 0) return nullptr;

	if (Behavior->GetJointGroupCount() == 0 || Behavior->GetLODCount() == 0) {
		return nullptr;
	}

	FRigLogic RigLogic = MakeDnaRigLogic(Behavior);
	FRigInstance Instance(&RigLogic);

	const TArrayView<const float> Neutral = GetDnaNeutralJoints(RigLogic);
	const int32 Stride = DnaJointAttributeCount(Neutral, Behavior->GetJointCount());

	UE_LOG(LogReflection, Display,
		TEXT("\"%s\" rig reports %d attribute(s) per joint (%s rotation)"),
		*GetAssetName(), Stride,
		Stride >= GDnaJointAttributesQuaternion ? TEXT("quaternion") : TEXT("euler"));

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();

	TArray<FName> BoneNames;
	TArray<int32> BoneJoints;

	for (uint16 Joint = 0; Joint < Behavior->GetJointCount(); ++Joint) {
		if (Behavior->GetJointParentIndex(Joint) == Joint) continue;

		const FName BoneName(*Behavior->GetJointName(Joint));

		const int32 Bone = RefSkeleton.FindBoneIndex(BoneName);
		if (Bone == INDEX_NONE) continue;

		BoneNames.Add(BoneName);
		BoneJoints.Add(Joint);
	}

	if (BoneNames.Num() == 0) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The DNA's joints aren't the mesh's bones"),
			TEXT("None of the joints the DNA drives are named in the skeleton, so a pose built out of it would move nothing.")
		);

		return nullptr;
	}

	TArray<FDnaPosePlan> Plan;

	const ERDnaCurves Curves = GetModdingAssetSettings().MetaHuman.Curves;

	const bool bBackported = WantsLegacyCurves(Curves) && BuildBackportedPosePlan(Behavior, Plan);

	if (!bBackported || KeepsRigControls(Curves)) {
		Plan.Reserve(Plan.Num() + ControlCount);

		TSet<FName> Standing;

		for (const FDnaPosePlan& Pose : Plan) {
			Standing.Add(Pose.Name);
		}

		for (int32 Control = 0; Control < ControlCount; ++Control) {
			const FName Named = FName(*Behavior->GetRawControlName(static_cast<uint16>(Control)).Replace(TEXT("."), TEXT("_")));

			if (Standing.Contains(Named)) continue;

			Plan.Add({ Named, { { static_cast<uint16>(Control), 1.0f } } });
		}
	}

	const int32 FrameCount = Plan.Num() + 1;

	TArray<FName> PoseNames;
	PoseNames.Reserve(FrameCount);

	TArray<TArray<FVector>> PositionKeys;
	TArray<TArray<FQuat>> RotationKeys;
	TArray<TArray<FVector>> ScaleKeys;

	PositionKeys.SetNum(BoneNames.Num());
	RotationKeys.SetNum(BoneNames.Num());
	ScaleKeys.SetNum(BoneNames.Num());

	const auto WriteFrame = [&](const TArrayView<const float>& Delta) {
		for (int32 Bone = 0; Bone < BoneNames.Num(); ++Bone) {
			const FTransform Local = ComposeDnaJoint(Neutral, Delta, BoneJoints[Bone] * Stride, Stride);

			PositionKeys[Bone].Add(Local.GetTranslation());
			RotationKeys[Bone].Add(Local.GetRotation());
			ScaleKeys[Bone].Add(Local.GetScale3D());
		}
	};

	WriteFrame({});
	PoseNames.Add(TEXT("base_pose"));

	for (const FDnaPosePlan& Pose : Plan) {
		for (int32 Reset = 0; Reset < ControlCount; ++Reset) {
			Instance.SetRawControl(static_cast<uint16>(Reset), 0.0f);
		}

		for (const TPair<uint16, float>& Drive : Pose.Drive) {
			Instance.SetRawControl(Drive.Key, Drive.Value);
		}

		RigLogic.Calculate(&Instance);

		WriteFrame(GetDnaJointOutputs(Instance));
		PoseNames.Add(Pose.Name);
	}

	const FString Folder = FPackageName::GetLongPackagePath(GetPackage()->GetName());

	const FString SequenceName = GetAssetName() + (bBackported
		? TEXT("_DNA_Facial_Pose_Backport")
		: TEXT("_DNA_Facial_Pose_Export"));
	const FString PoseAssetName = SequenceName + TEXT("_PoseAsset");

	UPackage* SequencePackage = CreatePackage(*(Folder / SequenceName));
	if (SequencePackage == nullptr) return nullptr;

	SequencePackage->FullyLoad();

	UAnimSequence* Sequence = NewObject<UAnimSequence>(SequencePackage, FName(*SequenceName), RF_Public | RF_Standalone);
	Sequence->SetSkeleton(Skeleton);
	Sequence->SetPreviewMesh(SkeletalMesh);

	{
		IAnimationDataController& Controller = Sequence->GetController();

		Controller.OpenBracket(NSLOCTEXT("Reflection", "BuildDnaPoses", "Building poses from DNA"), false);
		Controller.InitializeModel();

		Controller.SetFrameRate(FFrameRate(30, 1), false);
		Controller.SetNumberOfFrames(FFrameNumber(FMath::Max(1, FrameCount - 1)), false);

		for (int32 Bone = 0; Bone < BoneNames.Num(); ++Bone) {
			Controller.AddBoneCurve(BoneNames[Bone], false);
			Controller.SetBoneTrackKeys(BoneNames[Bone], PositionKeys[Bone], RotationKeys[Bone], ScaleKeys[Bone], false);
		}

		Controller.NotifyPopulated();
		Controller.CloseBracket(false);
	}

	UPackage* PosePackage = CreatePackage(*(Folder / PoseAssetName));
	if (PosePackage == nullptr) return nullptr;

	PosePackage->FullyLoad();

	UPoseAsset* PoseAsset = NewObject<UPoseAsset>(PosePackage, FName(*PoseAssetName), RF_Public | RF_Standalone);

	PoseAsset->SetSkeleton(Skeleton);
	PoseAsset->SetPreviewMesh(SkeletalMesh);
#if UE5_3_BEYOND
	PoseAsset->CreatePoseFromAnimation(Sequence, &PoseNames);
#else
	TArray<FSmartName> Named;
	Named.Reserve(PoseNames.Num());

	for (const FName& One : PoseNames) {
		FSmartName Found;

		Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, One, Found);
		Named.Add(Found);
	}

	PoseAsset->CreatePoseFromAnimation(Sequence, &Named);
#endif

	PoseAsset->ConvertSpace(true, 0);

	PoseAsset->SourceAnimation = Sequence;

	HandleAssetCreation(Sequence, SequencePackage);
	HandleAssetCreation(PoseAsset, PosePackage);

	if (GetSettings()->AssetSettings.SaveAssets) {
		SavePackage(SequencePackage);
		SavePackage(PosePackage);
	}

	return PoseAsset;
#else
	return nullptr;
#endif
}
