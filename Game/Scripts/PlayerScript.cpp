#include "PlayerScript.h"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include "../Systems/UISystem.h"
#include "BulletScript.h"
#include "ExplosionAttackArea.h"
#include "MirrorShatterScript.h"
#include "ObjectTypes.h"
#include "PhaseSystemScript.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "TutorialScript.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace Game {

void PlayerScript::Start(entt::entity entity, GameScene* scene) {
	// ★追加: シーン開始時は必ずカーソルを表示する
	isCursorVisible_ = true;
	while (ShowCursor(TRUE) < 0)
		;
	prevCursorToggle_ = false;

	// ★初期位置を保存
	if (scene->GetRegistry().all_of<TransformComponent>(entity)) {
		initialPos_ = scene->GetRegistry().get<TransformComponent>(entity).translate;
	}

	// ★追加: プレイヤーの3Dモデルを GLTF/GLB (sotai3.glb) に置き換え
	auto* pRenderer = scene->GetRenderer();
	if (pRenderer) {
		auto& mr = scene->GetRegistry().get_or_emplace<MeshRendererComponent>(entity);
		mr.modelHandle = pRenderer->LoadObjMesh("Resources/Models/3Dmodel/player/sotai3.glb");
		mr.textureHandle = 0;                // ★修正: 0を指定してGLTF内包テクスチャを使用する
		mr.shaderName = "ToonSkinning";      // スキニング対応トゥーンシェーダー
		mr.color = {1.0f, 1.0f, 1.0f, 1.0f}; // ★修正: Player.prefabの青色設定をリセットし、本来のテクスチャカラーを表示する
		mr.enabled = true;
	}

	// ★追加: アニメーターコンポーネントをアタッチ
	auto& anim = scene->GetRegistry().get_or_emplace<AnimatorComponent>(entity);
	anim.currentAnimation = "[保留アクション]";
	anim.time = 0.0f;
	anim.speed = 1.0f;
	anim.isPlaying = true;
	anim.loop = true;
	anim.enabled = true;

	// ★追加: プレイヤーを物理演算から切り離し、CharacterController方式（レイキャスト）で制御する
	if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
		scene->GetRegistry().get<RigidbodyComponent>(entity).isKinematic = true;
	}
	if (scene->GetRegistry().all_of<CharacterMovementComponent>(entity)) {
		auto& cm = scene->GetRegistry().get<CharacterMovementComponent>(entity);
		cm.heightOffset = 0.0f;    // ★修正: 1.0fから0.0fへ変更。モデルの原点（足元）を地面に合わせるため
		cm.jumpPower = jumpPower_; // ★追加: ヘッダで定義されているジャンプ力(8.0f)を反映し、ジャンプを弱くする
		cm.gravity = 28.0f;        // ★キレのあるアクション用重力 (原神風)
		cm.speed = 8.5f;           // ★追加: プレイヤーの基本移動速度を上げる（デフォルトは5.0f）
	}

	// プレイヤー自身のコライダーサイズを「見た目（2m立方体）」に合わせる
	if (scene->GetRegistry().all_of<BoxColliderComponent>(entity)) {
		auto& bc = scene->GetRegistry().get<BoxColliderComponent>(entity);
		bc.size = {2.0f, 2.0f, 2.0f};
		bc.center = {0.0f, 1.0f, 0.0f}; // ★追加: 足元原点に対応するため上へずらす
	}

	// 食らい判定（Hurtbox）がなければ追加し、サイズとタグを設定する
	if (!scene->GetRegistry().all_of<HurtboxComponent>(entity)) {
		auto& hb = scene->GetRegistry().emplace<HurtboxComponent>(entity);
		hb.size = {2.0f, 2.0f, 2.0f};
		hb.center = {0.0f, 1.0f, 0.0f}; // ★追加: 足元原点対応
		hb.tag = TagType::Player;
	} else {
		auto& hb = scene->GetRegistry().get<HurtboxComponent>(entity);
		hb.size = {2.0f, 2.0f, 2.0f};
		hb.center = {0.0f, 1.0f, 0.0f}; // ★追加: 足元原点対応
		hb.tag = TagType::Player;
	}

	// 体力（Health）がなければ追加する
	if (!scene->GetRegistry().all_of<HealthComponent>(entity)) {
		auto& hc = scene->GetRegistry().emplace<HealthComponent>(entity);
		hc.hp = 100.0f;
		hc.maxHp = 100.0f;
	}

	// ---- 剣（大剣）の初期化 ----
	entt::entity sword = scene->FindObjectByName(swordName_);
	if (sword == entt::null) {
		sword = scene->GetRegistry().create();
		scene->GetRegistry().emplace<NameComponent>(sword).name = swordName_;
	}

	// 必須コンポーネントを確実に付与・更新
	auto& sTc = scene->GetRegistry().get_or_emplace<TransformComponent>(sword);
	sTc.scale = {1.0f, 1.0f, 1.0f}; // 剣サイズ

	scene->SetParent(sword, entity); // ★修正: SetParent経由で親子関係を構築

	auto& sTag = scene->GetRegistry().get_or_emplace<TagComponent>(sword);
	sTag.tag = TagType::PlayerSword;

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		auto& sMr = scene->GetRegistry().get_or_emplace<MeshRendererComponent>(sword);
		sMr.modelHandle = renderer->LoadObjMesh("Resources/Models/3Dmodel/sword/ken.obj");
		sMr.textureHandle = renderer->LoadTexture2D("Resources/Models/3Dmodel/sword/ken_tex.png"); // ★直接テクスチャをロードして適用
		sMr.shaderName = "Toon";                                                                   // ★デフォルトからToonシェーダーに変更して輪郭と陰影をくっきり見やすく！
		sMr.color = {1.2f, 1.2f, 1.2f, 1.0f};                                                      // ★少し輝度をブーストして暗いステージでも存在感を出す
		sMr.enabled = true;
	}

	auto& sMotion = scene->GetRegistry().get_or_emplace<MotionComponent>(sword);
	// 大剣用コンボモーションの定義
	auto setupGreatswordCombo = [&](const std::string& name, int index) {
		MotionComponent::MotionClip clip;
		clip.name = name;
		clip.loop = false;
		float bsz = 1.3f; // スケールアップに合わせて1.3に変更
		if (index == 1) { // 横薙ぎ
			clip.totalDuration = 0.7f;
			clip.keyframes.push_back({
			    0.00f, {0.6f, 1.8f, 0.4f  },
                 {0.0f, 0.0f, -1.57f},
                 {1.3f, 1.3f, bsz   }
            }); // 始動: 真右(Yaw=0.0f)
			clip.keyframes.push_back({
			    0.25f, {0.7f, 1.8f,   0.6f      },
                 {0.0f, -0.78f, -1.57f    },
                 {1.3f, 1.3f,   bsz + 0.5f}
            }); // 右前(Yaw=-0.78f)
			clip.keyframes.push_back({
			    0.45f, {0.0f, 1.8f,   1.2f      },
                 {0.0f, -1.57f, -1.57f    },
                 {1.3f, 1.3f,   bsz + 1.2f}
            }); // 真前を通る(Yaw=-1.57f)
			clip.keyframes.push_back({
			    0.70f, {-0.6f, 1.8f,   0.4f  },
                 {0.0f,  -3.14f, -1.57f},
                 {1.3f,  1.3f,   bsz   }
            }); // 真左で終了(Yaw=-3.14f)、正面を通る右から左の180度に修正
		} else if (index == 2) {                                                         // 振り下ろし
			clip.totalDuration = 0.8f;
			clip.keyframes.push_back({
			    0.00f, {0.0f,          4.5f, -1.0f},
                 {-1.5f + 1.57f, 0.0f, 0.0f },
                 {1.3f,          1.3f, bsz  }
            });
			clip.keyframes.push_back({
			    0.30f, {0.0f,          4.8f, -0.5f},
                 {-1.8f + 1.57f, 0.0f, 0.0f },
                 {1.3f,          1.3f, bsz  }
            });
			clip.keyframes.push_back({
			    0.50f, {0.0f,         1.5f, 3.5f      },
                 {0.5f + 1.57f, 0.0f, 0.0f      },
                 {1.3f,         1.3f, bsz + 1.5f}
            });
			clip.keyframes.push_back({
			    0.80f, {0.0f,         1.2f, 2.8f},
                 {0.8f + 1.57f, 0.0f, 0.0f},
                 {1.3f,         1.3f, bsz }
            });
		} else { // 飛翔大回転（前方に飛ばして戻す）
			clip.totalDuration = 1.2f;
			// 0.0s: 始動
			clip.keyframes.push_back({
			    0.00f, {0.0f, 2.2f, 0.5f },
                 {0.0f, 0.0f, 1.57f},
                 {1.3f, 1.3f, bsz  }
            });
			// 0.3s: 前方に射出開始 + 回転
			clip.keyframes.push_back({
			    0.30f, {0.0f, 2.2f,  4.5f      },
                 {0.0f, 6.28f, 1.57f     },
                 {1.3f, 1.3f,  bsz + 0.5f}
            });
			// 0.6s: 最遠地点で激しく回転 (Boomerang Peak)
			clip.keyframes.push_back({
			    0.60f, {0.0f, 2.2f,   7.5f      },
                 {0.0f, 12.56f, 1.57f     },
                 {1.3f, 1.3f,   bsz + 1.0f}
            });
			// 0.9s: 帰還開始
			clip.keyframes.push_back({
			    0.90f, {0.0f, 2.2f,   3.5f      },
                 {0.0f, 18.84f, 1.57f     },
                 {1.3f, 1.3f,   bsz + 0.5f}
            });
			// 1.2s: キャッチ
			clip.keyframes.push_back({
			    1.20f, {-1.0f, 1.8f,   -0.5f},
                 {0.0f,  20.41f, 1.57f},
                 {1.3f,  1.3f,   bsz  }
            });
		}
		sMotion.clips[name] = clip;
	};
	setupGreatswordCombo("Combo1", 1);
	setupGreatswordCombo("Combo2", 2);
	setupGreatswordCombo("Combo3", 3);

	// ★追加: 大剣の溜め攻撃モーション「スチーム・ダイブ・スラッシュ」
	if (auto* motion = scene->GetRegistry().try_get<MotionComponent>(sword)) {
		MotionComponent::MotionClip clip;
		clip.name = "SwordChargeAttack";
		clip.totalDuration = 1.1f;
		clip.loop = false;
		float bsz = 1.3f; // スケールアップに合わせて1.3に変更

		// 0.0s: 溜め・溜め開放（身構える）
		clip.keyframes.push_back({
		    0.00f, {0.0f,          1.5f, 0.0f},
             {-0.5f + 1.57f, 0.0f, 0.0f},
             {1.3f,          1.3f, bsz }
        });
		// 0.2s: 跳躍開始（少し上へ、少し前へ）
		clip.keyframes.push_back({
		    0.20f, {0.0f,          3.5f, 1.5f      },
             {-1.5f + 1.57f, 0.0f, 0.0f      },
             {1.3f,          1.3f, bsz + 0.5f}
        });
		// 0.45s: 空中ピーク（最大限に振りかぶる）
		clip.keyframes.push_back({
		    0.45f, {0.0f,          6.0f, 3.5f      },
             {-2.8f + 1.57f, 0.0f, 0.0f      },
             {1.3f,          1.3f, bsz + 1.0f}
        });
		// 0.7s: 叩きつけ（最速で地面へ、前方に大きくリーチ）
		clip.keyframes.push_back({
		    0.70f, {0.0f,         1.2f, 5.5f      },
             {0.8f + 1.57f, 0.0f, 0.0f      },
             {1.3f,         1.3f, bsz + 1.8f}
        });
		// 1.1s: 着地硬直
		clip.keyframes.push_back({
		    1.10f, {0.0f,         1.1f, 4.0f},
             {1.2f + 1.57f, 0.0f, 0.0f},
             {1.3f,         1.3f, bsz }
        });

		motion->clips[clip.name] = clip;
	}

	auto& sHb = scene->GetRegistry().get_or_emplace<HitboxComponent>(sword);
	sHb.damage = 35.0f; // ★弱体化: 110.0f -> 35.0f (ワンパン防止)
	sHb.tag = TagType::Sword;
	sHb.size = {6.0f, 4.0f, 2.5f};
	sHb.center = {0.0f, 1.2f, 1.2f}; // ★Y軸+1.2に変更
	sHb.enabled = true;
	if (!isAttacking_)
		sHb.isActive = false;

	// ---- 銃の初期化 ----
	entt::entity gun = scene->FindObjectByName(gunName_);
	if (gun == entt::null) {
		gun = scene->GetRegistry().create();
		scene->GetRegistry().emplace<NameComponent>(gun).name = gunName_;
	}
	scene->SetTag(gun, TagType::Player); // ★追加: RayCastの地形判定から除外するためプレイヤー属性を付与

	auto& gTc = scene->GetRegistry().get_or_emplace<TransformComponent>(gun);
	gTc.scale = {2.2f, 2.2f, 2.2f}; // ピストルを大きく表示

	scene->SetParent(gun, entity); // ★修正: SetParent経由で親子関係を構築

	if (renderer) {
		auto& gMr = scene->GetRegistry().get_or_emplace<MeshRendererComponent>(gun);
		gMr.modelHandle = renderer->LoadObjMesh("Resources/Models/3Dmodel/pistol/pistol.obj");
		gMr.textureHandle = renderer->LoadTexture2D("Resources/Models/3Dmodel/pistol/pistol.png");
		gMr.shaderName = "Toon";              // トゥーンシェーダーで輪郭と陰影をくっきり表示
		gMr.color = {1.0f, 1.0f, 1.0f, 1.0f}; // テクスチャ本来の色を表示するため白色に変更
		gMr.enabled = true;
	}

	if (scene->GetRegistry().all_of<NameComponent>(entity)) {
		std::cout << "PlayerScript Started: Greatsword & Gun Initialized.\n";
	}
}

void PlayerScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (scene->GetRegistry().all_of<HealthComponent>(entity)) {
		if (scene->GetRegistry().get<HealthComponent>(entity).isDead)
			return;
	}
	ApplySkillEffects(entity, scene);
	if (!isSubscribed_) {
		scene->GetEventSystem().Subscribe("GainExp", [this, scene](float amount) {
			experience_ += amount;
			if (experience_ >= nextExperience_) {
				experience_ -= nextExperience_;
				level_++;
				nextExperience_ *= 1.5f;
				scene->GetEventSystem().Emit("GainSkillPoint", 1.0f);
			}
			debugReceiveCount_ += 1;
			debugLastValue_ = amount;
		});
		scene->GetEventSystem().Subscribe("PlayerSwordHit", [this, scene](float) {
			if (!scene || !scene->GetContext().camera)
				return;
			auto* cam = scene->GetContext().camera;
			if (comboCount_ <= 1) {
				cam->StartShake(0.1f, 0.15f); // 弱
			} else if (comboCount_ == 2) {
				cam->StartShake(0.15f, 0.35f); // 中
			} else {
				cam->StartShake(0.2f, 0.6f); // 強
			}
		});
		// ★追加: 強化弾ヒットイベント → 鏡割れエフェクト生成
		scene->GetEventSystem().Subscribe("EnhancedBulletHit", [this, entity, scene](float entityVal) {
			entt::entity target = static_cast<entt::entity>(static_cast<uint32_t>(entityVal));
			if (scene->GetRegistry().valid(target) && scene->GetRegistry().all_of<TransformComponent>(target)) {
				auto& tc = scene->GetRegistry().get<TransformComponent>(target);
				DirectX::XMFLOAT3 hitPos = tc.translate;
				hitPos.y += 1.0f;
				// 新しい鏡割れエフェクト(Distortionベース)を生成
				entt::entity shatterVfx = scene->CreateEntity("MirrorShatter");
				auto& vfxTc = scene->GetRegistry().get<TransformComponent>(shatterVfx);
				vfxTc.translate = hitPos;
				// ★弾の飛来方向をVariableComponentで渡す（プレイヤー→敵）
				auto& vc = scene->GetRegistry().emplace<VariableComponent>(shatterVfx);
				if (scene->GetRegistry().all_of<TransformComponent>(entity)) {
					auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
					float dx = tc.translate.x - pTc.translate.x;
					float dy = (tc.translate.y + 1.0f) - (pTc.translate.y + 1.0f);
					float dz = tc.translate.z - pTc.translate.z;
					float len = std::sqrt(dx * dx + dy * dy + dz * dz);
					if (len > 0.01f) {
						dx /= len;
						dy /= len;
						dz /= len;
					}
					vc.SetValue("DirX", dx);
					vc.SetValue("DirY", dy);
					vc.SetValue("DirZ", dz);
				}
				auto& sc = scene->GetRegistry().emplace<ScriptComponent>(shatterVfx);
				sc.scripts.push_back({"MirrorShatterScript", "", nullptr});

				// ★追加: 爆発攻撃範囲オブジェクトを生成して周囲の敵にダメージを与える
				entt::entity explosionAttackArea = scene->CreateEntity("ExplosionAttackArea");
				if (scene->GetRegistry().all_of<BoxColliderComponent>(explosionAttackArea))
					scene->GetRegistry().remove<BoxColliderComponent>(explosionAttackArea);
				if (scene->GetRegistry().all_of<HurtboxComponent>(explosionAttackArea))
					scene->GetRegistry().remove<HurtboxComponent>(explosionAttackArea);
				if (scene->GetRegistry().all_of<RigidbodyComponent>(explosionAttackArea))
					scene->GetRegistry().remove<RigidbodyComponent>(explosionAttackArea);

				auto& expTc = scene->GetRegistry().get<TransformComponent>(explosionAttackArea);
				expTc.translate = hitPos;
				float expRad = 8.5f; // 爆発半径
				expTc.scale = {expRad, expRad, expRad};

				auto& expSc = scene->GetRegistry().emplace<ScriptComponent>(explosionAttackArea);
				expSc.scripts.push_back({"ExplosionAttackArea", "", std::make_shared<ExplosionAttackArea>(), false});

				auto& expVc = scene->GetRegistry().emplace<VariableComponent>(explosionAttackArea);
				// 直撃より少ないが大きくダメージ (通常時32、スキル時80)
				float expDmg = isSkillActive_ ? 80.0f : 32.0f;
				expVc.SetValue("Damage", expDmg);
				expVc.SetValue("ExplosionRadius", expRad);
				expVc.SetValue("IsPlayerAttack", 1.0f); // ★追加: プレイヤー攻撃フラグ
			}
		});
		// ★追加: プレイヤー被ダメージイベント
		scene->GetEventSystem().Subscribe("PlayerTakeDamage", [this, entity, scene](float) {
			if (!scene || !scene->GetContext().camera)
				return;
			// 1. 強めのカメラシェイク
			scene->GetContext().camera->StartShake(0.5f, 0.4f, 0.02f);

			// 2. ダメージ演出タイマーセット
			damageEffectTimer_ = DAMAGE_EFFECT_DURATION;

			// 3. プレイヤーの無敵時間を延長（少し長めの1.0秒に設定）
			if (scene->GetRegistry().all_of<HealthComponent>(entity)) {
				auto& hc = scene->GetRegistry().get<HealthComponent>(entity);
				hc.invincibleTime = 1.0f;
			}
		});
		debugSubscribeCount_ += 1;
		isSubscribed_ = true;
	}

	bool hasPhaseSystem = false;
	entt::entity gmEntity = entt::null;
	{
		auto scView = scene->GetRegistry().view<ScriptComponent>();
		for (auto e : scView) {
			auto& sc = scView.get<ScriptComponent>(e);
			for (auto& entry : sc.scripts) {
				if (entry.scriptPath == "PhaseSystemScript" || entry.scriptPath == "TutorialScript") {
					hasPhaseSystem = true;
					gmEntity = e;
					break;
				}
			}
			if (hasPhaseSystem)
				break;
		}
	}

	if (skillCooldown_ > 0.0f)
		skillCooldown_ -= dt;
	if (gunShootTimer_ > 0.0f)
		gunShootTimer_ -= dt;

	float currentBuffRadius = buffRadius_ * playerBuffRangeRate_;
	// ★追加: 設備へのバフ効果範囲の視覚化
	auto* renderer = scene->GetRenderer();
	if (renderer && scene->GetRegistry().all_of<TransformComponent>(entity)) {
		bool showAura = true;
		if (auto* tutorial = TutorialScript::GetInstance(); tutorial && !tutorial->IsAuraEnabled()) {
			showAura = false;
		}

		if (showAura) {
			auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
			const int segments = 48;
			for (int i = 0; i < segments; ++i) {
				float theta1 = (2.0f * 3.14159265f * i) / segments;
				float theta2 = (2.0f * 3.14159265f * (i + 1)) / segments;
				Engine::Vector3 p1 = {pTc.translate.x + currentBuffRadius * std::cos(theta1), pTc.translate.y + 0.15f, pTc.translate.z + currentBuffRadius * std::sin(theta1)};
				Engine::Vector3 p2 = {pTc.translate.x + currentBuffRadius * std::cos(theta2), pTc.translate.y + 0.15f, pTc.translate.z + currentBuffRadius * std::sin(theta2)};
				// 発光する黄緑色のラインでオーラ範囲を描画
				renderer->DrawLine3D(p1, p2, {0.6f, 1.0f, 0.2f, 1.0f}, true);
			}

			// ★追加: バフ中のタワーへのエネルギー線を描画 (案1)
			auto viewBuff = scene->GetRegistry().view<BuffComponent, TransformComponent>();
			for (auto [e, buff, tTc] : viewBuff.each()) {
				if (buff.isBuffed) {
					// プレイヤーの胸辺りからタワーの少し高めの位置へ線を引く
					Engine::Vector3 pCenter = {pTc.translate.x, pTc.translate.y + 1.0f, pTc.translate.z};
					Engine::Vector3 tCenter = {tTc.translate.x, tTc.translate.y + 1.5f, tTc.translate.z};
					
					// 少し線を太く/光って見せるため、微小なオフセットをつけて複数本描画
					renderer->DrawLine3D(pCenter, tCenter, {0.5f, 1.0f, 0.2f, 1.0f}, true);
					Engine::Vector3 o1 = {tCenter.x + 0.05f, tCenter.y, tCenter.z + 0.05f};
					renderer->DrawLine3D(pCenter, o1, {0.5f, 1.0f, 0.2f, 0.6f}, true);
					Engine::Vector3 o2 = {tCenter.x - 0.05f, tCenter.y, tCenter.z - 0.05f};
					renderer->DrawLine3D(pCenter, o2, {0.5f, 1.0f, 0.2f, 0.6f}, true);
				}
			}
		}
	}

	// ★追加: ダメージ演出の更新
	bool isPrep = (hasPhaseSystem && PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase);

	if (damageEffectTimer_ > 0.0f) {
		damageEffectTimer_ -= dt;
		if (damageEffectTimer_ < 0.0f)
			damageEffectTimer_ = 0.0f;

		if (renderer) {
			renderer->SetPostEffect("Rich"); // ★追加: 赤色ビネット対応の高品質シェーダーに切り替え
			auto params = renderer->GetPostProcessParams();
			float rate = damageEffectTimer_ / DAMAGE_EFFECT_DURATION; // 1.0 -> 0.0

			// ビネット強度 (最大1.5) を赤色ビネットに変更
			params.damageVignette = rate * 1.5f;
			// 色収差は廃止されたのでコメントアウト
			// params.chromaShift = rate * 0.05f;

			renderer->SetPostProcessParams(params);
			renderer->SetPostProcessEnabled(true);
		}
	} else {
		// 演出終了時はパラメータをリセットし、現在のフェーズに合わせたポストプロセスに戻す
		if (renderer) {
			auto params = renderer->GetPostProcessParams();
			if (params.damageVignette > 0.0f) {
				params.damageVignette = 0.0f;
				renderer->SetPostProcessParams(params);

				// ダメージ終了の瞬間に元の絵画風ポストプロセスに戻す
				renderer->SetPostEffect("Painterly");
			}
		}
	}

	// ★追加: フェーズに応じて被写界深度(DOF)を切り替える
	// 準備フェーズ(PreparationPhase)の時だけミニチュア風のピンボケ効果を有効化
	if (renderer) {
		renderer->SetPostProcessEnabled(true);

		auto params = renderer->GetPostProcessParams();
		float targetDof = isPrep ? 0.3f : 0.0f; // ★Renderer.hに合わせた値
		if (params.dofIntensity != targetDof) {
			params.dofIntensity = targetDof;
			renderer->SetPostProcessParams(params);
		}

		// ★修正: 毎フレーム Rich で上書きするのではなく、ダメージを受けていない通常時は
		// PhaseSystemScriptなどが設定したエフェクト（Painterly / Anime）を尊重するため、
		// ここではSetPostEffectを呼ばないようにします。
	}

	// ★スキルバフ持続時間の管理
	if (isSkillActive_) {
		skillDuration_ -= dt;
		if (skillDuration_ <= 0.0f) {
			isSkillActive_ = false;
			skillDuration_ = 0.0f;
			std::cout << "Gun Skill Deactivated\n";
		}
	}

	isPrep = (hasPhaseSystem && PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase);
	bool isInsert = (hasPhaseSystem && PhaseSystemScript::IsPhase() == PhaseSystemScript::InsertPhase);

	// ★追加: インサート中や準備フェーズ中は武器の切り替えやスキル発動を無効化
	if (!isPrep && !isInsert) {
		bool currentSwitchKeyDown = (GetAsyncKeyState('T') & 0x8000) != 0;
		if (currentSwitchKeyDown && !prevPlayerSwitchKeyDown_) {
			SwitchPlayerType(entity, scene);
		}
		prevPlayerSwitchKeyDown_ = currentSwitchKeyDown;

		bool currentSkillKeyDown = (GetAsyncKeyState('E') & 0x8000) != 0;
		if (currentSkillKeyDown && !prevSkillKeyDown_) {
			ExecuteSkill(entity, scene);
		}
		prevSkillKeyDown_ = currentSkillKeyDown;
	} else {
		prevPlayerSwitchKeyDown_ = false;
		prevSkillKeyDown_ = false;
	}

	static bool s_wasPrep = true;

	if (isPrep && !s_wasPrep) {
		// 戦闘フェーズ等から準備フェーズに戻った時、自動でカーソルを表示する
		isCursorVisible_ = true;
		while (ShowCursor(TRUE) < 0)
			;

		// ★追加: フェーズクリア(準備フェーズ移行)時に初期位置へワープし、飛行状態などもリセットする
		auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
		pTc.translate = initialPos_;

		if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
			auto& rb = scene->GetRegistry().get<RigidbodyComponent>(entity);
			rb.velocity = {0.0f, 0.0f, 0.0f};
		}
		isFlying_ = false;
		flightPressure_ = maxFlightPressure_;
		recoilVelocity_ = {0.0f, 0.0f, 0.0f};
	}
	s_wasPrep = isPrep;

	// ★追加: カーソル表示切り替え (Left Altキー)
	if (isPrep) {
		bool currentCursorToggle = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
		if (currentCursorToggle && !prevCursorToggle_) {
			isCursorVisible_ = !isCursorVisible_;
			if (isCursorVisible_) {
				while (ShowCursor(TRUE) < 0)
					;
			} else {
				while (ShowCursor(FALSE) >= 0)
					;
			}
		}
		prevCursorToggle_ = currentCursorToggle;
	} else {
		// 準備フェーズ以外は強制的に非表示
		if (isCursorVisible_) {
			isCursorVisible_ = false;
			while (ShowCursor(FALSE) >= 0)
				;
		}
		prevCursorToggle_ = false;
	}

	// ★追加: カーソル非表示時は画面中心に固定
	if (!isCursorVisible_) {
		HWND hwnd = GetActiveWindow();
		if (hwnd) {
			RECT rect;
			GetClientRect(hwnd, &rect);
			POINT center = {(rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2};
			ClientToScreen(hwnd, &center);

			POINT current;
			if (GetCursorPos(&current)) {
				// 画面中心からずれている場合のみSetCursorPosを呼ぶ
				if (current.x != center.x || current.y != center.y) {
					SetCursorPos(center.x, center.y);
				}
			}
		}
	}

	auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);

	if (!isPrep && !isInsert) {
		// ★追加: スチーム・ブースト（剣士モード専用の高速回避）
		bool currentDashKeyDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		if (currentDashKeyDown && !prevDashKeyDown_ && playerType_ == PlayerType::Sword) {
			if (steamPressure_ > 0.0f && !isRecharging_) { // ★少しでもあれば発動可能に
				steamPressure_ -= DASH_COST;
				if (steamPressure_ < 0.0f)
					steamPressure_ = 0.0f; // リチャージ判定へ

				float dx = 0.0f, dz = 0.0f;
				if (scene->GetRegistry().all_of<PlayerInputComponent>(entity)) {
					auto& input = scene->GetRegistry().get<PlayerInputComponent>(entity);
					if (std::abs(input.moveDir.x) > 0.1f || std::abs(input.moveDir.y) > 0.1f) {
						// ★修正: カメラの向きに合わせてダッシュ方向を計算 (CharacterMovementSystemと同期)
						if (auto* camera = scene->GetContext().camera) {
							auto camRot = camera->Rotation();
							float cy = std::cos(camRot.y);
							float sy = std::sin(camRot.y);
							dx = input.moveDir.x * cy + input.moveDir.y * sy;
							dz = -input.moveDir.x * sy + input.moveDir.y * cy;
						} else {
							dx = input.moveDir.x;
							dz = input.moveDir.y;
						}
					} else {
						dx = std::sin(pTc.rotate.y);
						dz = std::cos(pTc.rotate.y);
					}
				}

				float len = std::sqrt(dx * dx + dz * dz);
				if (len > 0.001f) {
					dx /= len;
					dz /= len;
					if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
						auto& rb = scene->GetRegistry().get<RigidbodyComponent>(entity);
						rb.velocity.x = dx * DASH_POWER;
						rb.velocity.z = dz * DASH_POWER;
						recoilVelocity_ = rb.velocity; // エフェクト用に保持
					}

					// ★演出: ブースト噴射エフェクト (進行方向と逆へ大量に噴射)
					for (int i = 0; i < 2; ++i) { // 2回生成して密度を倍増
						entt::entity boostVfx = scene->CreateEntity("SteamBoost_VFX");
						auto& bTc = scene->GetRegistry().get<TransformComponent>(boostVfx);
						// ★修正: 発生位置を1フレーム後の予測位置に合わせて離れすぎを防ぐ
						bTc.translate.x = pTc.translate.x + recoilVelocity_.x * dt;
						bTc.translate.y = pTc.translate.y + 1.0f;
						bTc.translate.z = pTc.translate.z + recoilVelocity_.z * dt;

						// 進行方向と逆（背面）に少しずらしてスラスター感を出す
						float backOffset = 0.8f;
						bTc.translate.x -= dx * backOffset;
						bTc.translate.z -= dz * backOffset;

						// 少しだけ横にずらして「ツインスラスター」感を出す
						float offsetX = (i == 0 ? -0.4f : 0.4f);
						float sideX = -dz * offsetX;
						float sideZ = dx * offsetX;
						bTc.translate.x += sideX;
						bTc.translate.z += sideZ;

						scene->SetTag(boostVfx, TagType::VFX);

						auto& bVc = scene->GetRegistry().emplace<VariableComponent>(boostVfx);
						bVc.SetValue("NormalX", -dx);
						bVc.SetValue("NormalY", 0.05f); // わずか斜め上
						bVc.SetValue("NormalZ", -dz);
						bVc.SetValue("Radius", 4.0f);
						bVc.SetValue("Duration", 0.4f);
						bVc.SetValue("ScatterMode", 0.0f);
						bVc.SetValue("ScatterSpeed", 13.0f);
						bVc.SetValue("Count", 40.0f);

						auto& bSc = scene->GetRegistry().emplace<ScriptComponent>(boostVfx);
						bSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
					}

					// ★追加: ブースト発動時のカメラシェイク (弱体化)
					if (auto* camera = scene->GetContext().camera) {
						camera->StartShake(0.12f, 0.15f, 0.01f);
					}
				}
			}
		}
		prevDashKeyDown_ = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

		// ★ジャンプ開始時の反動Yリセットはもう不要（rbに統合したため）

		// ★CharacterMovementSystem に完全に移動処理を委譲したため、独自の raycast 移動・壁判定・減衰処理は削除

		// ★追加: 飛行システム
		bool isGrounded = (pTc.translate.y <= scene->GetHeightAt(pTc.translate.x, pTc.translate.z, pTc.translate.y + 1.0f) + 1.1f);
		if (isGrounded && !isFlying_) {
			flightPressure_ += maxFlightPressure_ * 3.0f * dt; // 0.33秒で全回復する速度で徐々に回復
			if (flightPressure_ > maxFlightPressure_) {
				flightPressure_ = maxFlightPressure_;
			}
		}

		bool currentRightClickDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		if (playerType_ == PlayerType::Gun && !isPrep && !isInsert) {
			if (currentRightClickDown && !prevRightClickDown_) {
				if (isFlying_) {
					isFlying_ = false;
				} else if (flightPressure_ >= maxFlightPressure_) {
					isFlying_ = true;
					if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
						scene->GetRegistry().get<RigidbodyComponent>(entity).velocity.y = 20.0f;
					}
				}
			}

			if (isFlying_) {
				flightPressure_ -= FLIGHT_COST_PER_SEC * dt;
				if (flightPressure_ <= 0.0f) {
					flightPressure_ = 0.0f;
					isFlying_ = false;
				} else {
					float groundY = scene->GetHeightAt(pTc.translate.x, pTc.translate.z, pTc.translate.y + 1.0f);
					if (currentRightClickDown) {
						// チャージ中（押している間）は上昇
						if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
							auto& rb = scene->GetRegistry().get<RigidbodyComponent>(entity);
							rb.velocity.y += 60.0f * dt; // 上昇推力
							if (pTc.translate.y - groundY > MAX_FLIGHT_HEIGHT) {
								if (rb.velocity.y > 0.0f)
									rb.velocity.y *= 0.5f;
							}
						}
					}
					// 離している間はシステム側の減衰処理により自動的にホバリング（静止）する

					// 飛行中のスチームエフェクト
					static float flightVfxTimer = 0.0f;
					flightVfxTimer += dt;
					if (flightVfxTimer > 0.05f) {
						flightVfxTimer = 0.0f;
						entt::entity boostVfx = scene->CreateEntity("FlightSteam");
						auto& bTc = scene->GetRegistry().get<TransformComponent>(boostVfx);
						bTc.translate = {pTc.translate.x, pTc.translate.y, pTc.translate.z};
						scene->SetTag(boostVfx, TagType::VFX);
						auto& bVc = scene->GetRegistry().emplace<VariableComponent>(boostVfx);
						bVc.SetValue("NormalY", -1.0f);
						bVc.SetValue("Radius", 2.0f);
						bVc.SetValue("Duration", 0.3f);
						bVc.SetValue("ScatterSpeed", 12.0f);
						bVc.SetValue("Count", 25.0f);
						bVc.SetValue("IsFlight", 1.0f);
						auto& bSc = scene->GetRegistry().emplace<ScriptComponent>(boostVfx);
						bSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
					}
				}
			}
		} else {
			isFlying_ = false;
		}
		prevRightClickDown_ = currentRightClickDown;

		// ★CharacterMovementSystem側の重力蓄積をリセットする
		if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
			auto& rb = scene->GetRegistry().get<RigidbodyComponent>(entity);
			if (isFlying_) {
				rb.useGravity = false;
			} else {
				rb.useGravity = true;
			}
		}
	}

	if (isPrep || isInsert) {
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity))
			scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = false;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))
			scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = false;

		// ★追加: 準備フェーズ中は物理的な滑りや慣性を完全に殺し、一切歩かない（動かない）ようにする
		if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
			auto& rb = scene->GetRegistry().get<RigidbodyComponent>(entity);
			rb.velocity.x = 0.0f;
			rb.velocity.z = 0.0f;
		}
		recoilVelocity_.x = 0.0f;
		recoilVelocity_.z = 0.0f;

		UpdateSword(entity, scene, dt);
		UpdateGun(entity, scene, dt);
	} else {
		UpdateMovement(entity, scene, dt);
		if (playerType_ == PlayerType::Sword) {
			UpdateAttack(entity, scene, dt);
			UpdateSword(entity, scene, dt);
		} else {
			UpdateGunAttack(entity, scene, dt);
			UpdateGun(entity, scene, dt);
		}

		maxSteam = maxSteamPressure_ * playerMaxSteamPressureRate_;

		// ★追加: 蒸気圧リチャージ処理 (モードに関わらず共通で実行)
		if (isRecharging_) {
			rechargeTimer_ -= dt;
			// リチャージ中は徐々に蒸気圧が回復する
			float rechargeRate = maxSteam / RECHARGE_TIME;
			steamPressure_ += rechargeRate * dt;
			if (steamPressure_ >= maxSteam) {
				steamPressure_ = maxSteam;
				isRecharging_ = false;
				rechargeTimer_ = 0.0f;
			}
		}

		// ★追加: 圧力が尽きた際のリチャージ開始判定 (全モード共通)
		if (steamPressure_ <= 0.0f && !isRecharging_) {
			steamPressure_ = 0.0f;
			isRecharging_ = true;
			rechargeTimer_ = RECHARGE_TIME;
		}

		// ★追加: 圧力ゲージの描画 (リチャージ中、銃モード、消費中、スキル中、または戦闘フェーズなら表示)
		bool isBattle = (hasPhaseSystem && PhaseSystemScript::IsPhase() == PhaseSystemScript::BattlePhase);
		bool shouldShowGauge = (playerType_ == PlayerType::Gun) || isRecharging_ || (steamPressure_ < maxSteam) || isSkillActive_ || isBattle || isFlying_ || (flightPressure_ < maxFlightPressure_);
		if (shouldShowGauge) {
			DrawPressureGauge(scene);
		}

		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity))
			scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = true;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))
			scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = true;
	}

	// ★追加: 各ステージで世界の端から落ちないように見えない壁(座標クランプ)を実装
	if (!isPrep && !isInsert) {
		entt::entity floor = scene->FindObjectByName("Floor");
		if (floor != entt::null && scene->GetRegistry().all_of<TransformComponent>(floor)) {
			auto& fTc = scene->GetRegistry().get<TransformComponent>(floor);
			// Floorのスケールを元にクランプ境界を計算。マージン(1.0f)を引いて落ちる前に止める。
			float boundX = fTc.scale.x - 1.0f;
			float boundZ = fTc.scale.y - 1.0f; // Stage床はX軸に-90度回転しているので奥行きはscale.y

			auto& playerTc = scene->GetRegistry().get<TransformComponent>(entity);
			float clampedX = std::clamp(playerTc.translate.x, fTc.translate.x - boundX, fTc.translate.x + boundX);
			float clampedZ = std::clamp(playerTc.translate.z, fTc.translate.z - boundZ, fTc.translate.z + boundZ);

			// 座標がクランプされた場合＝壁にぶつかっているため、めり込み防止と慣性のリセット
			if (playerTc.translate.x != clampedX || playerTc.translate.z != clampedZ) {
				playerTc.translate.x = clampedX;
				playerTc.translate.z = clampedZ;

				if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
					auto& rb = scene->GetRegistry().get<RigidbodyComponent>(entity);
					if (playerTc.translate.x == clampedX)
						rb.velocity.x = 0.0f;
					if (playerTc.translate.z == clampedZ)
						rb.velocity.z = 0.0f;
				}
			}
		}
	}

	// ★追加: 大剣の空中溜め攻撃時の浮遊判定のため、状態フラグを VariableComponent に設定する
	{
		auto& vc = scene->GetRegistry().get_or_emplace<VariableComponent>(entity);
		vc.SetValue("IsSwordCharging", isSwordCharging_ ? 1.0f : 0.0f);
		vc.SetValue("IsSwordAttacking", (isAttacking_ && playerType_ == PlayerType::Sword) ? 1.0f : 0.0f);
	}

	// ★追加: プレイヤーのアニメーション制御
	if (scene->GetRegistry().all_of<AnimatorComponent>(entity)) {
		auto& anim = scene->GetRegistry().get<AnimatorComponent>(entity);
		std::string nextAnim = "[保留アクション]";
		bool loop = true;
		float speed = 1.0f;

		// 地上にいるか空中にいるか
		bool isGrounded = true;
		if (scene->GetRegistry().all_of<CharacterMovementComponent>(entity)) {
			isGrounded = scene->GetRegistry().get<CharacterMovementComponent>(entity).isGrounded;
		}

		// 動いているか
		bool isMoving = false;
		float rbVelSqXZ = 0.0f;
		if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
			const auto& vel = scene->GetRegistry().get<RigidbodyComponent>(entity).velocity;
			rbVelSqXZ = vel.x * vel.x + vel.z * vel.z;
			if (rbVelSqXZ > 0.05f) {
				isMoving = true;
			}
		}

		// アニメーション選択優先度:
		// 1. 攻撃モーション (剣)
		if (playerType_ == PlayerType::Sword && isAttacking_) {
			loop = false;
			speed = 2.5f * playerSwordAttackSpeedRate_; // ★攻撃モーションを早めてキビキビと反応させる
			if (comboCount_ == 1) {
				nextAnim = "凪払い";
			} else if (comboCount_ == 2) {
				nextAnim = "振り下ろし";
			} else if (comboCount_ == 3) {
				nextAnim = "回転飛ばし切り";
			} else { // comboCount_ == 0
				nextAnim = "振り下ろし";
			}
		}
		// 2. 地上での射撃モーション (銃) - ★地上にいる時のみ射撃を最優先
		else if (isGrounded && playerType_ == PlayerType::Gun && (isAiming_ || gunShootTimer_ > 0.0f || isCharging_)) {
			nextAnim = "射撃構え";
			loop = true;
			speed = 1.0f;
		}
		// 3. 飛行状態 (空中射撃時はこちらが優先され、射撃モーションは入らない)
		else if (isFlying_) {
			nextAnim = "圧力ダッシュ";
			loop = false; // ★飛行中は最後のフレーム（手を後ろにやったポーズ）で止める
			speed = 1.0f;
		}
		// 4. ダッシュ / 圧力ブースト
		else if (rbVelSqXZ > 2500.0f) { // 速度50以上(二乗で2500)でダッシュモーション
			nextAnim = "圧力ダッシュ";
			loop = false; // ★ダッシュ時も同じく止める
			speed = 1.0f;
		}
		// 5. ジャンプ (空中) - 空中射撃時もこちらが優先される
		else if (!isGrounded) {
			nextAnim = "ジャンプ";
			loop = true;
			speed = 1.0f;
		}
		// 6. 移動 (歩き)
		else if (isMoving) {
			nextAnim = "歩く.004";
			loop = true;
			speed = 1.2f;
		}
		// 7. 待機 (アイドル)
		else {
			nextAnim = "[保留アクション]";
			loop = true;
			speed = 1.0f;
		}

		// アニメーション切り替え時の時間リセット
		if (anim.currentAnimation != nextAnim) {
			anim.currentAnimation = nextAnim;
			anim.time = 0.0f;
		}
		anim.loop = loop;
		anim.speed = speed;
		anim.isPlaying = true;
	}

	// ==== ★修正: DrawUIで描画していた各種ゲーム内UI・エフェクトをここでキューに積む ====
	// ※App.cpp の描画順序により、IScript::DrawUI は Renderer::EndFrame の後（ImGuiフェーズ）に呼ばれるため、
	// Renderer にキューを積む処理（SDFUI, DrawString, DrawLine3Dなど）は Update 内で行う必要があります。
	{
		auto* uiRenderer = Engine::Renderer::GetInstance();
		if (uiRenderer) {
			// ---- HP＆経験値ゲージ (HPIN.png) ----
			float hpProgress = 1.0f;
			if (scene->GetRegistry().all_of<HealthComponent>(entity)) {
				auto& hc = scene->GetRegistry().get<HealthComponent>(entity);
				hpProgress = hc.maxHp > 0.0f ? (hc.hp / hc.maxHp) : 0.0f;
			}
			float expProgress = nextExperience_ > 0.0f ? (experience_ / nextExperience_) : 0.0f;
			hpProgress = std::clamp(hpProgress, 0.0f, 1.0f);
			expProgress = std::clamp(expProgress, 0.0f, 1.0f);

			static uint32_t s_hpinTex = 0;
			if (s_hpinTex == 0) {
				s_hpinTex = uiRenderer->LoadTexture2D("Resources/Textures/UI/HPIN.png");
			}

			// HPIN.pngの元画像サイズ (1024x128) とスケール
			float scale = 0.45f;
			float gaugeW = 1024.0f * scale;
			float gaugeH = 128.0f * scale;
			float gaugeX = 20.0f;
			float gaugeY = 20.0f;

			// ピクセルシェーダー(SDFUI)で灰色のゲージ内を埋める (HP)
			Engine::Renderer::SdfUIDesc hpDesc{};
			// 少し長さを増やし、左端(X=145付近)を揃える (中心520, 幅750)
			hpDesc.centerPx = {gaugeX + 520.0f * scale, gaugeY + 52.0f * scale};
			hpDesc.sizePx = {750.0f * scale, 12.0f * scale};
			hpDesc.lineWidth = 0.0f;
			hpDesc.glow = 0.0f;                      // 枠からはみ出ないようにglowを0に
			hpDesc.color = {0.8f, 0.2f, 0.2f, 1.0f}; // 赤色
			hpDesc.shape = 0;
			hpDesc.round = hpDesc.sizePx.y * 0.5f; // 端を完全な半円にする
			hpDesc.progress = hpProgress;
			hpDesc.fill = 1.0f;
			if (hpProgress > 0.0f)
				uiRenderer->DrawSDFUI(hpDesc);

			// ピクセルシェーダー(SDFUI)で灰色のゲージ内を埋める (経験値)
			Engine::Renderer::SdfUIDesc expDesc{};
			// HPゲージと長さを完全に一致させる (中心520, 幅750)
			expDesc.centerPx = {gaugeX + 520.0f * scale, gaugeY + 76.0f * scale};
			expDesc.sizePx = {750.0f * scale, 12.0f * scale};
			expDesc.lineWidth = 0.0f;
			expDesc.glow = 0.0f;
			expDesc.color = {0.2f, 0.8f, 0.2f, 1.0f}; // 緑色
			expDesc.shape = 0;
			expDesc.round = expDesc.sizePx.y * 0.5f;
			expDesc.progress = expProgress;
			expDesc.fill = 1.0f;
			if (expProgress > 0.0f)
				uiRenderer->DrawSDFUI(expDesc);

			// 上からHPIN.pngを被せる(透過部分からゲージが見える)
			Engine::Renderer::SpriteDesc hpinDesc{};
			hpinDesc.x = gaugeX;
			hpinDesc.y = gaugeY;
			hpinDesc.w = gaugeW;
			hpinDesc.h = gaugeH;
			hpinDesc.pivotX = 0.0f;
			hpinDesc.pivotY = 0.0f;
			uiRenderer->DrawSprite(s_hpinTex, hpinDesc);

			// テキスト (Lvと経験値)
			char textBuf[64];
			snprintf(textBuf, sizeof(textBuf), "Lv.%d   EXP: %.1f / %.1f", level_, experience_, nextExperience_);
			// 画像の右下にテキストを配置
			uiRenderer->DrawString(textBuf, gaugeX + 30.0f + 2.0f, gaugeY + gaugeH + 10.0f + 2.0f, 0.5f, {0, 0, 0, 1});
			uiRenderer->DrawString(textBuf, gaugeX + 30.0f, gaugeY + gaugeH + 10.0f, 0.5f, {1, 1, 1, 1});

			float isSkillTreeOpen = 0.0f;
			if (gmEntity != entt::null) {
				isSkillTreeOpen = GetVar(gmEntity, scene, "IsSkillTreeOpen", 0.0f);
			}

			// ==== ★追加: ロックオンレティクル & 銃レティクル ====
			if (playerType_ == PlayerType::Gun && isSkillTreeOpen < 0.5f && !isCursorVisible_) {
				DrawReticle(entity, scene);
			} else if (scene && scene->GetRegistry().all_of<CameraTargetComponent>(entity)) {
				auto& ct = scene->GetRegistry().get<CameraTargetComponent>(entity);
				if (ct.lockedTarget != entt::null && scene->GetRegistry().valid(ct.lockedTarget)) {
					auto& eTc = scene->GetRegistry().get<TransformComponent>(ct.lockedTarget);
					DirectX::XMFLOAT3 targetWorldPos = eTc.translate;
					targetWorldPos.y += 1.5f; // 敵の頭上

					auto* camera = &scene->GetCamera();
					if (camera) {
						float sx = 0.0f, sy = 0.0f;
						if (UISystem::WorldToScreen(targetWorldPos, *camera, sx, sy)) {
							// ダイヤモンド型のターゲットマーカー（4つの短い線）
							float reticleSize = 20.0f;
							Engine::Vector4 reticleColor = {1.0f, 0.3f, 0.3f, 1.0f}; // 赤

							// 外枠 (円形)
							Engine::Renderer::SdfUIDesc rd{};
							rd.shape = 1; // Circle
							rd.centerPx = {sx, sy};
							rd.sizePx = {reticleSize * 2.0f, reticleSize * 2.0f};
							rd.lineWidth = 2.0f;
							rd.glow = 3.0f;
							rd.color = reticleColor;
							rd.progress = 1.0f;
							rd.fill = 0.0f;
							rd.round = 0.0f;
							uiRenderer->DrawSDFUI(rd);

							// 内側のドット
							Engine::Renderer::SdfUIDesc dotDesc{};
							dotDesc.shape = 1;
							dotDesc.centerPx = {sx, sy};
							dotDesc.sizePx = {4.0f, 4.0f};
							dotDesc.lineWidth = 0.0f;
							dotDesc.glow = 2.0f;
							dotDesc.color = reticleColor;
							dotDesc.progress = 1.0f;
							dotDesc.fill = 1.0f;
							uiRenderer->DrawSDFUI(dotDesc);
						}
					}
				}
			}

			// ==== ★追加: マズルフラッシュ描画 (3Dライン) ====
			for (auto& mf : muzzleFlashes_) {
				mf.life -= dt;
				if (mf.life > 0.0f) {
					float alpha = mf.life / mf.maxLife;
					float size = 0.3f + (1.0f - alpha) * 0.5f;
					// 十字のフラッシュ
					uiRenderer->DrawLine3D({mf.pos.x - size, mf.pos.y, mf.pos.z}, {mf.pos.x + size, mf.pos.y, mf.pos.z}, {1.0f, 0.9f, 0.3f, alpha});
					uiRenderer->DrawLine3D({mf.pos.x, mf.pos.y - size, mf.pos.z}, {mf.pos.x, mf.pos.y + size, mf.pos.z}, {1.0f, 0.9f, 0.3f, alpha});
					uiRenderer->DrawLine3D({mf.pos.x, mf.pos.y, mf.pos.z - size}, {mf.pos.x, mf.pos.y, mf.pos.z + size}, {1.0f, 0.9f, 0.3f, alpha});
				}
			}
			while (!muzzleFlashes_.empty() && muzzleFlashes_.front().life <= 0)
				muzzleFlashes_.pop_front();

			// ==== ★追加: クリスタル飛散エフェクト描画 ====
			// (ユーザー要望により、古い青い線のクリスタルは描画しない)
			/*
			for (auto& cp : crystalParticles_) {
			    cp.life -= dt;
			    cp.pos.x += cp.velocity.x * dt;
			    cp.pos.y += cp.velocity.y * dt;
			    cp.pos.z += cp.velocity.z * dt;
			    cp.velocity.y -= 8.0f * dt;
			    cp.rot += cp.rotSpeed * dt;
			    if (cp.life > 0.0f) {
			        float alpha = (cp.life / cp.maxLife);
			        float s = cp.size;
			        float c = std::cos(cp.rot);
			        float sn = std::sin(cp.rot);
			        Engine::Vector3 top    = {cp.pos.x + sn * s, cp.pos.y + c * s, cp.pos.z};
			        Engine::Vector3 right  = {cp.pos.x + c * s, cp.pos.y - sn * s, cp.pos.z + s * 0.3f};
			        Engine::Vector3 bottom = {cp.pos.x - sn * s, cp.pos.y - c * s, cp.pos.z};
			        Engine::Vector3 left   = {cp.pos.x - c * s, cp.pos.y + sn * s, cp.pos.z - s * 0.3f};
			        Engine::Vector4 col = {cp.color.x, cp.color.y, cp.color.z, alpha * cp.color.w};
			        uiRenderer->DrawLine3D(top, right, col, true);
			        uiRenderer->DrawLine3D(right, bottom, col, true);
			        uiRenderer->DrawLine3D(bottom, left, col, true);
			        uiRenderer->DrawLine3D(left, top, col, true);
			        Engine::Vector4 colInner = {cp.color.x * 1.2f, cp.color.y * 1.2f, cp.color.z * 1.2f, alpha * 0.6f};
			        uiRenderer->DrawLine3D(top, bottom, colInner, true);
			        uiRenderer->DrawLine3D(left, right, colInner, true);
			    }
			}
			while (!crystalParticles_.empty() && crystalParticles_.front().life <= 0) crystalParticles_.pop_front();
			*/

			// ==== ★追加: 薬莢描画 (3Dラインで小さな金色の線) ====
			for (auto& sc : shellCasings_) {
				sc.life -= dt;
				sc.velocity.y -= 15.0f * dt; // 重力
				sc.pos.x += sc.velocity.x * dt;
				sc.pos.y += sc.velocity.y * dt;
				sc.pos.z += sc.velocity.z * dt;

				if (sc.life > 0.0f) {
					float alpha = sc.life / 0.6f;
					Engine::Vector4 shellColor = {0.9f, 0.7f, 0.2f, alpha};
					uiRenderer->DrawLine3D({sc.pos.x, sc.pos.y, sc.pos.z}, {sc.pos.x, sc.pos.y + 0.08f, sc.pos.z}, shellColor);
				}
			}
			while (!shellCasings_.empty() && shellCasings_.front().life <= 0)
				shellCasings_.pop_front();

			// ==== ★追加: スキルバフ中のUI表示 ====
			// 右下の円形UIに統合したため、ここでは描画しない
		}
	}

	// Update 内での ImGui 呼び出しは例外の原因となる可能性があるため、OnEditorUI に移動しました。
}

