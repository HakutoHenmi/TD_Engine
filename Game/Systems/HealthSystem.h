#pragma once
#include "ISystem.h"
#include <unordered_map>

namespace Game {

class HealthSystem : public ISystem {
public:
	void Update(std::vector<SceneObject>& objects, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		for (auto& obj : objects) {
			// Healthコンポーネントを持たないオブジェクトはスキップ
			if (obj.healths.empty()) continue;

			bool isInvincible = false;
			for (auto& hc : obj.healths) {
				if (!hc.enabled || hc.isDead) continue;

				if (hc.invincibleTime > 0.0f) {
					hc.invincibleTime -= ctx.dt;
					if (hc.invincibleTime < 0.0f) hc.invincibleTime = 0.0f;
					if (hc.invincibleTime > 0.0f) isInvincible = true;
				}

				// ダメージ検知用の簡易ロジック
				static std::unordered_map<uint32_t, float> lastHp;
				if (lastHp.find(obj.id) != lastHp.end()) {
					float diff = lastHp[obj.id] - hc.hp;
					if (diff > 0.1f) {
						bool showDmg = true;
						if (!obj.worldSpaceUIs.empty() && !obj.worldSpaceUIs[0].showDamageNumbers) {
							showDmg = false;
						}

						if (showDmg) {
							// ダメージポップアップ用の変数をセット
							obj.SetString("damage_text", std::to_string((int)diff));
							obj.SetVariable("damage_timer", 1.0f); // 1秒表示
						}
					}
				}
				lastHp[obj.id] = hc.hp;

				if (hc.hp <= 0.0f && !hc.isDead) {
					hc.isDead = true;
				}
			}

			// 被弾リアクション: 無敵時間中のみ色を変更、それ以外は元の色に戻す
			if (isInvincible) {
				// 元の色を保存（まだ保存していない場合）
				if (originalColors_.find(obj.id) == originalColors_.end()) {
					std::vector<DirectX::XMFLOAT4> colors;
					for (const auto& mr : obj.meshRenderers) {
						colors.push_back(mr.color);
					}
					originalColors_[obj.id] = colors;
				}
				// 被弾色に変更
				for (auto& mr : obj.meshRenderers) {
					mr.color = {1.0f, 0.2f, 0.2f, 1.0f};
				}
			} else {
				// 元の色を復元
				auto it = originalColors_.find(obj.id);
				if (it != originalColors_.end()) {
					for (size_t i = 0; i < obj.meshRenderers.size() && i < it->second.size(); ++i) {
						obj.meshRenderers[i].color = it->second[i];
					}
					originalColors_.erase(it);
				}
			}
		}
	}

	void Reset(std::vector<SceneObject>& objects) override {
		// 元の色を復元してからクリア
		for (auto& obj : objects) {
			auto it = originalColors_.find(obj.id);
			if (it != originalColors_.end()) {
				for (size_t i = 0; i < obj.meshRenderers.size() && i < it->second.size(); ++i) {
					obj.meshRenderers[i].color = it->second[i];
				}
			}
			for (auto& hc : obj.healths) {
				hc.invincibleTime = 0.0f;
				hc.isDead = false;
				if (hc.hp <= 0) hc.hp = hc.maxHp;
			}
		}
		originalColors_.clear();
	}

private:
	// オブジェクトIDごとに元のMeshRendererのcolorを保存
	std::unordered_map<uint32_t, std::vector<DirectX::XMFLOAT4>> originalColors_;
};

} // namespace Game
