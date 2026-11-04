#pragma once

#include "CoreMinimal.h"
#include "Engine/Compatibility.h"

#if UE4_25_BELOW
#include "Framework/MultiBox/MultiBoxBuilder.h"
#endif

struct IParentDropdownBuilder {
	virtual ~IParentDropdownBuilder() = default;
	virtual void Build(FMenuBuilder& MenuBuilder) const = 0;
};