void PlayerScript::UpdateMovement(entt::entity entity, GameScene* scene, float /*dt*/) {
	if (!scene->GetRegistry().all_of<PlayerInputComponent>(entity))
		return;
	auto& input = scene->GetRegistry().get<PlayerInputComponent>(entity);

	entt::entity gmEntity = scene->FindObjectByName("GameManager");
	if (gmEntity != entt::null) {
		float isLevelUpPhase = GetVar(gmEntity, scene, "IsLevelUpPhase", 0.0f);
		if (isLevelUpPhase > 0.5f) {
			input.moveDir.x = 0.0f;
			input.moveDir.y = 0.0f;
			return;
		}
	}

	if (gunComboAnimTimer_ > 0.0f) {
		input.moveDir.x = 0.0f;
		input.moveDir.y = 0.0f;
		return;
	}

	float speedMul = 1.0f;
	if (isAttacking_) {
		speedMul = 0.2f;
	}
	if (isAiming_ && playerType_ == PlayerType::Gun) {
		speedMul = 0.4f;
	}
	// ★スキルバフ中は移動速度UP
	if (isSkillActive_ && playerType_ == PlayerType::Gun) {
		speedMul *= SKILL_SPEED_MULTIPLIER;
	}

	// ★飛行中は移動速度が大幅にアップ
	if (isFlying_ && playerType_ == PlayerType::Gun) {
		speedMul *= 3.0f; // ★3倍に（ベース速度に対して適用されるため非常に速くなります）
	}

	// ★追加: TPS視点として、Gunモード時かつエイム・射撃・チャージ中のみカメラの向きを向かせる（地上限定）
	if (playerType_ == PlayerType::Gun && !isFlying_ && (isAiming_ || gunShootTimer_ > 0.0f || isCharging_)) {
		auto* camera = &scene->GetCamera();
		if (camera) {
			auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
			pTc.rotate.y = camera->GetRotation().y;
			input.lockRotation = true; // CharacterMovementSystemでの移動方向への自動回転をブロック
		}
	} else {
		input.lockRotation = false;
	}

	// ★入力ベクトルの大きさが1.0に制限されるため、移動速度そのものを変更する
	if (scene->GetRegistry().all_of<CharacterMovementComponent>(entity)) {
		auto& cm = scene->GetRegistry().get<CharacterMovementComponent>(entity);
		static float baseSpeed = 0.0f;
		if (baseSpeed == 0.0f)
			baseSpeed = cm.speed;

		cm.speed = baseSpeed * speedMul * playerMoveSpeedRate_;
	}
}

