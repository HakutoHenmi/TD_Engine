#include "TimedBlock.h"
#include "../actors/PlayerBall.h"
#include "GimmickFactory.h"
#include "imgui.h"
#include <cmath>
#include <type_traits>
#include <sstream>
#include <vector>
#include <string>

namespace Game {

static GimmickRegistrar<TimedBlock> registrar("TimedBlock");

static float Frac_(float x) { return x - std::floor(x); }

//------------------------------------------------------------
// ここから：ON/OFF を “存在するAPIだけ” 呼ぶためのヘルパ（SFINAE）
//------------------------------------------------------------
namespace detail {

// ---- SetVisible(bool) ----
template<class T, class = void> struct has_SetVisible : std::false_type {};
template<class T> struct has_SetVisible<T, std::void_t<decltype(std::declval<T&>().SetVisible(true))>> : std::true_type {};

// ---- SetActive(bool) ----
template<class T, class = void> struct has_SetActive : std::false_type {};
template<class T> struct has_SetActive<T, std::void_t<decltype(std::declval<T&>().SetActive(true))>> : std::true_type {};

// ---- renderer.enabled = bool ----
template<class T, class = void> struct has_renderer_enabled : std::false_type {};
template<class T> struct has_renderer_enabled<T, std::void_t<decltype(std::declval<T&>().renderer.enabled)>> : std::true_type {};

// ---- renderer_.enabled = bool ----
template<class T, class = void> struct has_renderer__enabled : std::false_type {};
template<class T> struct has_renderer__enabled<T, std::void_t<decltype(std::declval<T&>().renderer_.enabled)>> : std::true_type {};

// ---- collider.enabled = bool ----
template<class T, class = void> struct has_collider_enabled : std::false_type {};
template<class T> struct has_collider_enabled<T, std::void_t<decltype(std::declval<T&>().collider.enabled)>> : std::true_type {};

// ---- collider_.enabled = bool ----
template<class T, class = void> struct has_collider__enabled : std::false_type {};
template<class T> struct has_collider__enabled<T, std::void_t<decltype(std::declval<T&>().collider_.enabled)>> : std::true_type {};

// ---- SetCollisionEnabled(bool) ----
template<class T, class = void> struct has_SetCollisionEnabled : std::false_type {};
template<class T> struct has_SetCollisionEnabled<T, std::void_t<decltype(std::declval<T&>().SetCollisionEnabled(true))>> : std::true_type {};

// ---- SetColliderEnabled(bool) ----
template<class T, class = void> struct has_SetColliderEnabled : std::false_type {};
template<class T> struct has_SetColliderEnabled<T, std::void_t<decltype(std::declval<T&>().SetColliderEnabled(true))>> : std::true_type {};

template<class T> static void TrySetVisible(T& obj, bool on) {
	if constexpr (has_SetVisible<T>::value) {
		obj.SetVisible(on);
	} else if constexpr (has_renderer_enabled<T>::value) {
		obj.renderer.enabled = on;
	} else if constexpr (has_renderer__enabled<T>::value) {
		obj.renderer_.enabled = on;
	} else if constexpr (has_SetActive<T>::value) {
		// 最終手段：SetActiveがある場合は「全体」を止める可能性があるので注意
		// ただし、表示も当たりもまとめて止めたい設計ならこれが正しい
		obj.SetActive(on);
	} else {
		// 何もしない（APIが見つからない）
	}
}

template<class T> static void TrySetCollision(T& obj, bool on) {
	if constexpr (has_SetCollisionEnabled<T>::value) {
		obj.SetCollisionEnabled(on);
	} else if constexpr (has_SetColliderEnabled<T>::value) {
		obj.SetColliderEnabled(on);
	} else if constexpr (has_collider_enabled<T>::value) {
		obj.collider.enabled = on;
	} else if constexpr (has_collider__enabled<T>::value) {
		obj.collider_.enabled = on;
	} else {
		// 何もしない（APIが見つからない）
	}
}

} // namespace detail
//------------------------------------------------------------

void TimedBlock::Start(Engine::GameObject* owner) {
	GimmickBase::Start(owner);

	if (periodSec_ < 0.01f)
		periodSec_ = 0.01f;
	if (duty01_ < 0.0f)
		duty01_ = 0.0f;
	if (duty01_ > 1.0f)
		duty01_ = 1.0f;
	phase01_ = Frac_(phase01_);

	// ★初期状態を startOn_ で決める
	if (startOn_) {
		timeSec_ = 0.0f; // u = phase
		active_ = IsActive_(timeSec_);
	} else {
		// 「必ずOFFから始める」ために、u が duty 以上になる位置へ飛ばす
		// u = Frac(time/period + phase). これを duty 以上にしたい
		const float uOff = (duty01_ + 0.001f); // dutyぴったり回避
		const float t = (uOff - phase01_) * periodSec_;
		timeSec_ = (t < 0.0f) ? (t + periodSec_) : t;
		active_ = false; // 明示的にOFF
	}

	// ★初期反映（表示/当たり）
	if (owner_) {
		owner_->isVisible = active_;
		detail::TrySetVisible(*owner_, active_);
		detail::TrySetCollision(*owner_, active_);
	}
}


bool TimedBlock::IsActive_(float globalTimeSec) const {
	float u = Frac_(globalTimeSec / periodSec_ + phase01_);
	return (u < duty01_);
}

void TimedBlock::Update() {
	if (!owner_)
		return;

	const float dt = 1.0f / 60.0f;
	timeSec_ += dt;

	active_ = IsActive_(timeSec_);

	// ★見た目は常に描く（半透明にしたいから）
	owner_->isVisible = true;
	detail::TrySetVisible(*owner_, true);

	// ★当たりだけ切る
	detail::TrySetCollision(*owner_, active_);

	// ★OFFは半透明（ここはあなたの GameObject の色に合わせて1行調整）
	// 例：owner_->color.w / owner_->material.color.w / owner_->renderer.color.w など
	const float a = active_ ? 1.0f : 0.35f;
	owner_->color.w = a; // ←ここだけ、あなたのエンジンの色変数に合わせて差し替え
}



void TimedBlock::OnCollision(void* other) {
	if (!active_) {
		return;
	}

	PlayerBall* player = static_cast<PlayerBall*>(other);
	if (!player)
		return;

	const Engine::Vector3 p = player->GetPosition();
	const float pr = player->GetRadius();

	if (!IsOnTop_(p, pr))
		return;

	// 足場として乗せる（吹き飛び防止）
	const Engine::Vector3 pos = owner_->transform.translate;
	const Engine::Vector3 half = owner_->transform.scale; // 半サイズ想定
	const float topY = pos.y + half.y;

	Engine::Vector3 pp = p;
	pp.y = topY + pr;
	player->SetPosition(pp);

	Engine::Vector3 v = player->GetVelocity();
	if (v.y < 0.0f)
		v.y = 0.0f;
	player->SetVelocity(v);
	player->SetGrounded(true);
}

bool TimedBlock::IsOnTop_(const Engine::Vector3& p, float pr) const {
	if (!owner_)
		return false;

	// owner_->transform をブロックの中心＆半サイズとして扱う
	const Engine::Vector3 pos = owner_->transform.translate;
	const Engine::Vector3 half = owner_->transform.scale; // 半サイズ想定（元コード準拠）

	const float hx = half.x;
	const float hy = half.y;
	const float hz = half.z;

	if (p.x < pos.x - hx - pr || p.x > pos.x + hx + pr)
		return false;
	if (p.z < pos.z - hz - pr || p.z > pos.z + hz + pr)
		return false;

	const float topY = pos.y + hy;
	const float pBottom = p.y - pr;
	const float dy = pBottom - topY;

	return (dy <= topTolUp_ && dy >= -topTolDown_);
}


void TimedBlock::OnInspectorGUI() {

	// ★StartOn を「変更した瞬間に反映」させる
	bool prevStartOn = startOn_;
	ImGui::Checkbox("Start On", &startOn_);
	if (startOn_ != prevStartOn && owner_) {
		// Start() と同じロジックで timeSec_/active_ を作り直す
		if (periodSec_ < 0.01f)
			periodSec_ = 0.01f;
		if (duty01_ < 0.0f)
			duty01_ = 0.0f;
		if (duty01_ > 1.0f)
			duty01_ = 1.0f;
		phase01_ = Frac_(phase01_);

		if (startOn_) {
			timeSec_ = 0.0f;
			active_ = IsActive_(timeSec_);
		} else {
			// ★Duty=1.0 は「絶対OFFが存在しない」ので強制的に見えなくするだけにする
			if (duty01_ >= 1.0f) {
				active_ = false;
			} else {
				const float uOff = (duty01_ + 0.001f);
				float t = (uOff - phase01_) * periodSec_;
				if (t < 0.0f)
					t += periodSec_;
				timeSec_ = t;
				active_ = false;
			}
		}

		owner_->isVisible = active_;
		detail::TrySetVisible(*owner_, active_);
		detail::TrySetCollision(*owner_, active_);
	}

	// ---- 以降は元のまま ----
	ImGui::DragFloat("Period (sec)", &periodSec_, 0.05f, 0.01f, 60.0f);
	ImGui::DragFloat("Duty (0..1)", &duty01_, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Phase (0..1)", &phase01_, 0.01f, 0.0f, 1.0f);

	ImGui::Separator();
	ImGui::DragFloat("Top Tol Up", &topTolUp_, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Top Tol Down", &topTolDown_, 0.01f, 0.0f, 1.0f);

	ImGui::Text("Active: %s", active_ ? "YES" : "NO");
}

// --------------------------------------------------------
// 保存処理: 変数をカンマ区切りの文字列にする
// ★重要: LoadParameter と同じ順番にすること
// --------------------------------------------------------
std::string TimedBlock::SaveParameter() {
	std::stringstream ss;

	// ★保存したい“調整パラメータ”だけ保存する（timeSec_ や active_ などの実行時状態は保存しない）
	// 順番:
	// periodSec_, duty01_, phase01_, startOn_, topTolUp_, topTolDown_
	ss << periodSec_ << "," << duty01_ << "," << phase01_ << "," << (startOn_ ? 1 : 0) << "," << topTolUp_ << "," << topTolDown_;

	return ss.str();
}

// --------------------------------------------------------
// 読み込み処理: 文字列を分解して変数に戻す
// ★重要: SaveParameter と同じ順番で取り出すこと
// --------------------------------------------------------
void TimedBlock::LoadParameter(const std::string& param) {
	if (param.empty())
		return;

	std::stringstream ss(param);
	std::string segment;
	std::vector<std::string> seglist;

	while (std::getline(ss, segment, ',')) {
		seglist.push_back(segment);
	}

	// 今回は 6個
	if (seglist.size() >= 6) {
		try {
			periodSec_ = std::stof(seglist[0]);
			duty01_ = std::stof(seglist[1]);
			phase01_ = std::stof(seglist[2]);
			startOn_ = (std::stoi(seglist[3]) != 0);
			topTolUp_ = std::stof(seglist[4]);
			topTolDown_ = std::stof(seglist[5]);
		} catch (...) {
			// 失敗したら何もしない（初期値のまま）
		}
	}

	// ---- 値の安全化（Startと同等）----
	if (periodSec_ < 0.01f)
		periodSec_ = 0.01f;
	if (duty01_ < 0.0f)
		duty01_ = 0.0f;
	if (duty01_ > 1.0f)
		duty01_ = 1.0f;
	phase01_ = Frac_(phase01_);

	// ---- 反映（StartOn を「ロード直後に」反映）----
	// Start() と同じロジックで timeSec_/active_ を作り直す
	if (startOn_) {
		timeSec_ = 0.0f;
		active_ = IsActive_(timeSec_);
	} else {
		if (duty01_ >= 1.0f) {
			active_ = false;
		} else {
			const float uOff = (duty01_ + 0.001f);
			float t = (uOff - phase01_) * periodSec_;
			if (t < 0.0f)
				t += periodSec_;
			timeSec_ = t;
			active_ = false;
		}
	}

	// 表示/当たりの初期反映（ロード直後に見た目がおかしくならないように）
	if (owner_) {
		// あなたの現状仕様：見た目は常に描く（Updateで isVisible=true にする）ので、ここは好みでOK
		// ただ “Startと同じ初期反映” に寄せるなら active_ を反映する
		owner_->isVisible = active_;
		detail::TrySetVisible(*owner_, active_);
		detail::TrySetCollision(*owner_, active_);

		// 透明度も初期反映しておく（Updateと一致）
		const float a = active_ ? 1.0f : 0.35f;
		owner_->color.w = a; // ←必要ならあなたの色変数に差し替え
	}
}



// 既存の IsOnTop_, OnCollision, OnInspectorGUI はそのままでOK
// （OnCollision 冒頭の if(!active_) return; が効く）

} // namespace Game
