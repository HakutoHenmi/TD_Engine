#include "MovingLiftGimmick.h"
#include "../Actors/PlayerBall.h"
#include "GimmickFactory.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>
#include <string>

namespace Game {

// ★魔法の1行：エディタに追加
static GimmickRegistrar<MovingLiftGimmick> registrar("MovingLift");

// ------------------------------------------------------------
// easeInOut（smootherstep）: 0→1 が「端で速度0」になる
// t: 0..1
// ------------------------------------------------------------
static float EaseInOut_(float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	// smootherstep: 6t^5 - 15t^4 + 10t^3（端で速度0、より滑らか）
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// ------------------------------------------------------------
// 2点往復の進行度（0..1..0）を「折り返し速度0」で作る
// normalized: 0..1（周期内）
// 戻りは 1 - ease(u) を使うことで端で速度0になる
// ------------------------------------------------------------
static float PingPongEase01_(float normalized01) {
	normalized01 = normalized01 - std::floor(normalized01); // 0..1 に正規化

	if (normalized01 < 0.5f) {
		// 行き：0..0.5 => u 0..1 => 0..1
		float u = normalized01 * 2.0f;
		return EaseInOut_(u);
	} else {
		// 帰り：0.5..1 => u 0..1 => 1..0
		float u = (normalized01 - 0.5f) * 2.0f;
		return 1.0f - EaseInOut_(u);
	}
}

void MovingLiftGimmick::Start(Engine::GameObject* owner) {
	GimmickBase::Start(owner);

	if (owner_ && !hasBasePos_) {
		basePos_ = owner_->transform.translate; // ★配置初期位置
		hasBasePos_ = true;

		origin_ = basePos_;
		prevPos_ = basePos_;
		timer_ = 0.0f;
	}
}


void MovingLiftGimmick::OnCollision(void* other) {
	// 衝突相手が PlayerBall なら運搬対象にする
	PlayerBall* player = static_cast<PlayerBall*>(other);
	if (player) {
		lastPlayer_ = player;
		touchedThisFrame_ = true; // このフレーム乗ってる
	}
}

void MovingLiftGimmick::Update() {
#ifdef _DEBUG
	// デバッグ時
#else
	// リリース時



	if (!owner_) {
		return;
	}

	// -----------------------------
	// 速度→周期へ変換（units/sec を使う定番設計）
	// 片道距離Dを「速度V」で進む → 片道時間 = D/V
	// 往復周期 = 2 * D/V
	// -----------------------------
	float D = (std::max)(0.0f, moveDistance_);
	float V = (std::max)(0.01f, moveSpeed_);
	float oneWayTime = (D > 0.0f) ? (D / V) : 0.0001f;
	float period = (std::max)(0.0001f, oneWayTime * 2.0f);

	// 60fps前提じゃなく「dt」を取りたいなら、プロジェクトのdeltaTimeを使ってここを差し替え
	float dt = 1.0f / 60.0f;
	timer_ += dt;

	// 周期内の 0..1
	float normalized = timer_ / period;

	// 0..1..0（端で速度0）
	float a = PingPongEase01_(normalized);

	// -----------------------------
	// 軸と「配置位置がどっち端か」に応じて移動方向を決定
	// 仕様:
	//  Vertical:
	//    anchor=Min (Bottom) => 上へ動く
	//    anchor=Max (Top)    => 下へ動く
	//  Horizontal:
	//    anchor=Min (Left)   => 右へ動く
	//    anchor=Max (Right)  => 左へ動く
	// -----------------------------
	Engine::Vector3 dir = {0, 0, 0};

	if (axis_ == LiftAxis::Vertical) {
		// Min=Bottom -> +Y, Max=Top -> -Y
		dir.y = (anchor_ == LiftAnchor::Min) ? +1.0f : -1.0f;
	} else if (axis_ == LiftAxis::Horizontal) {
		// Min=Left -> +X, Max=Right -> -X
		dir.x = (anchor_ == LiftAnchor::Min) ? +1.0f : -1.0f;
	} else { // Depth
		// Min=Front(手前) -> 奥へ, Max=Back(奥) -> 手前へ
		// ★注意：座標系で逆なら符号を反転してOK
		dir.z = (anchor_ == LiftAnchor::Min) ? +1.0f : -1.0f;
	}

	// 新しいリフト位置
	Engine::Vector3 newPos = origin_;
	newPos.x += dir.x * D * a;
	newPos.y += dir.y * D * a;
	newPos.z += dir.z * D * a;

	// リフトの移動差分（プレイヤー運搬用）
	Engine::Vector3 delta = {newPos.x - prevPos_.x, newPos.y - prevPos_.y, newPos.z - prevPos_.z};

	// リフト本体を移動
	owner_->transform.translate = newPos;

	// -----------------------------
	// プレイヤー運搬（追従）
	// 「このフレーム乗ってる」なら、リフトの移動量だけプレイヤーも動かす
	// -----------------------------
	if (touchedThisFrame_ && lastPlayer_) {

		// ★ここはプロジェクトの PlayerBall API に合わせて調整する場所
		// 1) SetPosition があるならそれが一番安定
		// 2) 無いなら transform_.translate に直接足す
		// 3) 速度に足す方式は、衝突解決の影響でズレやすいので非推奨

		Engine::Vector3 p = lastPlayer_->GetPosition();

		p.x += delta.x;
		p.y += delta.y;
		p.z += delta.z;

		// もし SetPosition が無い場合:
		// lastPlayer_->transform_.translate = p; みたいに直接書き換えるか
		// PlayerBall に SetPosition を追加してね
		lastPlayer_->SetPosition(p);
	}

	// 次フレームへ
	prevPos_ = newPos;

	// フレーム終わりに「接触フラグ」を戻す
	touchedThisFrame_ = false;
	// lastPlayer_ は「次フレームも触れてたら更新される」ので保持でOK
#endif

}

//void MovingLiftGimmick::SnapToBaseForSave() {
//	if (!owner_)
//		return;
//	if (!hasBasePos_) {
//		basePos_ = owner_->transform.translate;
//		hasBasePos_ = true;
//	}
//	owner_->transform.translate = basePos_;
//	origin_ = basePos_;
//	prevPos_ = basePos_;
//	timer_ = 0.0f;
//}


void MovingLiftGimmick::OnInspectorGUI() {
	// Axis（★3種類に増やす）
	const char* axisItems[] = {
	    "Vertical (Up/Down)", "Horizontal (Left/Right)",
	    "Depth (Front/Back)" // ★追加
	};
	int axis = (int)axis_;
	if (ImGui::Combo("Axis", &axis, axisItems, 3)) { // ★2 → 3
		axis_ = (LiftAxis)axis;
	}

	// Anchor（★軸に応じて表示名を変える：3分岐）
	if (axis_ == LiftAxis::Vertical) {
		const char* aItems[] = {"Bottom (placed at bottom)", "Top (placed at top)"};
		int a = (int)anchor_;
		if (ImGui::Combo("Anchor", &a, aItems, 2)) {
			anchor_ = (LiftAnchor)a;
		}
	} else if (axis_ == LiftAxis::Horizontal) {
		const char* aItems[] = {"Left (placed at left)", "Right (placed at right)"};
		int a = (int)anchor_;
		if (ImGui::Combo("Anchor", &a, aItems, 2)) {
			anchor_ = (LiftAnchor)a;
		}
	} else { // ★axis_ == LiftAxis::Depth
		const char* aItems[] = {
		    "Front (placed at front)", // 手前端に置いた
		    "Back (placed at back)"    // 奥端に置いた
		};
		int a = (int)anchor_;
		if (ImGui::Combo("Anchor", &a, aItems, 2)) {
			anchor_ = (LiftAnchor)a;
		}
	}

	ImGui::DragFloat("Move Distance", &moveDistance_, 0.1f, 0.0f, 200.0f);
	ImGui::DragFloat("Move Speed (units/sec)", &moveSpeed_, 0.1f, 0.01f, 200.0f);

	if (ImGui::Button("Capture Current As Base")) {
		if (owner_) {
			basePos_ = owner_->transform.translate;
			hasBasePos_ = true;
			origin_ = basePos_;
			prevPos_ = basePos_;
			timer_ = 0.0f; // 動きをリセット
		}
	}

	if (ImGui::Button("Snap To Base")) {
		if (owner_ && hasBasePos_) {
			owner_->transform.translate = basePos_;
			origin_ = basePos_;
			prevPos_ = basePos_;
			timer_ = 0.0f;
		}
	}

	// ★基準座標表示（編集不可の表示）
	ImGui::Separator();
	ImGui::Text("Base Position (saved anchor):");
	ImGui::Text("  %.3f, %.3f, %.3f", basePos_.x, basePos_.y, basePos_.z);

	// ついでに現在座標も出すとズレが一瞬で分かる
	if (owner_) {
		ImGui::Text("Current Position:");
		ImGui::Text("  %.3f, %.3f, %.3f", owner_->transform.translate.x, owner_->transform.translate.y, owner_->transform.translate.z);
	}

	// デバッグ表示
	ImGui::Text("time=%.2f", timer_);
}

// --------------------------------------------------------
// 保存処理: Inspectorで調整した値をカンマ区切りで保存
// 順番は LoadParameter と完全一致させること！
// --------------------------------------------------------
std::string MovingLiftGimmick::SaveParameter() {
	std::stringstream ss;

	// ★保存順（変更しないこと）
	// axis, anchor, distance, speed
	ss << (int)axis_ << "," << (int)anchor_ << "," << moveDistance_ << "," << moveSpeed_ << "," << basePos_.x << "," << basePos_.y << "," << basePos_.z;

	return ss.str();
}

// --------------------------------------------------------
// 読み込み処理: 保存文字列を分解して復元
// --------------------------------------------------------
void MovingLiftGimmick::LoadParameter(const std::string& param) {
	if (param.empty())
		return;

	std::stringstream ss(param);
	std::string segment;
	std::vector<std::string> seglist;

	while (std::getline(ss, segment, ',')) {
		seglist.push_back(segment);
	}

	// axis, anchor, distance, speed の4つ
	if (seglist.size() >= 7) {
		try {
			axis_ = (LiftAxis)std::stoi(seglist[0]);
			anchor_ = (LiftAnchor)std::stoi(seglist[1]);
			moveDistance_ = std::stof(seglist[2]);
			moveSpeed_ = std::stof(seglist[3]);
			basePos_.x = std::stof(seglist[4]);
			basePos_.y = std::stof(seglist[5]);
			basePos_.z = std::stof(seglist[6]);
			hasBasePos_ = true;

			// 安全のためのクランプ（任意だけどおすすめ）
			if (moveDistance_ < 0.0f)
				moveDistance_ = 0.0f;
			if (moveSpeed_ < 0.01f)
				moveSpeed_ = 0.01f;

			 // ★もし owner_ がもう有効なら、この場で基準に戻しておく（呼ばれる順序対策）
			if (owner_) {
				owner_->transform.translate = basePos_;
			}

		} catch (...) {
			// 壊れたデータなら初期値のまま
		}
	}
}

//void MovingLiftGimmick::OnBeforeSave() {
//	if (!owner_)
//		return;
//
//	// basePos_ が未確定なら「初期位置」を確定したいが、
//	// Save時点の位置を採用するとズレの元。
//	// なので：少なくとも owner_->transform は書き換えない。
//	if (!hasBasePos_) {
//		// ここは本当は「Start時点の位置」を入れるのが理想
//		// ただし現状最小修正としては、ここで確定しても transform を変えないだけで事故は激減する
//		basePos_ = owner_->transform.translate;
//		hasBasePos_ = true;
//	}
//
//	// ★重要：下の3行は全部消す（Saveのために動作状態を壊さない）
//	// owner_->transform.translate = basePos_;
//	// origin_ = basePos_;
//	// prevPos_ = basePos_;
//	// timer_ = 0.0f;
//}


} // namespace Game
