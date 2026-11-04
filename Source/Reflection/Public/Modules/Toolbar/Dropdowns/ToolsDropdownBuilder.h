#pragma once

#include "ParentDropdownBuilder.h"

struct IToolsDropdownBuilder final : IParentDropdownBuilder {
	virtual void Build(FMenuBuilder& MenuBuilder) const override;
};
