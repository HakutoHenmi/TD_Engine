#include "Input.h"
#include "WindowDX.h"
#include "imgui.h"
#include <cassert>
#include <algorithm>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "Xinput.lib")

namespace Engine {

Input* Input::instance_ = nullptr;

void Input::Initialize(HINSTANCE hInst, HWND hwnd) {
	instance_ = this;
	hwnd_ = hwnd;
	HRESULT hr;

	// --- DirectInput本体 ---
	hr = DirectInput8Create(hInst, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)di_.GetAddressOf(), nullptr);
	assert(SUCCEEDED(hr) && "DirectInput8Create failed");

	// --- Keyboard ---
	hr = di_->CreateDevice(GUID_SysKeyboard, kb_.GetAddressOf(), nullptr);
	assert(SUCCEEDED(hr) && "Keyboard CreateDevice failed");

	hr = kb_->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr) && "Keyboard SetDataFormat failed");

	hr = kb_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr) && "Keyboard SetCooperativeLevel failed");

	kb_->Acquire();

	// --- Mouse ---
	hr = di_->CreateDevice(GUID_SysMouse, mouse_.GetAddressOf(), nullptr);
	assert(SUCCEEDED(hr) && "Mouse CreateDevice failed");

	hr = mouse_->SetDataFormat(&c_dfDIMouse2);
	assert(SUCCEEDED(hr) && "Mouse SetDataFormat failed");

	hr = mouse_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(hr) && "Mouse SetCooperativeLevel failed");

	mouse_->Acquire();

	// 初期化
	ZeroMemory(keyState_, 256);
	ZeroMemory(prevKey_, 256);
	ZeroMemory(&mouseState_, sizeof(mouseState_));
	ZeroMemory(&prevMouseState_, sizeof(prevMouseState_));
	mouseX_ = mouseY_ = wheel_ = 0.0f;
	absMouseX_ = absMouseY_ = 0.0f;

	ZeroMemory(&gamepadState_, sizeof(XINPUT_GAMEPAD));
	ZeroMemory(&prevGamepadState_, sizeof(XINPUT_GAMEPAD));
	controllerConnected_ = false;
	activeDevice_ = InputDeviceType::KeyboardMouse;
}

float Input::ApplyDeadzone(SHORT value, SHORT deadzone) {
	if (value < -deadzone) {
		return (float)(value + deadzone) / (32768.0f - deadzone);
	} else if (value > deadzone) {
		return (float)(value - deadzone) / (32767.0f - deadzone);
	}
	return 0.0f;
}

