// StageBuilder.h
#pragma once
#include "StageData.h"
#include "StageRuntime.h"

struct StageBuilder {
	static StageRuntime Build(const StageData& data);
};
