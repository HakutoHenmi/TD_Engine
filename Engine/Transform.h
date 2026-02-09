#pragma once
#include "Matrix4x4.h"

namespace Engine {

struct Transform {
	Vector3 scale{1, 1, 1};
	Vector3 rotate{0, 0, 0};
	Vector3 translate{0, 0, 0};

	Matrix4x4 ToMatrix() const {
		using namespace DirectX;
		XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
		XMMATRIX R = XMMatrixRotationRollPitchYaw(rotate.x, rotate.y, rotate.z);
		XMMATRIX T = XMMatrixTranslation(translate.x, translate.y, translate.z);
		Matrix4x4 out;
		XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&out), S * R * T);
		return out;
	}
};

} // namespace Engine
