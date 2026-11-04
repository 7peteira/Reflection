#pragma once

#include "ParentDropdownBuilder.h"
#include "Engine/Compatibility.h"

#if ENGINE_UE5

struct IValidationDropdownBuilder final : IParentDropdownBuilder {
	virtual void Build(FMenuBuilder& MenuBuilder) const override;
};

#endif
