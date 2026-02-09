#pragma once
// ===============================
//  Input : DirectInput (Keyboard + Mouse)
// ===============================
#include <Windows.h>
#include <dinput.h>
#include <wrl.h>

namespace Engine {

class Input {
public:
	void Initialize(HINSTANCE hInst, HWND hwnd);
	void Update();
	void Shutdown();

	// ===== Keyboard =====
	bool Down(BYTE k) const { return (keyState_[k] & 0x80) != 0; }
	bool Trigger(BYTE k) const { return Down(k) && !(prevKey_[k] & 0x80); }

	// ===== Mouse =====
	float GetMouseDeltaX() const { return mouseX_; }
	float GetMouseDeltaY() const { return mouseY_; }
	float GetMouseWheelDelta() const { return wheel_; }

private:
	// --- DirectInput Core ---
	Microsoft::WRL::ComPtr<IDirectInput8> di_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> kb_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;

	// --- Keyboard ---
	BYTE keyState_[256]{};
	BYTE prevKey_[256]{};

	// --- Mouse ---
	DIMOUSESTATE2 mouseState_{};
	DIMOUSESTATE2 prevMouseState_{};

	float mouseX_ = 0.0f;
	float mouseY_ = 0.0f;
	float wheel_ = 0.0f;
};

} // namespace Engine