void PlayerScript::UpdateAttack(entt::entity entity, GameScene* scene, float dt) {
	bool currentAttackKeyDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

	// ★追加: 大剣の溜め攻撃ロジック
	if (playerType_ == PlayerType::Sword && !isRecharging_) {
		if (currentAttackKeyDown) {
			if (!isAttacking_ && !isSwordCharging_) {
				isSwordCharging_ = true;
				swordChargeTime_ = 0.0f;
			}

			if (isSwordCharging_) {
				swordChargeTime_ += dt;
				// 溜め中の蒸気エフェクト
				swordChargeVfxTimer_ += dt;
				if (swordChargeVfxTimer_ > 0.15f) {
					swordChargeVfxTimer_ = 0.0f;
					auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
					entt::entity vfx = scene->CreateEntity("SwordCharge_Steam");
					auto& vTc = scene->GetRegistry().get<TransformComponent>(vfx);
					vTc.translate = pTc.translate;
					vTc.translate.y += 0.5f;
					scene->SetTag(vfx, TagType::VFX);
					auto& bVc = scene->GetRegistry().emplace<VariableComponent>(vfx);
					bVc.SetValue("NormalY", 1.0f);
					bVc.SetValue("Radius", 3.0f + std::min(swordChargeTime_, 1.0f) * 4.0f);
					bVc.SetValue("Count", 15.0f);
					bVc.SetValue("ScatterSpeed", 5.0f);
					bVc.SetValue("Duration", 0.5f);
					auto& bSc = scene->GetRegistry().emplace<ScriptComponent>(vfx);
					bSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
				}
			}
		} else {
			if (isSwordCharging_) {
				isSwordCharging_ = false;
				if (swordChargeTime_ >= 0.35f && (steamPressure_ > 0.0f || isSkillActive_)) { // ★少しでもあれば発動可能
					// ★溜め攻撃発動！
					steamPressure_ -= SWORD_CHARGE_COST;
					if (steamPressure_ < 0.0f)
						steamPressure_ = 0.0f;

					isAttacking_ = true;
					comboCount_ = 0; // 0は溜め攻撃用
					entt::entity sword = scene->FindObjectByName(swordName_);
					if (sword != entt::null) {
						auto& motion = scene->GetRegistry().get<MotionComponent>(sword);
						motion.PlayAnimation("SwordChargeAttack");

						// ★前方にジャンプする推進力を与える
						auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
						float dx = std::sin(pTc.rotate.y);
						float dz = std::cos(pTc.rotate.y);
						if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
							auto& rb = scene->GetRegistry().get<RigidbodyComponent>(entity);
							rb.velocity.x = dx * 38.0f;
							rb.velocity.z = dz * 38.0f;
							rb.velocity.y = 22.0f; // ★垂直方向の推進力を追加
						}

						if (auto* camera = scene->GetContext().camera) {
							camera->StartShake(0.35f, 0.45f, 0.02f);
						}
					}
				} else {
					// 溜めが足りない場合は通常攻撃（またはキャンセル）
					attackQueued_ = true;
				}
				swordChargeTime_ = 0.0f;
			}
		}
	}

	if (currentAttackKeyDown && !prevAttackKeyDown_) {
		if (!isSwordCharging_)
			attackQueued_ = true;
	}
	prevAttackKeyDown_ = currentAttackKeyDown;

	entt::entity sword = scene->FindObjectByName(swordName_);
	if (sword == entt::null)
		return;

	auto& motion = scene->GetRegistry().get<MotionComponent>(sword);

	if (isAttacking_) {
		sheatheTimer_ = 0.0f;
		isSheathed_ = false;

		if (!motion.isPlaying) {
			if (attackQueued_ && comboCount_ < 3 && comboCount_ >= 0) {
				comboCount_++;
				attackQueued_ = false;
				motion.PlayAnimation("Combo" + std::to_string(comboCount_));
			} else {
				isAttacking_ = false;
				comboCount_ = 0;
				attackQueued_ = false;
			}
		}
	} else {
		if (attackQueued_) {
			isAttacking_ = true;
			isSheathed_ = false;
			comboCount_ = 1;
			attackQueued_ = false;
			motion.PlayAnimation("Combo1");
		} else {
			if (!isSheathed_) {
				sheatheTimer_ += scene->GetContext().dt;
				if (sheatheTimer_ >= AUTO_SHEATHE_TIME) {
					isSheathed_ = true;
				}
			}
		}
	}
}

