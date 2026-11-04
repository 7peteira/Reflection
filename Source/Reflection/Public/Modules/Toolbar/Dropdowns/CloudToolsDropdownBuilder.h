#pragma once

#include "ParentDropdownBuilder.h"

struct ICloudToolsDropdownBuilder final : IParentDropdownBuilder {
	virtual void Build(FMenuBuilder& MenuBuilder) const override;
};
