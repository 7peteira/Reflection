#pragma once

#include "Misc/EngineVersionComparison.h"
#include "Templates/SharedPointer.h"

#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION < 24

template <typename ObjectType, ESPMode Mode>
FORCEINLINE bool operator==(const TSharedPtr<ObjectType, Mode>& Pointer, decltype(nullptr)) {
	return !Pointer.IsValid();
}

template <typename ObjectType, ESPMode Mode>
FORCEINLINE bool operator==(decltype(nullptr), const TSharedPtr<ObjectType, Mode>& Pointer) {
	return !Pointer.IsValid();
}

template <typename ObjectType, ESPMode Mode>
FORCEINLINE bool operator!=(const TSharedPtr<ObjectType, Mode>& Pointer, decltype(nullptr)) {
	return Pointer.IsValid();
}

template <typename ObjectType, ESPMode Mode>
FORCEINLINE bool operator!=(decltype(nullptr), const TSharedPtr<ObjectType, Mode>& Pointer) {
	return Pointer.IsValid();
}

#endif