void PlayerScript::UpdateSword(entt::entity /*entity*/, GameScene* scene, float dt) {
	entt::entity sword = scene->FindObjectByName(swordName_);
	if (sword == entt::null)
		return;
	if (auto* hb = scene->GetRegistry().try_get<HitboxComponent>(sword)) {
		float swordDamage = 35.0f * playerSwordAttackPowerRate_;

		if (isSkillActive_) {
			swordDamage *= playerSwordSkillAttackPowerRate_;
		}

		hb->damage = swordDamage;
	}
	auto& swordTc = scene->GetRegistry().get<TransformComponent>(sword);

	if (!isAttacking_) {
		// 非攻撃時は常に背中に背負う配置にする
		swordTc.translate = {1.5f, 4.8f, -1.5f}; // ★さらに微調整で右へ動かすため、Xを1.5fに変更
		// Y軸回転を90度にしたことで逆になった傾きを正すため, X軸の回転を 30度 に変更
		swordTc.rotate = {DirectX::XMConvertToRadians(30.0f), DirectX::XMConvertToRadians(90.0f), DirectX::XMConvertToRadians(180.0f)};
		swordTc.scale = {1.3f, 1.3f, 1.3f}; // ★剣サイズを1.3倍に拡大

		// ★MotionSystemによる待機中のスケール＆座標リセットを防ぐため、再生クリップを空にする
		if (auto* motion = scene->GetRegistry().try_get<MotionComponent>(sword)) {
			motion->activeClip = "";
		}

		isSheathed_ = true;
		sheatheTimer_ = AUTO_SHEATHE_TIME;
	}

	if (auto* motion = scene->GetRegistry().try_get<MotionComponent>(sword)) {
		if (motion->isPlaying) {
			auto it = motion->clips.find(motion->activeClip);
			if (it != motion->clips.end()) {
				float duration = it->second.totalDuration;
				float t = motion->currentTime;
				float start = duration * 0.10f;
				float end = duration * 0.85f;
				bool active = (t > start && t < end);
				if (auto* hb = scene->GetRegistry().try_get<HitboxComponent>(sword)) {
					hb->isActive = active;
				}

				// ★追加: 溜め攻撃の着地衝撃 (t=0.7s)
				if (motion->activeClip == "SwordChargeAttack") {
					static float lastImpactTime = -1.0f;
					if (t >= 0.7f && t < 0.8f && lastImpactTime < 0.0f) {
						lastImpactTime = t;

						// ワールド座標を取得
						Engine::Matrix4x4 worldMat = scene->GetWorldMatrix((int)sword);
						Engine::Vector3 worldPos = {worldMat.m[3][0], worldMat.m[3][1], worldMat.m[3][2]};
						float groundY = scene->GetHeightAt(worldPos.x, worldPos.z, worldPos.y + 1.0f);

						// 1. 地面が歪む巨大な衝撃波 (SpaceShatter)
						entt::entity impactVfx = scene->CreateEntity("GroundSmash_VFX");
						auto& iTc = scene->GetRegistry().get<TransformComponent>(impactVfx);
						iTc.translate = {worldPos.x, groundY + 0.1f, worldPos.z};
						scene->SetTag(impactVfx, TagType::VFX);

						auto& iVc = scene->GetRegistry().emplace<VariableComponent>(impactVfx);
						iVc.SetValue("NormalY", 1.0f);
						iVc.SetValue("Radius", 25.0f); // 巨大な歪み
						iVc.SetValue("Duration", 0.9f);
						iVc.SetValue("ScatterMode", 0.0f);
						iVc.SetValue("ScatterSpeed", 35.0f);
						iVc.SetValue("Count", 120.0f); // 密度高め
						auto& iSc = scene->GetRegistry().emplace<ScriptComponent>(impactVfx);
						iSc.scripts.push_back({"SpaceShatterScript", "", nullptr});

						// 2. スチームバースト (白い蒸気の爆発)
						entt::entity steamVfx = scene->CreateEntity("GroundSmash_Steam");
						auto& sTc = scene->GetRegistry().get<TransformComponent>(steamVfx);
						sTc.translate = iTc.translate;
						scene->SetTag(steamVfx, TagType::VFX);
						auto& sVc = scene->GetRegistry().emplace<VariableComponent>(steamVfx);
						sVc.SetValue("NormalY", 0.8f);
						sVc.SetValue("Radius", 15.0f);
						sVc.SetValue("Count", 60.0f);
						sVc.SetValue("ScatterSpeed", 18.0f);
						sVc.SetValue("Duration", 0.7f);
						auto& sSc = scene->GetRegistry().emplace<ScriptComponent>(steamVfx);
						sSc.scripts.push_back({"SpaceShatterScript", "", nullptr});

						// 3. 範囲ダメージ (Hitbox)
						entt::entity aoe = scene->GetRegistry().create();
						scene->GetRegistry().emplace<TagComponent>(aoe).tag = TagType::PlayerSword;
						auto& aTc = scene->GetRegistry().emplace<TransformComponent>(aoe);
						aTc.translate = iTc.translate;
						auto& aHb = scene->GetRegistry().emplace<HitboxComponent>(aoe);
						aHb.isActive = true;
						float chargeAttackDamage = 80.0f * playerSwordAttackPowerRate_;

						if (isSkillActive_) {
							chargeAttackDamage *= playerSwordSkillAttackPowerRate_;
						}

						aHb.damage = chargeAttackDamage; // ★弱体化: 250.0f -> 80.0f (範囲強攻撃としてのバランス調整)
						aHb.size = {18.0f, 5.0f, 18.0f};
						aHb.tag = TagType::Sword;
						scene->DestroyObject((uint32_t)aoe);

						if (auto* camera = scene->GetContext().camera) {
							camera->StartShake(0.5f, 0.8f, 0.04f); // 激しい揺れ
						}
					}
					if (t < 0.1f)
						lastImpactTime = -1.0f; // リセット
				}
			}
		} else {
			if (auto* hb = scene->GetRegistry().try_get<HitboxComponent>(sword)) {
				hb->isActive = false;
			}
		}
	}

	bool hitboxActive = false;
	if (scene->GetRegistry().all_of<HitboxComponent>(sword)) {
		hitboxActive = scene->GetRegistry().get<HitboxComponent>(sword).isActive;
	}

	// ★追加：攻撃中の剣のダイナミック自発光エフェクト（青白くネオン発光）
	if (scene->GetRegistry().all_of<MeshRendererComponent>(sword)) {
		auto& sMr = scene->GetRegistry().get<MeshRendererComponent>(sword);
		if (isAttacking_) {
			// 攻撃中は青白く美しく発光（HDRによるブルーム効果で光り溢れます）
			sMr.color = {1.5f, 2.2f, 3.2f, 1.0f};
		} else {
			// 通常時はToonかつほんのり微発光
			sMr.color = {1.2f, 1.2f, 1.2f, 1.0f};
		}
	}

	if (isAttacking_ && hitboxActive) {
		Engine::Matrix4x4 worldMat = scene->GetWorldMatrix((int)sword);
		DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&worldMat));
		float bladeLen = 3.2f;                                                                                            // ★大剣の長さに合わせる
		DirectX::XMVECTOR basePos = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, -bladeLen * 0.1f, 0, 1), m); // 大剣モデル本来の長手方向であるY軸を使用
		DirectX::XMVECTOR tipPos = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, bladeLen * 0.9f, 0, 1), m);

		TrailPoint tp;
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&tp.base), basePos);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&tp.tip), tipPos);
		tp.life = 0.5f; // ★重さを出すため軌跡を少し長く残す
		tp.maxLife = 0.5f;
		trailPoints_.push_back(tp);
		if (trailPoints_.size() > 80)
			trailPoints_.pop_front();
	}

	for (auto& tp : trailPoints_)
		tp.life -= dt;
	while (!trailPoints_.empty() && trailPoints_.front().life <= 0)
		trailPoints_.pop_front();

	auto* renderer = scene->GetRenderer();
	if (renderer && trailPoints_.size() >= 2) {
		for (size_t i = 1; i < trailPoints_.size(); ++i) {
			float alpha = trailPoints_[i].life / trailPoints_[i].maxLife;
			Engine::Vector4 col = {0.2f, 0.6f, 1.0f, alpha * 0.95f};
			// 少しずらした線を追加して「厚み」を出す
			Engine::Vector3 tip1 = trailPoints_[i - 1].tip;
			Engine::Vector3 tip2 = trailPoints_[i].tip;
			Engine::Vector3 base1 = trailPoints_[i - 1].base;
			Engine::Vector3 base2 = trailPoints_[i].base;

			renderer->DrawLine3D(tip1, tip2, col, true);
			renderer->DrawLine3D(base1, base2, col, true);
			renderer->DrawLine3D(tip1, base1, col, true);

			Engine::Vector3 off = {0.0f, 0.03f, 0.0f};
			renderer->DrawLine3D(tip1 + off, tip2 + off, col, true);
			renderer->DrawLine3D(base1 + off, base2 + off, col, true);
			renderer->DrawLine3D(tip1 - off, tip2 - off, col, true);
			renderer->DrawLine3D(base1 - off, base2 - off, col, true);
		}
	}
}

void PlayerScript::UpdateGun(entt::entity entity, GameScene* scene, float /*dt*/) {
	entt::entity gun = scene->FindObjectByName(gunName_);
	if (gun == entt::null)
		return;

	auto& gunTc = scene->GetRegistry().get<TransformComponent>(gun);

	// ★追加：ガンナーモードの時のみ銃を表示する
	if (auto* mr = scene->GetRegistry().try_get<MeshRendererComponent>(gun)) {
		mr->enabled = (playerType_ == PlayerType::Gun);
	}

	if (isAiming_) {
		// エイム時：しっかり構える（平行に、少し低く）
		gunTc.translate = {0.6f, 2.0f, 2.0f}; // ★Z軸(前方向)を+1.0して前に出す
		gunTc.rotate = {0.0f, 0.0f, 0.0f};
	} else {
		// 非エイム時：腰だめ（平行に、もっと低く）
		gunTc.translate = {0.8f, 1.7f, 1.8f}; // ★Z軸(前方向)を+1.2して前に出す
		gunTc.rotate = {0.0f, 0.0f, 0.0f};    // 地面と平行に
	}

	// ★追加: 飛行中でロックしている敵がいる場合は、銃口だけ敵の方を向かせる
	if (isFlying_ && lockedEnemy_ != entt::null && scene->GetRegistry().valid(lockedEnemy_)) {
		auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
		auto& eTc = scene->GetRegistry().get<TransformComponent>(lockedEnemy_);

		float dx = eTc.translate.x - pTc.translate.x;
		float dy = (eTc.translate.y + 1.0f) - (pTc.translate.y + 1.0f); // 敵の胸を狙う (銃の高さは概ね+1.0f)
		float dz = eTc.translate.z - pTc.translate.z;

		float distXZ = std::sqrt(dx * dx + dz * dz);
		float targetYaw = std::atan2(dx, dz);
		float targetPitch = std::atan2(-dy, distXZ);

		// 銃はプレイヤーの子なので、プレイヤーの回転を打ち消す（ローカルYaw）
		float localYaw = targetYaw - pTc.rotate.y;
		while (localYaw > DirectX::XM_PI)
			localYaw -= DirectX::XM_2PI;
		while (localYaw < -DirectX::XM_PI)
			localYaw += DirectX::XM_2PI;

		// 360度自然に構えるために、銃のローカル座標も旋回させる（プレイヤーを中心に公転）
		float offsetX = 0.8f;
		float offsetY = 1.7f;
		float offsetZ = 1.8f; // ★Z軸を前に出してめり込み防止

		float cy = std::cos(localYaw);
		float sy = std::sin(localYaw);

		// 右手(X=0.8)を基準に回転させる
		gunTc.translate.x = offsetX * cy + offsetZ * sy;
		gunTc.translate.y = offsetY;
		gunTc.translate.z = -offsetX * sy + offsetZ * cy;

		// スムーズな回転補間（360度滑らかに追従）
		float diffYaw = localYaw - gunTc.rotate.y;
		while (diffYaw > DirectX::XM_PI)
			diffYaw -= DirectX::XM_2PI;
		while (diffYaw < -DirectX::XM_PI)
			diffYaw += DirectX::XM_2PI;

		gunTc.rotate.y += diffYaw * 0.2f; // Lerp
		gunTc.rotate.x = targetPitch;
	}

	gunTc.scale = {2.2f, 2.2f, 2.2f}; // ピストルのアスペクト比を維持しつつ大きく表示
}

void PlayerScript::UpdateGunAttack(entt::entity entity, GameScene* scene, float dt) {
	isAiming_ = false; // 右クリックは飛行用になったためエイムは廃止
	bool currentAttackKeyDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

	// ★リチャージ中は射撃不可（スキル発動中のみオーバークロックで許可）
	if (isRecharging_ && !isSkillActive_) {
		prevAttackKeyDown_ = currentAttackKeyDown;
		return;
	}

	auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);

	// ★ロックオンの更新 (飛行中)
	if (isFlying_) {
		float minDist = 1000.0f;
		entt::entity bestEnemy = entt::null;
		auto& registry = scene->GetRegistry();
		auto enemies = registry.view<TagComponent>();
		for (auto e : enemies) {
			if (enemies.get<TagComponent>(e).tag == TagType::Enemy) {
				auto* tc = registry.try_get<TransformComponent>(e);
				auto* hc = registry.try_get<HealthComponent>(e);
				if (tc && hc && !hc->isDead) {
					const auto& eTc = tc->translate;
					float dx = eTc.x - pTc.translate.x;
					float dy = eTc.y - pTc.translate.y;
					float dz = eTc.z - pTc.translate.z;
					float distSq = dx * dx + dy * dy + dz * dz;
					if (distSq < minDist * minDist) {
						minDist = std::sqrt(distSq);
						bestEnemy = e;
					}
				}
			}
		}
		lockedEnemy_ = bestEnemy;

		// 飛行中は本体（プレイヤー）は移動方向を向くため、ここでは敵の方へ回転させない
		// (銃口のみが敵を追う処理は UpdateGun にて実装済み)
	} else {
		lockedEnemy_ = entt::null;

		// 地上ではカメラ方向または移動方向を向く
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity)) {
			auto& ct = scene->GetRegistry().get<CameraTargetComponent>(entity);
			if (ct.lockedTarget != entt::null && scene->GetRegistry().valid(ct.lockedTarget)) {
				bool isActionActive = isCharging_ || currentAttackKeyDown || gunShootTimer_ > 0.0f;
				if (isActionActive) {
					auto& eTc = scene->GetRegistry().get<TransformComponent>(ct.lockedTarget);
					float dx = eTc.translate.x - pTc.translate.x;
					float dz = eTc.translate.z - pTc.translate.z;
					float targetYaw = std::atan2(dx, dz);
					float diff = targetYaw - pTc.rotate.y;
					while (diff > DirectX::XM_PI)
						diff -= DirectX::XM_2PI;
					while (diff < -DirectX::XM_PI)
						diff += DirectX::XM_2PI;
					pTc.rotate.y += diff * std::min(1.0f, 30.0f * dt);
				}
			}
		}
	}

	// ★反動後退の物理更新 (Updateへ移動)

	// ★チャージショット処理
	if (currentAttackKeyDown && gunShootTimer_ <= 0.0f) {
		if (!isCharging_ && !prevAttackKeyDown_) {
			// チャージ開始
			isCharging_ = true;
			chargeTime_ = 0.0f;
			chargeVfxTimer_ = 0.0f;
		}

		if (isCharging_) {
			chargeTime_ += dt;
			chargeTime_ = std::min(chargeTime_, CHARGE_TIME_MAX);

			// チャージ中の蒸気排出VFX（断続的に）
			chargeVfxTimer_ -= dt;
			if (chargeVfxTimer_ <= 0.0f && chargeTime_ > 0.15f) {
				// 銃の横から蒸気を少しずつ出す
				entt::entity gun = scene->FindObjectByName(gunName_);
				if (gun != entt::null) {
					Engine::Matrix4x4 gunWorld = scene->GetWorldMatrix((int)gun);
					DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gunWorld));
					DirectX::XMVECTOR gunPos = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, 0.0f, 1), m);
					DirectX::XMVECTOR gunRight = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(1, 0, 0, 0), m));
					DirectX::XMVECTOR gunFwd = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 0, 1, 0), m));

					DirectX::XMFLOAT3 steamPos;
					DirectX::XMStoreFloat3(&steamPos, gunPos);
					DirectX::XMFLOAT3 fwd;
					DirectX::XMStoreFloat3(&fwd, gunFwd);

					// 短い蒸気エフェクト
					entt::entity steamVfx = scene->CreateEntity("ChargeSteam_VFX");
					auto& sTc = scene->GetRegistry().get<TransformComponent>(steamVfx);
					sTc.translate = steamPos;
					scene->SetTag(steamVfx, TagType::VFX);

					auto& sVc = scene->GetRegistry().emplace<VariableComponent>(steamVfx);
					sVc.SetValue("NormalX", fwd.x);
					sVc.SetValue("NormalY", fwd.y);
					sVc.SetValue("NormalZ", fwd.z);
					sVc.SetValue("Radius", 1.0f + chargeTime_ * 1.5f); // チャージが進むと大きく
					sVc.SetValue("Duration", 0.25f);
					sVc.SetValue("ScatterMode", 0.0f);
					sVc.SetValue("ScatterDelay", 0.0f);
					sVc.SetValue("ScatterSpeed", 2.0f);
					sVc.SetValue("Count", 5.0f);

					auto& sSc = scene->GetRegistry().emplace<ScriptComponent>(steamVfx);
					sSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
				}

				// チャージが進むほど頻度が上がる（プシュッ、プシュッ → プシュシュシュ）
				float interval = 0.25f - (chargeTime_ / CHARGE_TIME_MAX) * 0.15f;
				chargeVfxTimer_ = std::max(0.08f, interval);
			}

			// 銃の振動（チャージが進むほど激しく）
			entt::entity gun = scene->FindObjectByName(gunName_);
			if (gun != entt::null) {
				auto& gunTc = scene->GetRegistry().get<TransformComponent>(gun);
				float intensity = (chargeTime_ / CHARGE_TIME_MAX) * 0.06f;
				gunTc.translate.x += ((rand() % 100) / 100.0f - 0.5f) * intensity;
				gunTc.translate.y += ((rand() % 100) / 100.0f - 0.5f) * intensity;
			}
		}
	}

	// ★ボタンを離した時の処理
	if (!currentAttackKeyDown && prevAttackKeyDown_ && isCharging_) {
		isCharging_ = false;

		// スキル発動中（オーバークロック）はコストを無視して撃てる
		float cCost = CHARGE_SHOT_COST * (isFlying_ ? 0.5f : 1.0f); // ★飛行中はコスト半減
		float nCost = NORMAL_SHOT_COST * (isFlying_ ? 0.5f : 1.0f); // ★飛行中はコスト半減

		if (chargeTime_ >= CHARGE_TIME_MIN && (isSkillActive_ || steamPressure_ > 0.0f)) {
			// ★チャージショット発射！
			ShootChargeShot(entity, scene);
			if (!isSkillActive_)
				steamPressure_ -= cCost;

			// 反動後退
			float forwardX = std::sin(pTc.rotate.y);
			float forwardZ = std::cos(pTc.rotate.y);
			float recoilPower = 12.0f * (chargeTime_ / CHARGE_TIME_MAX);
			recoilVelocity_.x = -forwardX * recoilPower;
			recoilVelocity_.z = -forwardZ * recoilPower;

			float chargeShotInterval = 0.5f;

			if (isFlying_) {
				chargeShotInterval = 0.25f;
			}

			float gunAttackSpeedRate = playerGunAttackSpeedRate_;

			if (gunAttackSpeedRate <= 0.0f) {
				gunAttackSpeedRate = 1.0f;
			}

			gunShootTimer_ = chargeShotInterval / gunAttackSpeedRate;
		} else if (isSkillActive_ || steamPressure_ > 0.0f) {
			// 通常射撃（短押し）- 残量に関わらず撃てる
			ShootGun(entity, scene);
			if (!isSkillActive_)
				steamPressure_ -= nCost;
			float normalShotInterval = 0.3f;

			if (isFlying_) {
				normalShotInterval = 0.12f;
			}

			float gunAttackSpeedRate = playerGunAttackSpeedRate_;

			if (gunAttackSpeedRate <= 0.0f) {
				gunAttackSpeedRate = 1.0f;
			}

			gunShootTimer_ = normalShotInterval / gunAttackSpeedRate;
		}

		chargeTime_ = 0.0f;

		// 圧力がなくなったらリチャージ開始 (Updateの共通処理へ移動)
	}

	// クールダウン
	if (gunShootTimer_ > 0.0f)
		gunShootTimer_ -= dt;

	prevAttackKeyDown_ = currentAttackKeyDown;
}

