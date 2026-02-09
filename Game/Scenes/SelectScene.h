#pragma once
#include "IScene.h"
#include <Windows.h>
#include <string>

namespace Game {

class SelectScene final : public Engine::IScene {
public:
	void Initialize(Engine::WindowDX* dx) override;
	void Update() override;
	void Draw() override;

	bool IsEnd() const override { return end_; }
	std::string Next() const override { return next_; }

private:
	bool WasPressed_(int vk) {
		SHORT now = GetAsyncKeyState(vk);
		bool pressed = ((now & 0x8000) != 0) && ((prevKey_[vk] & 0x8000) == 0);
		prevKey_[vk] = now;
		return pressed;
	}

private:
	bool end_ = false;
	std::string next_{};
	SHORT prevKey_[256]{};
};

} // namespace Game