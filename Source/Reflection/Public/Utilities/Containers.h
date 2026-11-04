#pragma once

#include "CoreMinimal.h"

template<typename K, typename V>
FORCEINLINE bool IsEmpty(const TMap<K, V>& Map) {
	return Map.Num() == 0;
}