void PlayerScript::ShootChargeShot(entt::entity entity, GameScene* scene) {
	float baseDamage = 45.0f; // ★復元: 22.0f -> 45.0f
	float chargeMul = chargeTime_ / CHARGE_TIME_MAX;
	float damage = baseDamage * playerGunAttackPowerRate_ * (0.5f + chargeMul * 0.5f);

	if (isSkillActive_) {
		damage *= SKILL_DAMAGE_MULTIPLIER;
		damage *= playerGunSkillAttackPowerRate_;
	}

	SpawnBullet(entity, scene, 0.0f, 0.0f, damage, 3.0f, true, true);

	auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
	entt::entity gun = scene->FindObjectByName(gunName_);
	DirectX::XMFLOAT3 muzzlePos = pTc.translate;
	muzzlePos.y += 1.0f;
	float fwdX = std::sin(pTc.rotate.y);
	float fwdZ = std::cos(pTc.rotate.y);

	if (gun != entt::null) {
		Engine::Matrix4x4 gunWorld = scene->GetWorldMatrix((int)gun);
		DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gunWorld));
		DirectX::XMVECTOR tip = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, 0.6f, 1), m);
		DirectX::XMStoreFloat3(&muzzlePos, tip);
		DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 0, 1, 0), m));
		fwdX = DirectX::XMVectorGetX(forward);
		fwdZ = DirectX::XMVectorGetZ(forward);
	}

	entt::entity muzzleVfx = scene->CreateEntity("ChargeShot_VFX");
	auto& mTc = scene->GetRegistry().get<TransformComponent>(muzzleVfx);
	mTc.translate = muzzlePos;
	scene->SetTag(muzzleVfx, TagType::VFX);

	auto& mvc = scene->GetRegistry().emplace<VariableComponent>(muzzleVfx);
	mvc.SetValue("NormalX", fwdX);
	mvc.SetValue("NormalY", 0.0f);
	mvc.SetValue("NormalZ", fwdZ);
	mvc.SetValue("Radius", 5.0f + chargeMul * 3.0f);
	mvc.SetValue("Duration", 0.8f);
	mvc.SetValue("ScatterMode", 0.0f);
	mvc.SetValue("ScatterDelay", 0.0f);
	mvc.SetValue("ScatterSpeed", 15.0f);
	mvc.SetValue("Count", 50.0f + chargeMul * 30.0f);
	mvc.SetValue("IsSpecial", 1.0f);

	auto& msc = scene->GetRegistry().emplace<ScriptComponent>(muzzleVfx);
	msc.scripts.push_back({"SpaceShatterScript", "", nullptr});

	MuzzleFlash flash;
	flash.pos = muzzlePos;
	flash.life = 0.12f;
	flash.maxLife = 0.12f;
	muzzleFlashes_.push_back(flash);
}

void PlayerScript::DrawPressureGauge(GameScene* scene) {
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	// ===== 1. 基本設定 =====
	float screenW = (float)Engine::WindowDX::kW;
	float screenH = (float)Engine::WindowDX::kH;
	float gaugeX = screenW - 130.0f; // 右下に配置
	float gaugeY = screenH - 120.0f; // 右下に配置
	float R = 75.0f; // 少し小さくする（元は85.0f）
	float pressureRatio = std::clamp(steamPressure_ / maxSteam, 0.0f, 1.0f);
	float startAngle = DirectX::XM_PI * 0.75f;
	float totalAngle = DirectX::XM_PI * 1.5f;

	// ===== 2. 背景・ベゼル =====
	renderer->DrawSDFUI({
	    {gaugeX + 3, gaugeY + 3},
        {R + 8, R + 8},
        0, 6.0f, {0, 0, 0, 0.3f},
        1, 0, 0, 1
    });
	renderer->DrawSDFUI({
	    {gaugeX, gaugeY},
        {R + 6, R + 6},
        0, 0, {0.25f, 0.18f, 0.06f, 1.0f},
        1, 0, 0, 1
    });
	renderer->DrawSDFUI({
	    {gaugeX, gaugeY},
        {R + 4, R + 4},
        1.5f, 0, {0.85f, 0.75f, 0.4f, 1.0f},
        1, 0, 0
    });
	renderer->DrawSDFUI({
	    {gaugeX, gaugeY},
        {R + 1, R + 1},
        1.0f, 0, {0.2f, 0.15f, 0.05f, 0.8f},
        1, 0, 0
    });
	renderer->DrawSDFUI({
	    {gaugeX, gaugeY},
        {R - 4, R - 4},
        0, 0, {0.96f, 0.94f, 0.88f, 1.0f},
        1, 0, 0, 1
    });
	renderer->DrawSDFUI({
	    {gaugeX, gaugeY},
        {R - 5, R - 5},
        8.0f, 4.0f, {0.3f, 0.2f, 0.1f, 0.15f},
        1, 0, 0
    });

	// ===== 3. リベット =====
	float rivetR = R + 3.0f;
	for (int i = 0; i < 4; ++i) {
		float angle = DirectX::XM_PIDIV4 + DirectX::XM_PIDIV2 * (float)i;
		float rx = gaugeX + std::cos(angle) * rivetR;
		float ry = gaugeY + std::sin(angle) * rivetR;
		renderer->DrawSDFUI({
		    {rx, ry},
            {3.0f, 3.0f},
            0, 0, {0.6f, 0.5f, 0.2f, 1.0f},
            1, 0, 0, 1
        });
		renderer->DrawSDFUI({
		    {rx - 0.5f, ry - 0.5f},
            {1.0f, 1.0f},
            0, 0, {1, 1, 1, 0.3f},
            1, 0, 0, 1
        });
	}

	// ===== 4. 危険ゾーン（赤いドット）=====
	{
		float dangerStart = 0.75f;
		float zoneR = R - 10.0f;
		for (int i = 0; i < 8; ++i) {
			float t = dangerStart + (float)i / 8.0f * (1.0f - dangerStart);
			float angle = startAngle + t * totalAngle;
			float dotSize = 3.5f;
			renderer->DrawSDFUI({
			    {gaugeX + std::cos(angle) * zoneR, gaugeY + std::sin(angle) * zoneR},
                {dotSize, dotSize},
                0, 0, {0.8f, 0.1f, 0.05f, 0.6f},
                1, 0, 0, 1
            });
		}
	}

	// ===== 5. 目盛り（ドットを高密度化）=====
	for (int i = 0; i <= 50; ++i) {
		float angle = startAngle + (float)i / 50.0f * totalAngle;
		bool isMajor = (i % 10 == 0);
		bool isMid = (i % 5 == 0);
		float dotSize = isMajor ? 4.8f : (isMid ? 3.2f : 1.6f);
		float tickR = R - 10.0f;
		float alpha = isMajor ? 1.0f : (isMid ? 0.7f : 0.35f);
		renderer->DrawSDFUI({
		    {gaugeX + std::cos(angle) * tickR, gaugeY + std::sin(angle) * tickR},
            {dotSize, dotSize},
            0, 0, {0.15f, 0.1f, 0.05f, alpha},
            1, 0, 0, 1
        });
	}

	// ===== 6. 数字ラベル =====
	const char* numLabels[] = {"0", "50", "100"};
	float labelSteps[] = {0.0f, 0.5f, 1.0f};
	for (int i = 0; i < 3; ++i) {
		float angle = startAngle + labelSteps[i] * totalAngle;
		float labelR = R - 30.0f;
		float sx = gaugeX + std::cos(angle) * labelR;
		float sy = gaugeY + std::sin(angle) * labelR;
		float fontSize = 0.35f;

		float tw = renderer->MeasureTextWidth(numLabels[i], fontSize);
		renderer->DrawString(numLabels[i], sx - tw * 0.5f, sy - 5.0f, fontSize, {0.1f, 0.08f, 0.05f, 1.0f});
	}

	// ===== 7. 中央ラベル（PRESSURE）=====
	{
		float fontSize = 0.28f;
		const char* title = "PRESSURE";
		float tw = renderer->MeasureTextWidth(title, fontSize);
		renderer->DrawString(title, gaugeX - tw * 0.5f, gaugeY + R * 0.25f, fontSize, {0.25f, 0.18f, 0.1f, 0.8f});
	}

	// ===== 8. 針 & カウンターウェイト（微細な振動を追加）=====
	float currentAngle = startAngle + pressureRatio * totalAngle;

	// プルプルした振動（ジッター）の計算
	{
		float time = (float)GetTickCount64() * 0.001f;
		float jitterBase = std::sin(time * 60.0f) * 0.012f;           // 高速なサイン波
		jitterBase += (float(rand() % 100) / 100.0f - 0.5f) * 0.008f; // 不規則なノイズ

		float jitterIntensity = 0.4f + pressureRatio * 0.6f; // 圧力が高いほど震える
		if (isSkillActive_)
			jitterIntensity *= 2.2f; // スキル中はさらに激しく

		// ★チャージ中の暴力的な揺れを追加
		if (isCharging_) {
			float cRatio = std::clamp(chargeTime_ / CHARGE_TIME_MAX, 0.0f, 1.0f);
			// チャージが進むほど、振幅の大きい不規則なガタつきを加える
			jitterBase += (float(rand() % 100) / 100.0f - 0.5f) * 0.15f * cRatio;
			jitterIntensity += cRatio * 2.0f;
		}

		currentAngle += jitterBase * jitterIntensity;
	}

	float cLen = 14.0f;
	float cAngle = currentAngle + DirectX::XM_PI;
	Engine::Renderer::SdfUIDesc counter;
	counter.centerPx = {gaugeX + std::cos(cAngle) * (cLen * 0.5f), gaugeY + std::sin(cAngle) * (cLen * 0.5f)};
	counter.sizePx = {cLen, 4.5f};
	counter.shape = 0;
	counter.rotateRad = -cAngle;
	counter.color = {0.12f, 0.1f, 0.06f, 1.0f};
	counter.fill = 1.0f;
	renderer->DrawSDFUI(counter);

	float nLen = R * 0.9f;
	Engine::Renderer::SdfUIDesc needle;
	needle.centerPx = {gaugeX + std::cos(currentAngle) * (nLen * 0.5f), gaugeY + std::sin(currentAngle) * (nLen * 0.5f)};
	needle.sizePx = {nLen, 3.2f};
	needle.shape = 0;
	needle.rotateRad = -currentAngle;
	needle.color = isRecharging_ ? Engine::Vector4{0.9f, 0.1f, 0.1f, 1.0f} : Engine::Vector4{0.12f, 0.1f, 0.06f, 1.0f};
	needle.fill = 1.0f;
	renderer->DrawSDFUI(needle);

	renderer->DrawSDFUI({
	    {gaugeX, gaugeY},
        {12, 12},
        0, 0, {0.22f, 0.16f, 0.06f, 1.0f},
        1, 0, 0, 1
    });
	renderer->DrawSDFUI({
	    {gaugeX, gaugeY},
        {8, 8},
        0, 0, {0.6f, 0.5f, 0.25f, 1.0f},
        1, 0, 0, 1
    });

	// ===== 9. 特殊演出 =====
	if (isSkillActive_) {
		float p = std::sin(skillDuration_ * 12.0f) * 0.5f + 0.5f;
		renderer->DrawSDFUI({
		    {gaugeX, gaugeY},
            {R + 6, R + 6},
            2.0f, 10.0f, {0.2f, 0.8f, 1.0f, 0.2f + p * 0.4f},
            1, 0, 0
        });
		renderer->DrawString("OVERCLOCK", gaugeX - 55, gaugeY + R + 15, 0.4f, {0.3f, 0.9f, 1.0f, 1.0f});
	} else if (isRecharging_) {
		float b = std::sin(rechargeTimer_ * 10.0f) * 0.5f + 0.5f;
		renderer->DrawString("RECHARGING", gaugeX - 55, gaugeY + R + 15, 0.4f, {1.0f, 0.2f, 0.1f, 0.5f + b * 0.5f});
	}

	// ===== 10. 飛行圧力計 (Gunモードのみ) =====
	if (playerType_ == PlayerType::Gun) {
		float flightGaugeX = gaugeX - 110.0f; // メイン圧力計の左側
		float flightGaugeY = gaugeY + 15.0f;  // 少し下
		float fR = 45.0f;                     // 少し小さめ
		float fRatio = std::clamp(flightPressure_ / maxFlightPressure_, 0.0f, 1.0f);

		// 背景
		renderer->DrawSDFUI({
		    {flightGaugeX, flightGaugeY},
            {fR + 4, fR + 4},
            0, 0, {0.1f, 0.2f, 0.3f, 1.0f},
            1, 0, 0, 1
        });
		renderer->DrawSDFUI({
		    {flightGaugeX, flightGaugeY},
            {fR - 2, fR - 2},
            0, 0, {0.85f, 0.9f, 0.95f, 1.0f},
            1, 0, 0, 1
        });

		// 針の微細な振動（飛行中のみ）
		float fAngle = startAngle + fRatio * totalAngle;
		if (isFlying_) {
			float time = (float)GetTickCount64() * 0.001f;
			fAngle += std::sin(time * 80.0f) * 0.02f;
		}

		float fLen = fR * 0.85f;
		Engine::Renderer::SdfUIDesc fNeedle;
		fNeedle.centerPx = {flightGaugeX + std::cos(fAngle) * (fLen * 0.5f), flightGaugeY + std::sin(fAngle) * (fLen * 0.5f)};
		fNeedle.sizePx = {fLen, 2.5f};
		fNeedle.shape = 0;
		fNeedle.rotateRad = -fAngle;
		fNeedle.color = {0.1f, 0.2f, 0.5f, 1.0f};
		fNeedle.fill = 1.0f;
		renderer->DrawSDFUI(fNeedle);

		// センターピン
		renderer->DrawSDFUI({
		    {flightGaugeX, flightGaugeY},
            {8, 8},
            0, 0, {0.1f, 0.2f, 0.3f, 1.0f},
            1, 0, 0, 1
        });

		// ラベル
		float fTitleTw = renderer->MeasureTextWidth("FLIGHT", 0.2f);
		renderer->DrawString("FLIGHT", flightGaugeX - fTitleTw * 0.5f, flightGaugeY + fR * 0.2f, 0.2f, {0.1f, 0.2f, 0.3f, 0.8f});
	}

	// ===== 11. スキルクールタイム/バフ円形UI =====
	{
		float skillGaugeX = gaugeX - 50.0f; // 圧力計の左上
		float skillGaugeY = gaugeY - 90.0f;
		float sR = 30.0f;
		
		float skillRatio = 0.0f;
		Engine::Vector4 skillColor = {0.5f, 0.5f, 0.5f, 1.0f};
		float maxCd = (playerType_ == PlayerType::Gun) ? (SKILL_COOLDOWN_TIME / playerGunSkillCooldownRate_) : (20.0f / playerSwordSkillCooldownRate_);

		if (isSkillActive_) {
			skillRatio = skillDuration_ / SKILL_MAX_DURATION;
			skillColor = {0.3f, 0.9f, 1.0f, 1.0f}; // バフ中：水色
		} else if (skillCooldown_ > 0.0f) {
			skillRatio = 1.0f - (skillCooldown_ / maxCd); // クールダウン中は増えていく
			skillColor = {0.5f, 0.2f, 0.2f, 0.8f}; // クールダウン中：暗い赤
		} else {
			skillRatio = 1.0f;
			skillColor = {0.9f, 0.8f, 0.2f, 1.0f}; // 使用可能：黄色
		}

		// 背景
		renderer->DrawSDFUI({
			{skillGaugeX, skillGaugeY}, {sR + 4, sR + 4},
			0, 0, {0.1f, 0.1f, 0.15f, 0.8f}, 1, 0, 0, 0, 1.0f, 1.0f, false
		});

		// ゲージ
		renderer->DrawSDFUI({
			{skillGaugeX, skillGaugeY}, {sR, sR},
			0, (skillRatio >= 1.0f) ? 3.0f : 0.0f, skillColor,
			1, 0, 0, 0, skillRatio, 1.0f, false // shape = 1 (Circle)
		});

		// ラベル "E" または "SKILL"
		float tw = renderer->MeasureTextWidth("E", 0.35f);
		renderer->DrawString("E", skillGaugeX - tw * 0.5f, skillGaugeY - 10.0f, 0.35f, {1, 1, 1, 0.9f});
	}
}

