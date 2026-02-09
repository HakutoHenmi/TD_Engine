#pragma once

namespace Game {

enum OnOffColor {
	ONOFF_RED = 0,
	ONOFF_BLUE = 1,
};

// 共有状態（Worldを触らずに、全Switch/Blockで同じ状態を見るため）
struct OnOffSharedState {
	static OnOffColor Get() { return state_; }
	static void Toggle() { state_ = (state_ == ONOFF_RED) ? ONOFF_BLUE : ONOFF_RED; }
	static void Set(OnOffColor s) { state_ = s; }

private:
	// C++17以降: inline static でヘッダ定義OK（ODR回避）
	inline static OnOffColor state_ = ONOFF_RED;
};

} // namespace Game
