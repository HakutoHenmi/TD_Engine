//======================================================
// BoostRingGimmick.cpp
//======================================================
#include "BoostRingGimmick.h"
#include "../Actors/PlayerBall.h"
#include "GimmickFactory.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace Game {

// ★魔法の1行：エディタに出す（PDF通り）【GimmickRegistrar】
// 表示名は GetGimmickName と揃えるのが安全
static GimmickRegistrar<BoostRingGimmick> registrar("BoostRing");

void BoostRingGimmick::Start(Engine::GameObject* owner) {
	GimmickBase::Start(owner);
	cooldownTimer_ = 0.0f;
	wasInside_ = false;

	// ★追加: 効果音ロード
	auto* audio = Engine::Audio::GetInstance();
	seBoostHandle_ = audio->Load("Resources/Sound/bu-sutopaddo.mp3");
}

bool BoostRingGimmick::IsHitXZ_(const Engine::Vector3& p, float pr, const Engine::Vector3& ringPos) const {

	// ★owner_ の scale を反映（X/Zの大きい方）
	float scaleXZ = owner_->transform.scale.x;
	if (owner_->transform.scale.z > scaleXZ)
		scaleXZ = owner_->transform.scale.z;

	float scaledRadius = radius_ * scaleXZ;

	float dx = p.x - ringPos.x;
	float dz = p.z - ringPos.z;

	// 「中に入ったら」なので半径以内
	float r = pr + scaledRadius;
	return (dx * dx + dz * dz) <= (r * r);
}

void BoostRingGimmick::Update() {
	// TODO: あなたのエンジンの dt 取得に置き換え
	float dt = 0.016f;

	if (cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= dt;
		if (cooldownTimer_ < 0.0f)
			cooldownTimer_ = 0.0f;
	}
}

void BoostRingGimmick::OnCollision(void* other) {
	// OutputDebugStringA("[BoostRing] OnCollision called\n");
	other = other; // 未使用回避
	               ////==================================================
	               //// 1) OnCollision が呼ばれてるか＆型が合ってるか確認
	               ////==================================================

	//// もし Engine 側が "PlayerBall*" を渡してるならこれでOK
	// PlayerBall* player = static_cast<PlayerBall*>(other);

	//// ★もしここが怪しい場合：Engine 側が GameObject* を渡してるパターンがある
	//// その場合は上の1行をコメントアウトして、下みたいに受け取る必要がある
	//// Engine::GameObject* otherObj = static_cast<Engine::GameObject*>(other);
	//// if (!otherObj) return;
	//// if (static_cast<Game::ObjectType>(otherObj->type) != Game::ObjectType::PLAYER) return;
	//// PlayerBall* player = reinterpret_cast<PlayerBall*>(otherObj->userPtr); // ← こういう仕組みがある場合

	// if (!player || !owner_) {
	//	return;
	// }

	////==================================================
	//// 2) クールダウン
	////==================================================
	// if (cooldownTimer_ > 0.0f) {
	//	return;
	// }

	////==================================================
	//// 3) XZ 半径判定（★モデルスケールを反映）
	////==================================================
	// const Engine::Vector3 ringPos = owner_->transform.translate;

	//// 見た目が拡大されてるのに radius_ が固定だと、判定が小さすぎて当たらない
	// float scaleXZ = owner_->transform.scale.x;
	// if (owner_->transform.scale.z > scaleXZ)
	//	scaleXZ = owner_->transform.scale.z;
	// float scaledRingRadius = radius_ * scaleXZ;

	// const Engine::Vector3 ppos = player->GetPosition();
	// float dx = ppos.x - ringPos.x;
	// float dz = ppos.z - ringPos.z;
	// float r = (player->GetRadius() + scaledRingRadius);
	// if ((dx * dx + dz * dz) > (r * r)) {
	//	return;
	// }

	////==================================================
	//// 4) 高さ判定（★デバッグしやすく＆少し緩める）
	////==================================================
	// float playerBottomY = ppos.y - player->GetRadius();

	//// もしリングモデルの原点が「中心」で、見た目の地面と ringPos.y がズレてる場合があるので
	//// とりあえず tolerance を効かせやすいように dy を作る
	// float dy = playerBottomY - ringPos.y;

	//// 下から来たら無視（床の下を通過してるなど）
	// if (dy < -0.5f) {
	//	return;
	// }

	//// 上に浮いてると無視（ただし厳しすぎると発動しないので、まずは 1.0f くらいで様子見）
	// if (dy > heightTolerance_) {
	//	return;
	// }

	////==================================================
	//// 5) 速度下限（★停止でも押し出す）
	////==================================================
	// Engine::Vector3 v = player->GetVelocity();
	// float speedXZ = std::sqrt(v.x * v.x + v.z * v.z);

	//// 止まっててもブーストしてほしいなら、向きが取れないので
	//// 「リング中心→プレイヤー」の向きで押し出す
	// Engine::Vector3 dir{0.0f, 0.0f, 0.0f};

	// if (speedXZ >= 0.001f) {
	//	dir = {v.x / speedXZ, 0.0f, v.z / speedXZ};
	// } else {
	//	float len = std::sqrt(dx * dx + dz * dz);
	//	if (len < 0.001f) {
	//		// ど真ん中で停止してるなら、とりあえずZ+へ
	//		dir = {0.0f, 0.0f, 1.0f};
	//	} else {
	//		dir = {dx / len, 0.0f, dz / len};
	//	}
	// }

	////==================================================
	//// 6) 加速：最低速度に引き上げる（Going Balls 型）
	////==================================================
	// float target = boostSpeed_;
	// if (speedXZ < target) {
	//	v.x = dir.x * target;
	//	v.z = dir.z * target;
	//	player->SetVelocity(v);
	// }

	// cooldownTimer_ = cooldownSec_;
}