void PlayerScript::DrawReticle(entt::entity playerEntity, GameScene* scene) {
	auto* renderer = Engine::Renderer::GetInstance();
	if (!renderer)
		return;

	// ===== 1. 表示位置の決定 =====
	float cx = (float)Engine::WindowDX::kW * 0.5f;
	float cy = (float)Engine::WindowDX::kH * 0.5f;
	bool hasTarget = false;
	Engine::Vector4 reticleColor = {0.85f, 0.75f, 0.4f, 0.8f}; // 基本は真鍮ゴールド（不透明度80%）

	if (scene->GetRegistry().all_of<CameraTargetComponent>(playerEntity)) {
		auto& ct = scene->GetRegistry().get<CameraTargetComponent>(playerEntity);
		entt::entity target = isFlying_ ? lockedEnemy_ : ct.lockedTarget;
		if (target != entt::null && scene->GetRegistry().valid(target)) {
			auto& eTc = scene->GetRegistry().get<TransformComponent>(target);
			DirectX::XMFLOAT3 targetWorldPos = eTc.translate;
			targetWorldPos.y += 1.0f; // 敵の胸あたり

			auto* camera = &scene->GetCamera();
			if (camera) {
				float sx = 0.0f, sy = 0.0f;
				if (UISystem::WorldToScreen(targetWorldPos, *camera, sx, sy)) {
					cx = sx;
					cy = sy;
					hasTarget = true;
					reticleColor = {0.95f, 0.55f, 0.15f, 0.95f}; // ロックオン時は少し朱色っぽく輝かせる
				}
			}
		}
	}

	// ===== 2. アニメーションとダイナミック状態の計算 =====
	float baseR = 24.0f;

	// 反動による一時的な拡大 (射撃後クールダウン)
	float recoilOffset = 0.0f;
	if (gunShootTimer_ > 0.0f) {
		recoilOffset = (gunShootTimer_ / (isFlying_ ? 0.25f : 0.3f)) * 26.0f;
	}
	float currentR = baseR + recoilOffset;

	// プルプルした振動（ジッター）の計算
	float jitterX = 0.0f;
	float jitterY = 0.0f;
	{
		float jitterIntensity = 0.0f;

		if (isRecharging_) {
			jitterIntensity = 3.5f; // オーバーヒート時は激しくガタガタ
		} else if (isCharging_) {
			float cRatio = std::clamp<float>(chargeTime_ / CHARGE_TIME_MAX, 0.0f, 1.0f);
			jitterIntensity = 1.0f + cRatio * 5.0f;
		} else if (gunShootTimer_ > 0.0f) {
			jitterIntensity = (gunShootTimer_ / 0.3f) * 4.0f;
		}

		if (jitterIntensity > 0.0f) {
			jitterX = (float(rand() % 100) / 100.0f - 0.5f) * jitterIntensity;
			jitterY = (float(rand() % 100) / 100.0f - 0.5f) * jitterIntensity;
		}
	}

	float rx = cx + jitterX;
	float ry = cy + jitterY;

	// ===== 3. リチャージ（オーバーヒート）時の警告表示 =====
	if (isRecharging_) {
		float b = std::sin((float)GetTickCount64() * 0.015f) * 0.5f + 0.5f;
		Engine::Vector4 warnColor = {1.0f, 0.2f, 0.1f, 0.4f + b * 0.6f};

		renderer->DrawString("OVERHEAT", rx - 38.0f, ry - currentR - 22.0f, 0.35f, warnColor);
		renderer->DrawString("RECHARGING", rx - 48.0f, ry + currentR + 10.0f, 0.35f, warnColor);

		reticleColor = {1.0f, 0.15f, 0.1f, 0.75f};
	}

	// ===== 4. スチームパンク風SDFUIレティクル描画 =====

	// (A) 真鍮の外環（極細ベゼル）
	{
		Engine::Renderer::SdfUIDesc desc{};
		desc.centerPx = {rx, ry};
		desc.sizePx = {currentR + 3.0f, currentR + 3.0f};
		desc.lineWidth = 2.5f; // ★太さを設定
		desc.glow = 1.0f;
		desc.color = reticleColor;
		desc.shape = 1; // ★Circle
		desc.round = 0.0f;
		desc.inner = 0.0f;
		desc.rotateRad = 0.0f;
		desc.progress = -1.0f; // プログレスバー機能無効
		desc.fill = 0.0f;      // ★Outline(スカスカ)
		renderer->DrawSDFUI(desc);
	}

	{
		Engine::Renderer::SdfUIDesc desc{};
		desc.centerPx = {rx, ry};
		desc.sizePx = {currentR - 1.0f, currentR - 1.0f};
		desc.lineWidth = 1.5f; // ★太さを設定
		desc.glow = 0.5f;
		desc.color = {reticleColor.x, reticleColor.y, reticleColor.z, reticleColor.w * 0.9f};
		desc.shape = 1; // ★Circle
		desc.round = 0.0f;
		desc.inner = 0.0f;
		desc.rotateRad = 0.0f;
		desc.progress = -1.0f; // プログレスバー機能無効
		desc.fill = 0.0f;      // ★Outline
		renderer->DrawSDFUI(desc);
	}

	// (B) 4点のリベット（極小ドット）
	for (int i = 0; i < 4; ++i) {
		float angle = DirectX::XM_PIDIV2 * (float)i;
		float dotR = currentR + 1.0f;
		float dx = rx + std::cos(angle) * dotR;
		float dy = ry + std::sin(angle) * dotR;

		Engine::Renderer::SdfUIDesc desc{};
		desc.centerPx = {dx, dy};
		desc.sizePx = {3.0f, 3.0f};
		desc.lineWidth = 0.0f;
		desc.glow = 1.0f;
		desc.color = reticleColor;
		desc.shape = 1; // Circle
		desc.round = 0.0f;
		desc.inner = 0.0f;
		desc.rotateRad = 0.0f;
		desc.progress = -1.0f; // プログレスバー機能無効
		desc.fill = 1.0f;      // 塗りつぶし
		renderer->DrawSDFUI(desc);
	}

	// (C) チャージメーター（円周に沿う進捗ゲージ）
	if (isCharging_) {
		float cRatio = std::clamp<float>(chargeTime_ / CHARGE_TIME_MAX, 0.0f, 1.0f);
		Engine::Vector4 chargeColor = {1.0f, 0.45f + cRatio * 0.55f, 0.1f, 0.9f}; // オレンジから黄色に変化

		Engine::Renderer::SdfUIDesc desc{};
		desc.centerPx = {rx, ry};
		desc.sizePx = {currentR + 6.0f, currentR + 6.0f};
		desc.lineWidth = 3.0f; // ★太さを設定
		desc.glow = 3.0f;
		desc.color = chargeColor;
		desc.shape = 1; // ★Circle
		desc.round = 0.0f;
		desc.inner = 0.0f;
		desc.rotateRad = 0.0f;
		desc.progress = cRatio; // チャージ進捗を適用！
		desc.fill = 0.0f;       // ★Outline
		renderer->DrawSDFUI(desc);
	}

	// (D) 内側の精密十字線（中央から少し離れた4本の短い線）
	float crossLen = 8.0f;
	float crossOffset = 5.0f + recoilOffset * 0.2f;
	float crossThickness = 2.5f;

	// 上
	{
		Engine::Renderer::SdfUIDesc desc{};
		desc.centerPx = {rx, ry - crossOffset - crossLen * 0.5f};
		desc.sizePx = {crossThickness, crossLen};
		desc.lineWidth = 1.0f;
		desc.glow = 0.0f;
		desc.color = reticleColor;
		desc.shape = 0; // Square
		desc.round = 0.0f;
		desc.inner = 0.0f;
		desc.rotateRad = 0.0f;
		desc.progress = -1.0f; // プログレスバー機能無効
		desc.fill = 1.0f;      // 塗りつぶし
		renderer->DrawSDFUI(desc);
	}
	// 下
	{
		Engine::Renderer::SdfUIDesc desc{};
		desc.centerPx = {rx, ry + crossOffset + crossLen * 0.5f};
		desc.sizePx = {crossThickness, crossLen};
		desc.lineWidth = 1.0f;
		desc.glow = 0.0f;
		desc.color = reticleColor;
		desc.shape = 0; // Square
		desc.round = 0.0f;
		desc.inner = 0.0f;
		desc.rotateRad = 0.0f;
		desc.progress = -1.0f; // プログレスバー機能無効
		desc.fill = 1.0f;      // 塗りつぶし
		renderer->DrawSDFUI(desc);
	}
	// 左
	{
		Engine::Renderer::SdfUIDesc desc{};
		desc.centerPx = {rx - crossOffset - crossLen * 0.5f, ry};
		desc.sizePx = {crossLen, crossThickness};
		desc.lineWidth = 1.0f;
		desc.glow = 0.0f;
		desc.color = reticleColor;
		desc.shape = 0; // Square
		desc.round = 0.0f;
		desc.inner = 0.0f;
		desc.rotateRad = 0.0f;
		desc.progress = -1.0f; // プログレスバー機能無効
		desc.fill = 1.0f;      // 塗りつぶし
		renderer->DrawSDFUI(desc);
	}
	// 右
	{
		Engine::Renderer::SdfUIDesc desc{};
		desc.centerPx = {rx + crossOffset + crossLen * 0.5f, ry};
		desc.sizePx = {crossLen, crossThickness};
		desc.lineWidth = 1.0f;
		desc.glow = 0.0f;
		desc.color = reticleColor;
		desc.shape = 0; // Square
		desc.round = 0.0f;
		desc.inner = 0.0f;
		desc.rotateRad = 0.0f;
		desc.progress = -1.0f; // プログレスバー機能無効
		desc.fill = 1.0f;      // 塗りつぶし
		renderer->DrawSDFUI(desc);
	}

	// (E) センタードット
	{
		Engine::Renderer::SdfUIDesc desc{};
		desc.centerPx = {rx, ry};
		desc.sizePx = {4.0f, 4.0f};
		desc.lineWidth = 1.0f;
		desc.glow = 2.0f;
		desc.color = reticleColor;
		desc.shape = 1; // Circle
		desc.round = 0.0f;
		desc.inner = 0.0f;
		desc.rotateRad = 0.0f;
		desc.progress = -1.0f; // プログレスバー機能無効
		desc.fill = 1.0f;      // 塗りつぶし
		renderer->DrawSDFUI(desc);
	}
}

void PlayerScript::ShootGun(entt::entity entity, GameScene* scene) {
	float baseDamage = 15.0f; // ★復元: 8.0f -> 15.0f
	float damage = baseDamage * playerGunAttackPowerRate_;

	if (isSkillActive_) {
		damage *= SKILL_DAMAGE_MULTIPLIER;
		damage *= playerGunSkillAttackPowerRate_;
	}
	SpawnBullet(entity, scene, 0.0f, 0.0f, damage, 2.0f, isSkillActive_, false);

	// ★空間割れマズルエフェクト（銃口の前に小規模なガラス割れを生成）
	auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
	entt::entity gun = scene->FindObjectByName(gunName_);
	DirectX::XMFLOAT3 muzzlePos = pTc.translate;
	muzzlePos.y += 1.0f;
	float fwdX = std::sin(pTc.rotate.y);
	float fwdY = 0.0f;
	float fwdZ = std::cos(pTc.rotate.y);

	auto* camera = &scene->GetCamera();
	if (camera && playerType_ == PlayerType::Gun) {
		Engine::Vector3 camRot = camera->GetRotation();
		fwdX = std::sin(camRot.y) * std::cos(camRot.x);
		fwdY = -std::sin(camRot.x);
		fwdZ = std::cos(camRot.y) * std::cos(camRot.x);
	}

	if (gun != entt::null) {
		Engine::Matrix4x4 gunWorld = scene->GetWorldMatrix((int)gun);
		DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gunWorld));
		// 銃のモデルの先端（ローカルZ前方）を計算
		DirectX::XMVECTOR tip = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, 0.6f, 1), m);
		DirectX::XMStoreFloat3(&muzzlePos, tip);
	} else {
		muzzlePos.x += fwdX * 1.5f;
		muzzlePos.y += fwdY * 1.5f;
		muzzlePos.z += fwdZ * 1.5f;
	}

	entt::entity muzzleVfx = scene->CreateEntity("MuzzleShatter_VFX");
	auto& mTc = scene->GetRegistry().get<TransformComponent>(muzzleVfx);
	mTc.translate = muzzlePos;
	scene->SetTag(muzzleVfx, TagType::VFX);

	auto& mvc = scene->GetRegistry().emplace<VariableComponent>(muzzleVfx);
	mvc.SetValue("NormalX", fwdX);
	mvc.SetValue("NormalY", fwdY);
	mvc.SetValue("NormalZ", fwdZ);
	mvc.SetValue("Radius", 3.0f);        // 拡大
	mvc.SetValue("Duration", 0.5f);      // 素早く消える
	mvc.SetValue("ScatterMode", 0.0f);   // マズルモード
	mvc.SetValue("ScatterDelay", 0.0f);  // 即座に噴射
	mvc.SetValue("ScatterSpeed", 12.0f); // 勢いよく

	auto& msc = scene->GetRegistry().emplace<ScriptComponent>(muzzleVfx);
	msc.scripts.push_back({"SpaceShatterScript", "", nullptr});
}

