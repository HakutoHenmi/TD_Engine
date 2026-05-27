#pragma once
// ===============================
//  Input : DirectInput (Keyboard + Mouse) + XInput (Controller)
// ===============================
#include <Windows.h>
#include <dinput.h>
#include <Xinput.h>
#include <wrl.h>

namespace Engine {

// 入力デバイスの種類
enum class InputDeviceType {
	KeyboardMouse,
	Controller
};

class Input {
public:
	void Initialize(HINSTANCE hInst, HWND hwnd);
	void Update();
	void Shutdown();

	static Input* GetInstance() { return instance_; }

	// ===== Keyboard =====
	bool Down(BYTE k) const { return (keyState_[k] & 0x80) != 0; }
	bool Trigger(BYTE k) const { return Down(k) && !(prevKey_[k] & 0x80); }

	// ===== Mouse =====
	float GetMouseDeltaX() const { return mouseX_; }
	float GetMouseDeltaY() const { return mouseY_; }
	float GetMouseWheelDelta() const { return wheel_; }

	// ★追加: 絶対座標とボタン状態
	void GetMousePos(float& x, float& y) const { x = absMouseX_; y = absMouseY_; }
	bool IsMouseDown(int button) const { return (mouseState_.rgbButtons[button] & 0x80) != 0; }
	bool IsMouseTrigger(int button) const { return IsMouseDown(button) && !(prevMouseState_.rgbButtons[button] & 0x80); }

	// ===== Controller (XInput) =====
	bool IsControllerConnected() const { return controllerConnected_; }
	
	// ボタン状態 (XINPUT_GAMEPAD_A など)
	bool IsControllerButtonDown(WORD button) const { return (gamepadState_.wButtons & button) != 0; }
	bool IsControllerButtonTrigger(WORD button) const { return IsControllerButtonDown(button) && !(prevGamepadState_.wButtons & button); }
	
	// スティック (-1.0f ~ 1.0f)
	float GetLeftStickX() const { return leftStickX_; }
	float GetLeftStickY() const { return leftStickY_; }
	float GetRightStickX() const { return rightStickX_; }
	float GetRightStickY() const { return rightStickY_; }
	
	// トリガー (0.0f ~ 1.0f)
	float GetLeftTrigger() const { return leftTrigger_; }
	float GetRightTrigger() const { return rightTrigger_; }

	// アクティブデバイス（現在どちらで操作しているか）
	InputDeviceType GetActiveDeviceType() const { return activeDevice_; }

	// ★追加: エディタがゲーム入力をブロックするフラグ
	// エディタUI操作中にゲームスクリプト側の入力を無効化するが、カメラ操作は別途制御する
	void SetGameInputBlocked(bool blocked) { gameInputBlocked_ = blocked; }
	bool IsGameInputBlocked() const { return gameInputBlocked_; }

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
	float absMouseX_ = 0.0f; // ★追加
	float absMouseY_ = 0.0f; // ★追加
	float wheel_ = 0.0f;
	HWND hwnd_ = nullptr; // ★追加
	bool gameInputBlocked_ = false; // ★追加: エディタがゲーム入力をブロック中か

	// --- Controller (XInput) ---
	XINPUT_GAMEPAD gamepadState_{};
	XINPUT_GAMEPAD prevGamepadState_{};
	bool controllerConnected_ = false;
	
	float leftStickX_ = 0.0f;
	float leftStickY_ = 0.0f;
	float rightStickX_ = 0.0f;
	float rightStickY_ = 0.0f;
	float leftTrigger_ = 0.0f;
	float rightTrigger_ = 0.0f;
	
	InputDeviceType activeDevice_ = InputDeviceType::KeyboardMouse;
	
	// デッドゾーン処理
	float ApplyDeadzone(SHORT value, SHORT deadzone);

	static Input* instance_;
};

} // namespace Engine