void BoostRingGimmick::CheckTrigger(Game::PlayerBall* player) {
	if (!owner_ || !player)
		return;

	using namespace DirectX;

	//==================================================
	// 0) dt（エンジンから取れない想定なのでfallback）
	//==================================================
	// ★本当は Engine::GetDeltaTime() 等に置き換えてOK
	const float dt = 1.0f / 60.0f;

	//==================================================
	// 1) 前フレーム→今フレーム の線分（スイープ判定用）
	//==================================================
	Engine::Vector3 p1 = player->GetPosition(); // 今
	Engine::Vector3 p0 = p1;                    // 前（初回は今と同じ）

	if (hasPrevPlayerPos_) {
		p0 = prevPlayerPos_;
	}
	prevPlayerPos_ = p1;
	hasPrevPlayerPos_ = true;

	//==================================================
	// 2) ワールド→ローカル（前と今の両方）
	//==================================================
	XMMATRIX S = XMMatrixScaling(owner_->transform.scale.x, owner_->transform.scale.y, owner_->transform.scale.z);
	XMMATRIX R = XMMatrixRotationRollPitchYaw(owner_->transform.rotate.x, owner_->transform.rotate.y, owner_->transform.rotate.z);
	XMMATRIX T = XMMatrixTranslation(owner_->transform.translate.x, owner_->transform.translate.y, owner_->transform.translate.z);
	XMMATRIX W = S * R * T;
	XMMATRIX invW = XMMatrixInverse(nullptr, W);

	auto ToLocal = [&](const Engine::Vector3& wp) -> Engine::Vector3 {
		XMVECTOR lp = XMVector3TransformCoord(XMVectorSet(wp.x, wp.y, wp.z, 1.0f), invW);
		return Engine::Vector3{XMVectorGetX(lp), XMVectorGetY(lp), XMVectorGetZ(lp)};
	};

	Engine::Vector3 a = ToLocal(p0); // 前（local）
	Engine::Vector3 b = ToLocal(p1); // 今（local）

	//==================================================
	// 3) 厚み方向＝ローカルX、穴＝YZ（あなたの判定そのまま）
	//==================================================
	const float centerX = (owner_->localAABBMin.x + owner_->localAABBMax.x) * 0.5f;
	const float halfX = (owner_->localAABBMax.x - owner_->localAABBMin.x) * 0.5f;

	//==================================================
	// 4) 穴の半径（非均一スケール対応：楕円）
	//==================================================
	const float sy = std::abs(owner_->transform.scale.y);
	const float sz = std::abs(owner_->transform.scale.z);

	float ay = innerRadius_ * sy - player->GetRadius() - margin_;
	float az = innerRadius_ * sz - player->GetRadius() - margin_;

	if (ay <= 1e-6f || az <= 1e-6f) {
		wasInside_ = false;
		return;
	}

	//==================================================
	// 5) スイープ厚み判定（Xスラブ）
	//==================================================
	float x0 = a.x - centerX;
	float x1 = b.x - centerX;
	float dx = x1 - x0;

	float slab = (halfX + thickness_);

	float tMin = 0.0f, tMax = 1.0f;

	if (std::abs(dx) < 1e-6f) {
		if (std::abs(x0) > slab) {
			wasInside_ = false;
			return;
		}
	} else {
		float inv = 1.0f / dx;
		float tA = (-slab - x0) * inv;
		float tB = (slab - x0) * inv;
		if (tA > tB)
			std::swap(tA, tB);

		tMin = (std::max)(tMin, tA);
		tMax = (std::min)(tMax, tB);

		if (tMin > tMax) {
			wasInside_ = false;
			return;
		}
	}

	//==================================================
	// 6) スイープ穴判定（YZ楕円）
	//==================================================
	float y0 = a.y, y1 = b.y;
	float z0 = a.z, z1 = b.z;
	float dy = y1 - y0;
	float dz = z1 - z0;

	float A = (dy * dy) / (ay * ay) + (dz * dz) / (az * az);
	float B = 2.0f * (y0 * dy / (ay * ay) + z0 * dz / (az * az));

	auto EvalF = [&](float t) -> float {
		float y = y0 + dy * t;
		float z = z0 + dz * t;
		return (y * y) / (ay * ay) + (z * z) / (az * az);
	};

	float tStar = tMin;
	if (A > 1e-12f) {
		tStar = -B / (2.0f * A);
		if (tStar < tMin)
			tStar = tMin;
		if (tStar > tMax)
			tStar = tMax;
	}

	float fMin = (std::min)({EvalF(tMin), EvalF(tMax), EvalF(tStar)});

	if (fMin > 1.0f) {
		wasInside_ = false;
		return;
	}

	// ここまで来たら「このフレームで穴を通過した」＝inside扱い
	bool isInsideNow = true;

	//==================================================
	// 7) 侵入瞬間だけ：軸方向に矯正＋最低速度保証
	//==================================================
	if (isInsideNow && !wasInside_) {

		// 1) リング軸（ワールド）を作る
		XMMATRIX Ronly = XMMatrixRotationRollPitchYaw(owner_->transform.rotate.x, owner_->transform.rotate.y, owner_->transform.rotate.z);

		// ★あなたが貼ったのと同じ（local Z）
		XMVECTOR axisLocal = XMVectorSet(0, 0, 1, 0);
		XMVECTOR axisWorldV = XMVector3Normalize(XMVector3TransformNormal(axisLocal, Ronly));

		Engine::Vector3 axis = {XMVectorGetX(axisWorldV), XMVectorGetY(axisWorldV), XMVectorGetZ(axisWorldV)};

		// 2) 速度分解
		Engine::Vector3 v = player->GetVelocity();
		float dotV = v.x * axis.x + v.y * axis.y + v.z * axis.z;

		Engine::Vector3 vParallel = {axis.x * dotV, axis.y * dotV, axis.z * dotV};
		Engine::Vector3 vLateral = {v.x - vParallel.x, v.y - vParallel.y, v.z - vParallel.z};

		// 3) 横成分減衰
		const float lateralKill = 14.0f; // 8～20
		float k = 1.0f - lateralKill * dt;
		if (k < 0.0f)
			k = 0.0f;

		vLateral.x *= k;
		vLateral.y *= k;
		vLateral.z *= k;

		// 4) 軸方向最低速度保証
		float sign = (dotV >= 0.0f) ? 1.0f : -1.0f;
		const float minForward = boostSpeed_;

		if (std::abs(dotV) < minForward) {
			dotV = minForward * sign;
			vParallel = {axis.x * dotV, axis.y * dotV, axis.z * dotV};
		}

		// 5) 合成
		v.x = vParallel.x + vLateral.x;
		v.y = vParallel.y + vLateral.y;
		v.z = vParallel.z + vLateral.z;

		player->SetVelocity(v);

		// ★追加: プレイヤーの演出を発動
		player->OnBoost();

		// ★追加: ブースト音再生
		Engine::Audio::GetInstance()->Play(seBoostHandle_);

		// 侵入時クールダウン
		cooldownTimer_ = cooldownSec_;
	}

	// inside 更新
	wasInside_ = isInsideNow;
}