void PlayerScript::SpawnBullet(entt::entity entity, GameScene* scene, float spreadYaw, float spreadPitch, float damage, float lifeTime, bool enhanced, bool explode) {
	if (!scene->GetRegistry().all_of<TransformComponent>(entity))
		return;
	auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);

	entt::entity bullet = scene->GetRegistry().create();
	scene->GetRegistry().emplace<TagComponent>(bullet).tag = TagType::Bullet;

	auto& bTc = scene->GetRegistry().emplace<TransformComponent>(bullet);
	bTc.translate = pTc.translate;
	bTc.translate.y += 1.0f; // 腰か胸の高さ

	bTc.rotate = pTc.rotate;
	bTc.rotate.y += spreadYaw;
	bTc.rotate.x += spreadPitch;

	// ★追加: ターゲットロック中のオートエイム（地上・空中問わずロック中ならそちらを向く）
	entt::entity targetToAim = entt::null;
	if (!isFlying_ && scene->GetRegistry().all_of<CameraTargetComponent>(entity)) {
		auto& ct = scene->GetRegistry().get<CameraTargetComponent>(entity);
		targetToAim = ct.lockedTarget;
	} else if (isFlying_) {
		targetToAim = lockedEnemy_;
	}

	if (targetToAim != entt::null && scene->GetRegistry().valid(targetToAim)) {
		auto& eTc = scene->GetRegistry().get<TransformComponent>(targetToAim);
		float dx = eTc.translate.x - bTc.translate.x;
		float dy = (eTc.translate.y + 1.0f) - bTc.translate.y; // 敵の胸を狙う
		float dz = eTc.translate.z - bTc.translate.z;

		float distXZ = std::sqrt(dx * dx + dz * dz);
		float yaw = std::atan2(dx, dz);
		float pitch = std::atan2(-dy, distXZ);

		bTc.rotate.y = yaw + spreadYaw;
		bTc.rotate.x = pitch + spreadPitch;
		bTc.rotate.z = 0.0f;
	}

	// ★追加・変更: ロックオン中または飛行中のロック対象に対して追尾弾にする
	entt::entity homingTarget = entt::null;
	if (scene->GetRegistry().all_of<CameraTargetComponent>(entity)) {
		auto& ct = scene->GetRegistry().get<CameraTargetComponent>(entity);
		if (ct.lockedTarget != entt::null && scene->GetRegistry().valid(ct.lockedTarget)) {
			homingTarget = ct.lockedTarget;
		}
	}
	if (isFlying_ && lockedEnemy_ != entt::null && scene->GetRegistry().valid(lockedEnemy_)) {
		homingTarget = lockedEnemy_;
	}

	if (homingTarget != entt::null) {
		auto& bVc = scene->GetRegistry().get_or_emplace<VariableComponent>(bullet);
		bVc.SetValue("HasTarget", 1.0f);
		bVc.SetValue("IsPlayerAttack", 1.0f); // ★追加: プレイヤー攻撃フラグ

		uint32_t targetId = static_cast<uint32_t>(homingTarget);
		bVc.SetValue("TargetHigh", static_cast<float>(targetId >> 16));
		bVc.SetValue("TargetLow", static_cast<float>(targetId & 0xFFFF));

		bVc.SetValue("Speed", enhanced ? 120.0f : 80.0f);
	}

	float cosX = std::cos(bTc.rotate.x);
	float moveX = std::sin(bTc.rotate.y) * cosX;
	float moveZ = std::cos(bTc.rotate.y) * cosX;
	float moveY = -std::sin(bTc.rotate.x);

	// ★銃の先端から弾を出す
	entt::entity gun = scene->FindObjectByName(gunName_);
	if (gun != entt::null) {
		Engine::Matrix4x4 gunWorld = scene->GetWorldMatrix((int)gun);
		DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gunWorld));
		DirectX::XMVECTOR tip = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, 0.6f, 1), m);
		DirectX::XMStoreFloat3(&bTc.translate, tip);
	} else {
		bTc.translate.x += moveX * 2.0f;
		bTc.translate.y += moveY * 2.0f;
		bTc.translate.z += moveZ * 2.0f;
	}

	// ★変更: レティクル（カメラの中心）へ正確に弾を飛ばすためのレイキャスト判定
	auto* camera = &scene->GetCamera();
	if (camera && playerType_ == PlayerType::Gun && !isFlying_ && targetToAim == entt::null) {
		Engine::Vector3 camRot = camera->GetRotation();
		Engine::Vector3 camPos = camera->GetPosition();

		// カメラの前方ベクトルを計算
		float cy = std::cos(camRot.y);
		float sy = std::sin(camRot.y);
		float cp = std::cos(camRot.x);
		float sp = std::sin(camRot.x);
		Engine::Vector3 camFwd = {sy * cp, -sp, cy * cp};

		// レイキャストで目標地点を特定 (最大200m)
		float hitDist = 0.0f;
		Engine::Vector3 targetPoint;
		// ※自分自身をRayCastから除外
		if (scene->RayCast(camPos, camFwd, 200.0f, static_cast<uint32_t>(entity), hitDist)) {
			targetPoint.x = camPos.x + camFwd.x * hitDist;
			targetPoint.y = camPos.y + camFwd.y * hitDist;
			targetPoint.z = camPos.z + camFwd.z * hitDist;
		} else {
			// 何も当たらなければ200m先をターゲットとする
			targetPoint.x = camPos.x + camFwd.x * 200.0f;
			targetPoint.y = camPos.y + camFwd.y * 200.0f;
			targetPoint.z = camPos.z + camFwd.z * 200.0f;
		}

		// 銃口からターゲットへの方向ベクトルを計算
		float dx = targetPoint.x - bTc.translate.x;
		float dy = targetPoint.y - bTc.translate.y;
		float dz = targetPoint.z - bTc.translate.z;

		float distXZ = std::sqrt(dx * dx + dz * dz);
		float yaw = std::atan2(dx, dz);
		float pitch = std::atan2(-dy, distXZ);

		bTc.rotate.y = yaw + spreadYaw;
		bTc.rotate.x = pitch + spreadPitch;

		moveX = std::sin(bTc.rotate.y) * std::cos(bTc.rotate.x);
		moveY = -std::sin(bTc.rotate.x);
		moveZ = std::cos(bTc.rotate.y) * std::cos(bTc.rotate.x);
	} else if (gun != entt::null) {
		Engine::Matrix4x4 gunWorld = scene->GetWorldMatrix((int)gun);
		DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gunWorld));
		DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 0, 1, 0), m));
		moveX = DirectX::XMVectorGetX(forward);
		moveY = DirectX::XMVectorGetY(forward);
		moveZ = DirectX::XMVectorGetZ(forward);

		float fYaw = std::atan2(moveX, moveZ);
		float fPitch = -std::asin(std::max(-1.0f, std::min(1.0f, moveY)));
		bTc.rotate.y = fYaw + spreadYaw;
		bTc.rotate.x = fPitch + spreadPitch;
	}

	bTc.scale = {0.2f, 0.2f, 0.6f};

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		auto& mr = scene->GetRegistry().emplace<MeshRendererComponent>(bullet);
		mr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		mr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		mr.color = enhanced ? DirectX::XMFLOAT4{0.4f, 0.9f, 1.0f, 1.0f} : DirectX::XMFLOAT4{1.0f, 0.8f, 0.2f, 1.0f};
	}

	auto& hb = scene->GetRegistry().emplace<HitboxComponent>(bullet);
	hb.isActive = true;
	hb.damage = damage;
	hb.tag = TagType::Bullet;
	hb.size = {1.0f, 1.0f, 1.0f};

	auto& sc = scene->GetRegistry().emplace<ScriptComponent>(bullet);
	sc.scripts.push_back({"BulletScript", "", nullptr});

	// ★修正: 先ほど get_or_emplace されている可能性があるため、ここも get_or_emplace に変更する
	auto& vc = scene->GetRegistry().get_or_emplace<VariableComponent>(bullet);
	vc.SetValue("MaxLifeTime", lifeTime);
	vc.SetValue("IsPlayerAttack", 1.0f); // ★追加: プレイヤーの攻撃フラグ
	if (explode) {
		vc.SetValue("Enhanced", 1.0f);
	}

	// ★強化: マズルでの空間割れエフェクト（着弾時と同等の迫力へ）
	{
		entt::entity muzzleShatter = scene->CreateEntity("MuzzleShatter_VFX");
		auto& msTc = scene->GetRegistry().get<TransformComponent>(muzzleShatter);
		msTc.translate = bTc.translate; // 銃口位置

		auto& msVc = scene->GetRegistry().emplace<VariableComponent>(muzzleShatter);
		msVc.SetValue("NormalX", moveX);
		msVc.SetValue("NormalY", moveY);
		msVc.SetValue("NormalZ", moveZ);
		msVc.SetValue("Radius", 2.8f); // ★スチームの迫力を出すため拡大
		msVc.SetValue("Count", 35.0f);
		msVc.SetValue("Duration", 0.6f);      // ★少し長めに残る
		msVc.SetValue("ScatterMode", 0.0f);   // ★射撃（マズル）モードに設定
		msVc.SetValue("ScatterDelay", 0.15f); // ★綺麗な状態を長く(0.15秒)
		msVc.SetValue("ScatterSpeed", 1.5f);  // ★飛びすぎないように調整

		auto& msSc = scene->GetRegistry().emplace<ScriptComponent>(muzzleShatter);
		msSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
	}

	// ★追加: マズルフラッシュ生成（疑似ライト）
	MuzzleFlash flash;
	flash.pos = bTc.translate; // 弾のスポーン位置（銃口付近）
	flash.life = 0.06f;        // 一瞬だけ光る
	flash.maxLife = 0.06f;
	muzzleFlashes_.push_back(flash);
	if (muzzleFlashes_.size() > 30)
		muzzleFlashes_.pop_front();

	// ★追加: 薬莢生成
	ShellCasing shell;
	shell.pos = pTc.translate;
	shell.pos.y += 1.2f; // 胸の高さ
	// プレイヤーの右側へ、上に小さく弾く
	float shellRightX = std::cos(pTc.rotate.y);
	float shellRightZ = -std::sin(pTc.rotate.y);
	shell.velocity.x = shellRightX * 3.0f + ((rand() % 100 / 100.0f - 0.5f) * 1.0f);
	shell.velocity.y = 3.0f + (rand() % 100 / 100.0f) * 2.0f;
	shell.velocity.z = shellRightZ * 3.0f + ((rand() % 100 / 100.0f - 0.5f) * 1.0f);
	shell.life = 0.6f;
	shellCasings_.push_back(shell);
	if (shellCasings_.size() > 30)
		shellCasings_.pop_front();
}

void PlayerScript::SpawnCrystalBurst(const DirectX::XMFLOAT3& pos, int count, bool enhanced) {
	for (int i = 0; i < count; ++i) {
		CrystalParticle cp;
		cp.pos = pos;
		float angle = (rand() % 360) * 3.14159f / 180.0f;
		float upAngle = (rand() % 100 / 100.0f - 0.3f) * 3.14159f * 0.5f;
		float speed = 3.0f + (rand() % 100 / 100.0f) * 5.0f;
		cp.velocity.x = std::cos(angle) * std::cos(upAngle) * speed;
		cp.velocity.y = std::sin(upAngle) * speed * 0.5f + 1.5f;
		cp.velocity.z = std::sin(angle) * std::cos(upAngle) * speed;
		cp.life = 0.3f + (rand() % 100 / 100.0f) * 0.4f;
		cp.maxLife = cp.life;
		cp.size = 0.05f + (rand() % 100 / 100.0f) * 0.1f;
		cp.rotSpeed = (rand() % 100 / 100.0f - 0.5f) * 20.0f;
		cp.rot = 0.0f;
		if (enhanced) {
			cp.color = {0.3f + (rand() % 100 / 100.0f) * 0.2f, 0.8f + (rand() % 100 / 100.0f) * 0.2f, 1.0f, 1.0f};
			cp.size *= 1.5f;
		} else {
			cp.color = {0.5f + (rand() % 100 / 100.0f) * 0.3f, 0.7f + (rand() % 100 / 100.0f) * 0.3f, 0.9f + (rand() % 100 / 100.0f) * 0.1f, 1.0f};
		}
		crystalParticles_.push_back(cp);
		if (crystalParticles_.size() > 100)
			crystalParticles_.pop_front();
	}
}

void PlayerScript::SwitchPlayerType(entt::entity /*entity*/, GameScene* scene) {
	if (playerType_ == PlayerType::Sword) {
		playerType_ = PlayerType::Gun;
		std::cout << "Switched to Gun Mode\n";
	} else {
		playerType_ = PlayerType::Sword;
		std::cout << "Switched to Sword Mode\n";
	}

	entt::entity sword = scene->FindObjectByName(swordName_);
	entt::entity gun = scene->FindObjectByName(gunName_);

	if (sword != entt::null && scene->GetRegistry().all_of<MeshRendererComponent>(sword)) {
		scene->GetRegistry().get<MeshRendererComponent>(sword).enabled = (playerType_ == PlayerType::Sword);
		if (playerType_ == PlayerType::Gun && scene->GetRegistry().all_of<HitboxComponent>(sword)) {
			scene->GetRegistry().get<HitboxComponent>(sword).isActive = false;
		}
	}
	if (gun != entt::null && scene->GetRegistry().all_of<MeshRendererComponent>(gun)) {
		scene->GetRegistry().get<MeshRendererComponent>(gun).enabled = (playerType_ == PlayerType::Gun);
	}
}

void PlayerScript::ExecuteSkill(entt::entity entity, GameScene* scene) {
	if (skillCooldown_ > 0.0f)
		return;

	if (playerType_ == PlayerType::Sword) {
		skillCooldown_ = 20.0f / playerSwordSkillCooldownRate_;

		auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
		entt::entity wave = scene->GetRegistry().create();
		scene->GetRegistry().emplace<TagComponent>(wave).tag = TagType::PlayerSword; // ★プレイヤーの剣攻撃として判定

		auto& wTc = scene->GetRegistry().emplace<TransformComponent>(wave);
		wTc.translate = pTc.translate;
		wTc.translate.y += 1.0f;
		wTc.rotate = pTc.rotate;

		float moveX = std::sin(wTc.rotate.y);
		float moveZ = std::cos(wTc.rotate.y);
		wTc.translate.x += moveX * 2.5f;
		wTc.translate.z += moveZ * 2.5f;

		wTc.scale = {12.0f, 0.2f, 2.0f}; // 超横広な衝撃波（次元斬）

		auto* renderer = scene->GetRenderer();
		if (renderer) {
			auto& mr = scene->GetRegistry().emplace<MeshRendererComponent>(wave);
			mr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
			mr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
			mr.color = {0.1f, 0.8f, 1.0f, 0.8f}; // 青白い光
		}

		auto& hb = scene->GetRegistry().emplace<HitboxComponent>(wave);
		hb.isActive = true;
		hb.damage = 60.0f;             // ★弱体化: 150.0f -> 60.0f (次元斬スキル用)
		hb.tag = TagType::PlayerSword; // ★プレイヤーの剣攻撃として判定
		hb.size = {12.0f, 1.0f, 2.0f};

		auto& sc = scene->GetRegistry().emplace<ScriptComponent>(wave);
		sc.scripts.push_back({"BulletScript", "", nullptr});

		entt::entity sword = scene->FindObjectByName(swordName_);
		if (sword != entt::null && scene->GetRegistry().all_of<MotionComponent>(sword)) {
			auto& motion = scene->GetRegistry().get<MotionComponent>(sword);
			motion.PlayAnimation("Combo2"); // 振り下ろすモーション
			isAttacking_ = true;
		}
		std::cout << "Executed Sword Skill: Dimension Wave\n";
	} else {
		// ★銃スキル: 移動速度UP + ダメージUP + エフェクト強化バフ
		skillCooldown_ = SKILL_COOLDOWN_TIME / playerGunSkillCooldownRate_;
		isSkillActive_ = true;
		skillDuration_ = SKILL_MAX_DURATION;
		steamPressure_ = maxSteam; // ★オーバークロック発動時に圧力を100まで戻す
		isRecharging_ = false;     // リチャージ状態も強制解除
		std::cout << "Gun Skill Activated: Crystal Enhancement!\n";
	}
}

void PlayerScript::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::SeparatorText("Player Debug");
	ImGui::Text("Level: %d", level_);
	ImGui::Text("Experience: %.1f / %.1f", experience_, nextExperience_);
	ImGui::Text("Subscribe Count: %d", debugSubscribeCount_);
	ImGui::Text("Receive Count: %d", debugReceiveCount_);
	ImGui::Text("Last Value: %.2f", debugLastValue_);
#endif
}
void PlayerScript::ApplySkillEffects(entt::entity entity, GameScene* scene) {
	(void)entity;
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	entt::entity gm = entt::null;
	auto viewScript = registry.view<ScriptComponent>();

	for (entt::entity e : viewScript) {
		const ScriptComponent& sc = viewScript.get<ScriptComponent>(e);

		for (const auto& instance : sc.scripts) {
			if (instance.scriptPath == "PhaseSystemScript" || instance.scriptPath == "TutorialScript") {
				gm = e;
				break;
			}
		}

		if (gm != entt::null) {
			break;
		}
	}

	if (gm == entt::null) {
		return;
	}
	playerMoveSpeedRate_ = GetVar(gm, scene, "PlayerMoveSpeedRate", 1.0f); // ★移動速度の変数を取得

	playerSwordAttackSpeedRate_ = GetVar(gm, scene, "PlayerSwordAttackSpeedRate", 1.0f); // ★剣攻撃速度の変数を取得
	playerGunAttackSpeedRate_ = GetVar(gm, scene, "PlayerGunAttackSpeedRate", 1.0f);     // ★銃攻撃速度の変数を取得

	playerSwordAttackPowerRate_ = GetVar(gm, scene, "PlayerSwordAttackPowerRate", 1.0f); // 攻撃力の変数を取得

	playerGunAttackPowerRate_ = GetVar(gm, scene, "PlayerGunAttackPowerRate", 1.0f); // 銃の攻撃力の変数を取得

	playerMaxSteamPressureRate_ = GetVar(gm, scene, "PlayerMaxSteamPressureRate", 1.0f); // スチーム圧力の変数を取得

	playerSwordSkillCooldownRate_ = GetVar(gm, scene, "PlayerSwordSkillCooldownRate", 1.0f); // クールダウンの変数を取得
	playerGunSkillCooldownRate_ = GetVar(gm, scene, "PlayerGunSkillCooldownRate", 1.0f);     // 銃スキルのクールダウン

	playerSwordSkillAttackPowerRate_ = GetVar(gm, scene, "PlayerSwordSkillAttackPowerRate", 1.0f); // スキルの攻撃力の変数を取得
	playerGunSkillAttackPowerRate_ = GetVar(gm, scene, "PlayerGunSkillAttackPowerRate", 1.0f);

	playerBuffRangeRate_ = GetVar(gm, scene, "PlayerBuffRangeRate", 1.0f); // バフの範囲の変数を取得
}
void PlayerScript::DrawUI(entt::entity /*entity*/, GameScene* /*scene*/) {
	// ※注意: DrawUI は Renderer::EndFrame の後（ImGuiフェーズ）に呼び出されるため、
	// ここで renderer->DrawSDFUI や DrawString などを呼んでも次のフレームの先頭でクリアされてしまい描画されません。
	// ゲーム内のUI描画（SDFやテキストなど）は Update 内で行うように移動しました。
}

void PlayerScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PlayerScript);

} // namespace Game