void Input::Update() {
	// --- Keyboard更新 ---
	memcpy(prevKey_, keyState_, 256);
	if (FAILED(kb_->GetDeviceState(256, keyState_))) {
		kb_->Acquire();
		kb_->GetDeviceState(256, keyState_);
	}

	// --- Mouse更新 ---
	prevMouseState_ = mouseState_;
	if (FAILED(mouse_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_))) {
		mouse_->Acquire();
		mouse_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_);
	}

	// エディタUIが入力をキャプチャしている場合、ゲームスクリプト側の入力をブロック
	// ★注意: プレイ中はゲーム入力を常に有効にする（ビューポートもImGui Image内なのでWantCaptureMouse=trueになるため）
	// カメラ操作は Camera::Update 側で別途制御するためここではブロックしない
	gameInputBlocked_ = false;
	if (ImGui::GetCurrentContext()) {
		auto& io = ImGui::GetIO();
		// エディタUIがフォーカスを持っている場合のみフラグを立てる
		// ※プレイ中かどうかはゲーム側で判断してこのフラグを上書きする
		if (io.WantCaptureKeyboard || io.WantCaptureMouse) {
			gameInputBlocked_ = true;
		}
	}

	// マウスの移動差分
	mouseX_ = static_cast<float>(mouseState_.lX);
	mouseY_ = static_cast<float>(mouseState_.lY);

	// ★絶対座標の更新（OSカーソルと同期し、内部解像度にスケーリング）
	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(hwnd_, &pt);

	// クライアント領域のサイズを取得してスケーリング
	RECT rc;
	GetClientRect(hwnd_, &rc);
	float clientW = static_cast<float>(rc.right - rc.left);
	float clientH = static_cast<float>(rc.bottom - rc.top);

	if (clientW > 0 && clientH > 0) {
		absMouseX_ = static_cast<float>(pt.x) * (static_cast<float>(WindowDX::kW) / clientW);
		absMouseY_ = static_cast<float>(pt.y) * (static_cast<float>(WindowDX::kH) / clientH);
	} else {
		absMouseX_ = static_cast<float>(pt.x);
		absMouseY_ = static_cast<float>(pt.y);
	}

	// ホイール量（上:+、下:-）
	wheel_ = static_cast<float>(mouseState_.lZ) / WHEEL_DELTA;

	// --- Controller (XInput) 更新 ---
	prevGamepadState_ = gamepadState_;
	XINPUT_STATE xState;
	ZeroMemory(&xState, sizeof(XINPUT_STATE));

	static int checkTimer = 0;
	bool shouldCheck = true;

	// XInputGetState はコントローラー未接続時に呼ぶと数ミリ秒のブロッキングが発生するため、
	// 未接続時は60フレーム(約1秒)に1回だけポーリングするように制限して最適化する
	if (!controllerConnected_) {
		checkTimer++;
		if (checkTimer < 60) {
			shouldCheck = false;
		} else {
			checkTimer = 0;
		}
	}

	if (shouldCheck) {
		if (XInputGetState(0, &xState) == ERROR_SUCCESS) {
			controllerConnected_ = true;
			gamepadState_ = xState.Gamepad;

			leftStickX_ = ApplyDeadzone(gamepadState_.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
			leftStickY_ = ApplyDeadzone(gamepadState_.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
			rightStickX_ = ApplyDeadzone(gamepadState_.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
			rightStickY_ = ApplyDeadzone(gamepadState_.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

			leftTrigger_ = (gamepadState_.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) ? (float)gamepadState_.bLeftTrigger / 255.0f : 0.0f;
			rightTrigger_ = (gamepadState_.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) ? (float)gamepadState_.bRightTrigger / 255.0f : 0.0f;
		} else {
			controllerConnected_ = false;
			ZeroMemory(&gamepadState_, sizeof(XINPUT_GAMEPAD));
			leftStickX_ = leftStickY_ = rightStickX_ = rightStickY_ = 0.0f;
			leftTrigger_ = rightTrigger_ = 0.0f;
		}
	} else {
		// チェックをスキップしたフレームは前回の未接続状態（ゼロ）を維持する
		ZeroMemory(&gamepadState_, sizeof(XINPUT_GAMEPAD));
		leftStickX_ = leftStickY_ = rightStickX_ = rightStickY_ = 0.0f;
		leftTrigger_ = rightTrigger_ = 0.0f;
	}

	// --- デバイスの自動判別 (最後に操作した方をアクティブにする) ---
	bool hasKeyboardMouseInput = false;
	for (int i = 0; i < 256; ++i) {
		if (keyState_[i] & 0x80) { hasKeyboardMouseInput = true; break; }
	}
	if (mouseX_ != 0.0f || mouseY_ != 0.0f || wheel_ != 0.0f) hasKeyboardMouseInput = true;
	for (int i = 0; i < 8; ++i) {
		if (mouseState_.rgbButtons[i] & 0x80) { hasKeyboardMouseInput = true; break; }
	}

	bool hasControllerInput = false;
	if (controllerConnected_) {
		if (gamepadState_.wButtons != 0 || 
			leftStickX_ != 0.0f || leftStickY_ != 0.0f || 
			rightStickX_ != 0.0f || rightStickY_ != 0.0f || 
			leftTrigger_ > 0.0f || rightTrigger_ > 0.0f) {
			hasControllerInput = true;
		}
	}

	if (hasControllerInput && !hasKeyboardMouseInput) {
		activeDevice_ = InputDeviceType::Controller;
	} else if (hasKeyboardMouseInput) {
		activeDevice_ = InputDeviceType::KeyboardMouse;
	}

	// ★追加: 仮想マウスエミュレーション (コントローラー操作時)
	// UI操作やチュートリアルのクリック判定を全てコントローラーでシームレスに行うため
	if (activeDevice_ == InputDeviceType::Controller) {
		float cursorSpeed = 15.0f; // カーソル移動速度
		int dx = 0, dy = 0;
		if (gamepadState_.wButtons & XINPUT_GAMEPAD_DPAD_UP) dy -= (int)cursorSpeed;
		if (gamepadState_.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) dy += (int)cursorSpeed;
		if (gamepadState_.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) dx -= (int)cursorSpeed;
		if (gamepadState_.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) dx += (int)cursorSpeed;
		
		if (dx != 0 || dy != 0) {
			POINT vpt;
			GetCursorPos(&vpt);
			SetCursorPos(vpt.x + dx, vpt.y + dy);
		}

		// Aボタン/Bボタンが押されている場合、DirectInputのマウスボタン状態を強制上書き
		// これにより既存の input->IsMouseTrigger(0) 等に依存する全UI操作がそのまま動く
		if (gamepadState_.wButtons & XINPUT_GAMEPAD_A) {
			mouseState_.rgbButtons[0] = 0x80;
		}
		if (gamepadState_.wButtons & XINPUT_GAMEPAD_B) {
			mouseState_.rgbButtons[1] = 0x80;
		}
	}
}

void Input::Shutdown() {
	if (kb_)
		kb_->Unacquire();
	if (mouse_)
		mouse_->Unacquire();
	kb_.Reset();
	mouse_.Reset();
	di_.Reset();
	instance_ = nullptr;
}

} // namespace Engine
