#pragma once

#include "ParentDropdownBuilder.h"

struct IGeneralDropdownBuilder final : IParentDropdownBuilder {
	virtual void Build(FMenuBuilder& MenuBuilder) const override;
};
