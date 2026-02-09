// StageLoader.h
#pragma once
#include "StageData.h"
#include <string>

struct StageLoader {
	static bool LoadFromFile(const std::string& path, StageData& out);
};