void BoostRingGimmick::OnInspectorGUI() {

	ImGui::Text("BoostRing Trigger");
	ImGui::DragFloat("Inner Radius (model)", &innerRadius_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Thickness", &thickness_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Margin", &margin_, 0.01f, 0.0f, 1.0f);

	ImGui::Separator();

	ImGui::DragFloat("Radius", &radius_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Boost Speed (min)", &boostSpeed_, 0.01f, 0.0f, 50.0f);
	ImGui::DragFloat("Cooldown Sec", &cooldownSec_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Height Tolerance", &heightTolerance_, 0.01f, 0.0f, 5.0f);
	ImGui::Text("Effective ay=%.3f az=%.3f", innerRadius_ * std::abs(owner_->transform.scale.y) - margin_, innerRadius_ * std::abs(owner_->transform.scale.z) - margin_);
}

// --------------------------------------------------------
// 保存処理: Inspectorで調整した値を文字列化して保存
// 順番は LoadParameter と完全一致させること！
// --------------------------------------------------------
std::string BoostRingGimmick::SaveParameter() {
	std::stringstream ss;

	// ★保存順（変えないこと）
	// innerRadius, thickness, margin, radius, boostSpeed, cooldownSec, heightTolerance
	ss << innerRadius_ << "," << thickness_ << "," << margin_ << "," << radius_ << "," << boostSpeed_ << "," << cooldownSec_ << "," << heightTolerance_;

	return ss.str();
}

// --------------------------------------------------------
// 読み込み処理: 保存文字列を分解して復元
// --------------------------------------------------------
void BoostRingGimmick::LoadParameter(const std::string& param) {
	if (param.empty())
		return;

	std::stringstream ss(param);
	std::string seg;
	std::vector<std::string> a;

	while (std::getline(ss, seg, ',')) {
		a.push_back(seg);
	}

	// 7要素: innerRadius, thickness, margin, radius, boostSpeed, cooldownSec, heightTolerance
	if (a.size() >= 7) {
		try {
			innerRadius_ = std::stof(a[0]);
			thickness_ = std::stof(a[1]);
			margin_ = std::stof(a[2]);
			radius_ = std::stof(a[3]);
			boostSpeed_ = std::stof(a[4]);
			cooldownSec_ = std::stof(a[5]);
			heightTolerance_ = std::stof(a[6]);

			// ★軽い安全クランプ（任意だけどおすすめ）
			if (innerRadius_ < 0.0f)
				innerRadius_ = 0.0f;
			if (thickness_ < 0.0f)
				thickness_ = 0.0f;
			if (margin_ < 0.0f)
				margin_ = 0.0f;
			if (radius_ < 0.0f)
				radius_ = 0.0f;

			if (boostSpeed_ < 0.0f)
				boostSpeed_ = 0.0f;
			if (cooldownSec_ < 0.0f)
				cooldownSec_ = 0.0f;
			if (heightTolerance_ < 0.0f)
				heightTolerance_ = 0.0f;

		} catch (...) {
			// 壊れたデータなら何もしない（デフォルト値のまま）
		}
	}
}

} // namespace Game
