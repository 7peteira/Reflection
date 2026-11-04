#pragma once

#include "ParentDropdownBuilder.h"

struct IDonateDropdownBuilder final : IParentDropdownBuilder {
	virtual void Build(FMenuBuilder& MenuBuilder) const override;
};
