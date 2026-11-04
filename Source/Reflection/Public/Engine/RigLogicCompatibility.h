#pragma once

#include "Engine/Compatibility.h"

#if REFLECTION_RIG_LOGIC

#include "RigLogic.h"
#include "RigInstance.h"
#include "DNAAsset.h"
#include "DNAReader.h"
#include "DNAReaderAdapter.h"

#if REFLECTION_RIG_LOGIC_UE_SPACE_READER
#include "RigLogicDNAReader.h"
#endif

inline TSharedPtr<IDNAReader> GetDnaBehaviorReader(UDNAAsset* Asset) {
#if UE5_8_BEYOND
	return Asset->GetDNAReader();
#else
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return Asset->GetBehaviorReader();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
}

inline FString& GetDnaFileName(UDNAAsset* Asset) {
#if UE5_8_BEYOND
	return Asset->DnaFileName_DEPRECATED;
#else
	return Asset->DnaFileName;
#endif
}

inline TArrayView<const float> GetDnaNeutralJoints(const FRigLogic& RigLogic) {
#if UE5_5_BEYOND
	return RigLogic.GetNeutralJointValues();
#else
	return RigLogic.GetRawNeutralJointValues();
#endif
}

inline TArrayView<const float> GetDnaJointOutputs(const FRigInstance& Instance) {
#if UE5_5_BEYOND
	return Instance.GetJointOutputs();
#else
	return Instance.GetRawJointOutputs();
#endif
}

inline FRigLogic MakeDnaRigLogic(const TSharedPtr<IDNAReader>& Behavior) {
#if REFLECTION_RIG_LOGIC_UE_SPACE_READER
	RigLogicDNAReader InUeSpace{Behavior->Unwrap()};
	FDNAReader<RigLogicDNAReader> Wrapper{&InUeSpace};

	return FRigLogic(&Wrapper);
#else
	return FRigLogic(Behavior.Get());
#endif
}

#endif
