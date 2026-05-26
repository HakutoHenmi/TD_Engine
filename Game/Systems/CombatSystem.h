#include "ISystem.h"
#include <cmath>
#include "../Engine/Time/TimeManager.h" 
#include "../Engine/QuadTree.h"
#include "../Scripts/HitDistortionScript.h"
#include "../Scripts/TutorialScript.h"
#include "GameScene.h"

namespace Game {

class CombatSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// --- QuadTree の構築 (Hurtbox と BoxCollider を対象) ---
		::Engine::PhysicsQuadTree qt(-4000.0f, -4000.0f, 4000.0f, 4000.0f, 6, 10);
		
		m_hurters.clear();
		auto hurtboxView = registry.view<HurtboxComponent, TransformComponent>();
		for (auto entity : hurtboxView) {
			auto& hb = hurtboxView.get<HurtboxComponent>(entity);
			auto& tc = hurtboxView.get<TransformComponent>(entity);
			if (!hb.enabled) continue;

			::Engine::Matrix4x4 world = ctx.scene ? ctx.scene->GetWorldMatrix(static_cast<int>(entity)) : tc.ToMatrix();
			DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&world));
			
			// AABBを計算してQuadTreeに登録
			DirectX::XMVECTOR center = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&hb.center)), worldMat);
			DirectX::XMVECTOR scale, rot, trans;
			DirectX::XMMatrixDecompose(&scale, &rot, &trans, worldMat);
			float ex = hb.size.x * 0.5f * std::abs(DirectX::XMVectorGetX(scale));
			float ey = hb.size.y * 0.5f * std::abs(DirectX::XMVectorGetY(scale));
			float ez = hb.size.z * 0.5f * std::abs(DirectX::XMVectorGetZ(scale));

			float cx = DirectX::XMVectorGetX(center), cy = DirectX::XMVectorGetY(center), cz = DirectX::XMVectorGetZ(center);
			
			HurtDeviceInfo info;
			info.entity = entity;
			info.center = {cx, cy, cz};
			info.extents = {ex, ey, ez};
			
			DirectX::XMFLOAT4X4 wm;
			DirectX::XMStoreFloat4x4(&wm, worldMat);
			info.axes[0] = { wm._11, wm._12, wm._13 };
			info.axes[1] = { wm._21, wm._22, wm._23 };
			info.axes[2] = { wm._31, wm._32, wm._33 };

			// 正規化
			for(int i=0; i<3; ++i) {
				float len = std::sqrt(info.axes[i].x*info.axes[i].x + info.axes[i].y*info.axes[i].y + info.axes[i].z*info.axes[i].z);
				if(len > 0.0001f) { info.axes[i].x /= len; info.axes[i].y /= len; info.axes[i].z /= len; }
			}

			uint32_t idx = static_cast<uint32_t>(m_hurters.size());
			m_hurters.push_back(info);
			qt.Insert(idx, cx - ex, cz - ez, cx + ex, cz + ez);
		}

		// Hitboxを持つエンティティの更新
		auto attackerView = registry.view<HitboxComponent, TransformComponent>();
		for (auto attackerEntity : attackerView) {
			auto& hitbox = attackerView.get<HitboxComponent>(attackerEntity);
			if (!hitbox.enabled || !hitbox.isActive) continue;

			::Engine::Matrix4x4 aWorld = ctx.scene ? ctx.scene->GetWorldMatrix(static_cast<int>(attackerEntity)) : registry.get<TransformComponent>(attackerEntity).ToMatrix();
			DirectX::XMMATRIX aWorldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&aWorld));
			DirectX::XMVECTOR aCenter = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&hitbox.center)), aWorldMat);
			
			DirectX::XMVECTOR aAxes[3] = {
				DirectX::XMVector3Normalize(aWorldMat.r[0]),
				DirectX::XMVector3Normalize(aWorldMat.r[1]),
				DirectX::XMVector3Normalize(aWorldMat.r[2])
			};
			
			DirectX::XMVECTOR aScale, aRot, aTrans;
			DirectX::XMMatrixDecompose(&aScale, &aRot, &aTrans, aWorldMat);
			float aExtents[3] = {
				hitbox.size.x * 0.5f * std::abs(DirectX::XMVectorGetX(aScale)),
				hitbox.size.y * 0.5f * std::abs(DirectX::XMVectorGetY(aScale)),
				hitbox.size.z * 0.5f * std::abs(DirectX::XMVectorGetZ(aScale))
			};

			float acx = DirectX::XMVectorGetX(aCenter), acz = DirectX::XMVectorGetZ(aCenter);
			m_nearbyIndices.clear();
			qt.Query(acx - aExtents[0], acz - aExtents[2], acx + aExtents[0], acz + aExtents[2], m_nearbyIndices);

			for (uint32_t hurtIdx : m_nearbyIndices) {
				const auto& hurtInfo = m_hurters[hurtIdx];
				entt::entity defenderEntity = hurtInfo.entity;
				if (attackerEntity == defenderEntity) continue;

				DirectX::XMVECTOR dCenter = DirectX::XMLoadFloat3(&hurtInfo.center);
				DirectX::XMVECTOR dAxes[3] = {
					DirectX::XMLoadFloat3(&hurtInfo.axes[0]),
					DirectX::XMLoadFloat3(&hurtInfo.axes[1]),
					DirectX::XMLoadFloat3(&hurtInfo.axes[2])
				};
				float dExtents[3] = { hurtInfo.extents.x, hurtInfo.extents.y, hurtInfo.extents.z };

				if (CheckObbOverlap(aCenter, aAxes, aExtents, dCenter, dAxes, dExtents)) {
					TagType aTag = registry.all_of<TagComponent>(attackerEntity) ? registry.get<TagComponent>(attackerEntity).tag : TagType::Untagged;
					TagType dTag = registry.all_of<TagComponent>(defenderEntity) ? registry.get<TagComponent>(defenderEntity).tag : TagType::Untagged;
					
					bool skipDamage = false;
					if (aTag == TagType::Bullet || aTag == TagType::PlayerSword || aTag == TagType::Sword || aTag == TagType::Poison) { if (dTag != TagType::Enemy) skipDamage = true; }
					if (aTag != TagType::Untagged && aTag == dTag) skipDamage = true;
					if (aTag == TagType::EnemyBullet && dTag == TagType::Enemy) skipDamage = true;
					// ★ミサイル/弾がCanonタグ（タワー）に当たらないようにする
					if ((aTag == TagType::Bullet || aTag == TagType::EnemyBullet) && dTag == TagType::Canon) skipDamage = true;
					// ★タワー（防衛設備）は無敵にする
					if (dTag == TagType::Canon || dTag == TagType::Cannon || dTag == TagType::IceCanon || dTag == TagType::PipeCannon || dTag == TagType::Defender || dTag == TagType::Missile || dTag == TagType::Poison) {
						skipDamage = true;
					}
					if (skipDamage) continue;

					if (registry.all_of<HealthComponent>(defenderEntity)) {
						auto& hc = registry.get<HealthComponent>(defenderEntity);
						if (hc.invincibleTime <= 0.0f) {
							auto& hurtbox = registry.get<HurtboxComponent>(defenderEntity);
							
							// ★敵への攻撃判定・シールドとダメージ係数の処理
							if (dTag == TagType::Enemy) {
								bool isPlayerAttack = false;
								if (aTag == TagType::PlayerSword || aTag == TagType::Sword) {
									isPlayerAttack = true;
								} else if (registry.all_of<VariableComponent>(attackerEntity)) {
									if (registry.get<VariableComponent>(attackerEntity).GetValue("IsPlayerAttack", 0.0f) > 0.5f) {
										isPlayerAttack = true;
									}
								}

								// ダメージ係数
								// プレイヤー攻撃: シールドに適正ダメージ(1.0倍)、本体には少しだけダメージ(0.2倍)
								// タワー攻撃: シールドに極小ダメージ(0.1倍)、本体に超大ダメージ(2.0倍)
								float shieldDamageRate = isPlayerAttack ? 1.0f : 0.1f;
								float hpDamageRate = isPlayerAttack ? 0.2f : 2.0f;

								float rawDamage = hitbox.damage * hurtbox.damageMultiplier;
								float damageToApply = rawDamage;

								if (hc.shieldHp > 0.0f) {
									float effectiveShieldDamage = damageToApply * shieldDamageRate;
									if (hc.shieldHp >= effectiveShieldDamage) {
										hc.shieldHp -= effectiveShieldDamage;
										damageToApply = 0.0f; // 全てシールドで吸収
									} else {
										float overflowDamage = effectiveShieldDamage - hc.shieldHp;
										hc.shieldHp = 0.0f; // シールド破壊
										if (auto* tutorial = TutorialScript::GetInstance()) tutorial->IncrementBrokenShieldCount();
										// 残りのダメージを本体用の係数に変換して適用
										damageToApply = (overflowDamage / shieldDamageRate) * hpDamageRate;
									}
								} else {
									// シールドがない場合はそのまま本体へ
									damageToApply *= hpDamageRate;
								}

								hc.hp -= damageToApply;
							} else {
								// 敵以外（プレイヤー等）への通常ダメージ処理
								hc.hp -= hitbox.damage * hurtbox.damageMultiplier;
							}
							
							hc.invincibleTime = 0.5f;

							if (aTag == TagType::Poison && dTag == TagType::Enemy) {
								auto& enemyVc = registry.all_of<VariableComponent>(defenderEntity) ? 
									registry.get<VariableComponent>(defenderEntity) : 
									registry.emplace<VariableComponent>(defenderEntity);
								enemyVc.SetValue("IsPoisoned", 1.0f);
								enemyVc.SetValue("PoisonedTimer", 3.0f);
							}

							if (ctx.scene) {
								if (dTag == TagType::Player) {
									ctx.scene->GetEventSystem().Emit("PlayerTakeDamage", hitbox.damage);
								}
								if (aTag == TagType::PlayerSword || aTag == TagType::Sword) {
									ctx.scene->GetEventSystem().Emit("PlayerSwordHit", 1.0f);
								}

								// ★修正: 特殊弾（Enhanced）の場合は通常の歪みエフェクトを出さない
								bool isEnhanced = false;
								if (aTag == TagType::Bullet && registry.all_of<VariableComponent>(attackerEntity)) {
									auto& vc = registry.get<VariableComponent>(attackerEntity);
									if (vc.GetValue("Enhanced", 0.0f) > 0.5f) isEnhanced = true;
								}

								if (!isEnhanced && dTag != TagType::Player) { // ★修正: プレイヤー被弾時は出さない
									auto hitDistortion = ctx.scene->CreateEntity("HitDistortion_VFX");
									if (registry.all_of<BoxColliderComponent>(hitDistortion)) registry.remove<BoxColliderComponent>(hitDistortion);
									if (registry.all_of<HurtboxComponent>(hitDistortion)) registry.remove<HurtboxComponent>(hitDistortion);
									if (registry.all_of<RigidbodyComponent>(hitDistortion)) registry.remove<RigidbodyComponent>(hitDistortion);

									auto& tc_hit = registry.get<TransformComponent>(hitDistortion);
									DirectX::XMStoreFloat3(&tc_hit.translate, dCenter);
									tc_hit.scale = { 1, 1, 1 };

									auto& mrc_hit = registry.emplace<MeshRendererComponent>(hitDistortion);
									mrc_hit.shaderName = "Distortion";
									mrc_hit.texturePath = "Resources/Textures/normal.png";
									mrc_hit.modelPath = "Resources/Models/plane.obj";
									
									if (ctx.renderer) {
										mrc_hit.modelHandle = ctx.renderer->LoadObjMesh(mrc_hit.modelPath);
										mrc_hit.textureHandle = ctx.renderer->LoadTexture2D(mrc_hit.texturePath);
									}
									mrc_hit.color = { 1, 1, 1, 2.0f };

									auto& sc_hit = registry.emplace<ScriptComponent>(hitDistortion);
									sc_hit.scripts.push_back({ "HitDistortionScript", "", std::make_shared<HitDistortionScript>(), false });

									auto& tcTag_hit = registry.emplace<TagComponent>(hitDistortion);
									tcTag_hit.tag = TagType::HitDistortion_VFX;
								}
							}

							hc.hitFlashTimer = 0.2f;
							::Engine::TimeManager::GetInstance().SetHitstop(0.1f);
							ApplyKnockback(registry, attackerEntity, defenderEntity);
						}
					}

					// 弾はヒット判定が行われたら（無敵状態でも）消去する
					if ((aTag == TagType::Bullet || aTag == TagType::EnemyBullet) && ctx.scene) {
						// ★追加: 強化弾の場合、鏡割れエフェクトイベントを発火
						if (aTag == TagType::Bullet && registry.all_of<VariableComponent>(attackerEntity)) {
							auto& vc = registry.get<VariableComponent>(attackerEntity);
							if (vc.GetValue("Enhanced", 0.0f) > 0.5f) {
								ctx.scene->GetEventSystem().Emit("EnhancedBulletHit", static_cast<float>(static_cast<uint32_t>(defenderEntity)));
							}
						}

						// ★通常大砲の弾（Bullet）が着弾した際に、軽量かつ洗練された爆発エフェクトを生成
						if (aTag == TagType::Bullet && registry.all_of<TransformComponent>(attackerEntity)) {
							auto& bulletTrans = registry.get<TransformComponent>(attackerEntity);
							
							// アイスキャノンの弾丸（IceBulletScript）であるかチェック
							bool isIceBullet = false;
							if (registry.all_of<ScriptComponent>(attackerEntity)) {
								const auto& sc = registry.get<ScriptComponent>(attackerEntity);
								for (const auto& instance : sc.scripts) {
									if (instance.scriptPath == "IceBulletScript") {
										isIceBullet = true;
										break;
									}
								}
							}

							if (isIceBullet) {
								// 敵に凍結デバフを適用
								if (registry.all_of<VariableComponent>(attackerEntity)) {
									float stopTime = registry.get<VariableComponent>(attackerEntity).GetValue("StopTime", 1.0f);
									
									auto& enemyVc = registry.all_of<VariableComponent>(defenderEntity) ? 
										registry.get<VariableComponent>(defenderEntity) : 
										registry.emplace<VariableComponent>(defenderEntity);
									
									enemyVc.SetValue("SpeedBuffTimer", stopTime);
									enemyVc.SetValue("SpeedBuffMultiplier", 0.0f); // 速度を0に

									enemyVc.SetValue("IsFrozen", 1.0f);
									enemyVc.SetValue("FrozenTimer", stopTime);
								}

								// 地面から氷の棘（スパイク）が放射状に突き出す
								DirectX::XMFLOAT3 impactPos = bulletTrans.translate;
								float groundH = ctx.scene->GetHeightAt(impactPos.x, impactPos.z, impactPos.y + 1.0f, static_cast<uint32_t>(attackerEntity));
								if (groundH > -5000.0f) {
									impactPos.y = groundH;
								}

								int spikeCount = 8;
								for (int i = 0; i < spikeCount; ++i) {
									float angle = (6.283185f / static_cast<float>(spikeCount)) * static_cast<float>(i);
									
									entt::entity spike = ctx.scene->CreateEntity("IceSpike_VFX");
									ctx.scene->SetTag(spike, TagType::VFX);

									auto& sTrans = registry.get<TransformComponent>(spike);
									sTrans.translate = impactPos;
									
									float dist = 1.2f; // ★より広く (0.4f -> 1.2f)
									sTrans.translate.x += std::cos(angle) * dist;
									sTrans.translate.z += std::sin(angle) * dist;
									sTrans.translate.y -= 0.5f; // 地面下に埋める

									sTrans.rotate.y = -angle;
									sTrans.rotate.x = 0.85f; // ★もっと外側に傾ける (0.35f -> 0.85f)
									sTrans.scale = { 0.12f, 0.01f, 0.12f };

									if (ctx.renderer) {
										auto& mr = registry.emplace<MeshRendererComponent>(spike);
										mr.modelHandle = ctx.renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
										mr.textureHandle = ctx.renderer->LoadTexture2D("Resources/Textures/white1x1.png");
										mr.color = { 0.5f, 0.85f, 1.0f, 1.5f }; // 青白い半透明色
									}

									auto& sc = registry.emplace<ScriptComponent>(spike);
									sc.scripts.push_back({ "IceSpikeScript", "", nullptr });

									auto& vc = registry.emplace<VariableComponent>(spike);
									vc.SetValue("TargetScaleY", 1.0f + (rand() % 100) * 0.005f);
									vc.SetValue("Angle", angle);
								}

								// 砕け散る氷の破片 (きらめく反射) & 凍結エフェクト
								entt::entity impactVfx = ctx.scene->CreateEntity("IceImpact_VFX");
								ctx.scene->SetTag(impactVfx, TagType::VFX);
								auto& iTrans = registry.get<TransformComponent>(impactVfx);
								iTrans.translate = bulletTrans.translate;

								auto& pecShatter = registry.emplace<ParticleEmitterComponent>(impactVfx);
								pecShatter.emitter.params.name = "IceImpactShatter";
								pecShatter.emitter.params.texturePath = "Resources/Textures/particles/shattered_prism.png"; // 砕けたプリズム
								pecShatter.emitter.params.emitRate = 0.0f;
								pecShatter.emitter.params.shape = Engine::EmissionShape::Sphere;
								pecShatter.emitter.params.shapeRadius = 1.2f;
								pecShatter.emitter.params.lifeTime = 0.9f;
								pecShatter.emitter.params.lifeTimeVariance = 0.25f;
								pecShatter.emitter.params.startVelocity = { 0.0f, 1.5f, 0.0f };
								pecShatter.emitter.params.velocityVariance = { 2.5f, 1.2f, 2.5f };
								pecShatter.emitter.params.damping = 0.8f;
								pecShatter.emitter.params.startColor = { 0.65f, 0.9f, 1.0f, 1.6f };
								pecShatter.emitter.params.endColor = { 0.3f, 0.7f, 1.0f, 0.0f };
								pecShatter.emitter.params.startSize = { 0.35f, 0.35f, 0.35f };
								pecShatter.emitter.params.endSize = { 1.1f, 1.1f, 1.1f };
								pecShatter.emitter.params.isAdditive = true;
								pecShatter.emitter.params.position = { bulletTrans.translate.x, bulletTrans.translate.y, bulletTrans.translate.z };

								pecShatter.emitter.Initialize(*ctx.renderer, "IceImpactShatter");
								pecShatter.isInitialized = true;
								pecShatter.emitter.EmitBurst(22);

								// キラキラ光る反射スパーク
								entt::entity sparkleVfx = ctx.scene->CreateEntity("IceImpactSparkle_VFX");
								ctx.scene->SetTag(sparkleVfx, TagType::VFX);
								auto& spTrans = registry.get<TransformComponent>(sparkleVfx);
								spTrans.translate = bulletTrans.translate;

								auto& pecSparkle = registry.emplace<ParticleEmitterComponent>(sparkleVfx);
								pecSparkle.emitter.params.name = "IceImpactSparkle";
								pecSparkle.emitter.params.texturePath = "Resources/Textures/particles/diamond_flare.png"; // ダイヤモンドフレア
								pecSparkle.emitter.params.emitRate = 0.0f;
								pecSparkle.emitter.params.shape = Engine::EmissionShape::Sphere;
								pecSparkle.emitter.params.shapeRadius = 0.8f;
								pecSparkle.emitter.params.lifeTime = 0.7f;
								pecSparkle.emitter.params.lifeTimeVariance = 0.2f;
								pecSparkle.emitter.params.startVelocity = { 0.0f, 3.5f, 0.0f };
								pecSparkle.emitter.params.velocityVariance = { 4.5f, 3.5f, 4.5f };
								pecSparkle.emitter.params.damping = 0.6f;
								pecSparkle.emitter.params.startColor = { 0.8f, 0.95f, 1.0f, 1.8f };
								pecSparkle.emitter.params.endColor = { 0.2f, 0.5f, 1.0f, 0.0f };
								pecSparkle.emitter.params.startSize = { 0.2f, 0.2f, 0.2f };
								pecSparkle.emitter.params.endSize = { 0.02f, 0.02f, 0.02f };
								pecSparkle.emitter.params.angularVelocity = { 5.0f, 5.0f, 5.0f };
								pecSparkle.emitter.params.angularVelocityVariance = { 10.0f, 10.0f, 10.0f };
								pecSparkle.emitter.params.isAdditive = true;
								pecSparkle.emitter.params.position = { bulletTrans.translate.x, bulletTrans.translate.y, bulletTrans.translate.z };

								pecSparkle.emitter.Initialize(*ctx.renderer, "IceImpactSparkle");
								pecSparkle.isInitialized = true;
								pecSparkle.emitter.EmitBurst(25);

								// スクリプトと寿命
								auto& scSp = registry.emplace<ScriptComponent>(impactVfx);
								scSp.scripts.push_back({ "BulletScript", "", nullptr });
								auto& vcSp = registry.emplace<VariableComponent>(impactVfx);
								vcSp.SetValue("Speed", 0.0f);
								vcSp.SetValue("MaxLifeTime", 1.2f);

								auto& scSparkle = registry.emplace<ScriptComponent>(sparkleVfx);
								scSparkle.scripts.push_back({ "BulletScript", "", nullptr });
								auto& vcSparkle = registry.emplace<VariableComponent>(sparkleVfx);
								vcSparkle.SetValue("Speed", 0.0f);
								vcSparkle.SetValue("MaxLifeTime", 1.0f);

							} else {
								entt::entity explosionVfx = ctx.scene->CreateEntity("CanonExplosion_VFX");
								ctx.scene->SetTag(explosionVfx, TagType::VFX);

								auto& vfxTrans = registry.get<TransformComponent>(explosionVfx);
								vfxTrans.translate = bulletTrans.translate;

								// 1. 火花（きらめくテクスチャを使用）
								auto& pec = registry.emplace<ParticleEmitterComponent>(explosionVfx);
								pec.emitter.params.name = "ImpactExplosion";
								pec.emitter.params.texturePath = "Resources/Textures/particles/diamond_flare.png";
								pec.emitter.params.emitRate = 0.0f;
								pec.emitter.params.shape = Engine::EmissionShape::Sphere;
								pec.emitter.params.shapeRadius = 0.5f;
								pec.emitter.params.startVelocity = {0.0f, 6.0f, 0.0f};
								pec.emitter.params.velocityVariance = {4.0f, 4.0f, 4.0f};
								pec.emitter.params.acceleration = {0.0f, -9.8f, 0.0f}; // 重力落下
								pec.emitter.params.startColor = {1.0f, 0.8f, 0.3f, 1.0f};
								pec.emitter.params.endColor = {1.0f, 0.2f, 0.0f, 0.0f};
								pec.emitter.params.startSize = {0.4f, 0.4f, 0.4f};
								pec.emitter.params.endSize = {0.05f, 0.05f, 0.05f};
								pec.emitter.params.lifeTime = 0.6f;
								pec.emitter.params.lifeTimeVariance = 0.2f;
								pec.emitter.params.damping = 1.0f;
								pec.emitter.params.isAdditive = true;

								// 明示的に初期化し、その場でバースト放出！
								pec.emitter.Initialize(*ctx.renderer, "ImpactExplosion_Emitter");
								pec.isInitialized = true;
								pec.emitter.EmitBurst(12);

								// 2. 煙（白煙）
								entt::entity smokeVfx = ctx.scene->CreateEntity("CanonExplosion_Smoke_VFX");
								ctx.scene->SetTag(smokeVfx, TagType::VFX);
								auto& sTrans = registry.get<TransformComponent>(smokeVfx);
								sTrans.translate = bulletTrans.translate;

								auto& spec = registry.emplace<ParticleEmitterComponent>(smokeVfx);
								spec.emitter.params.name = "ImpactSmoke";
								spec.emitter.params.texturePath = "Resources/Textures/white1x1.png";
								spec.emitter.params.emitRate = 0.0f;
								spec.emitter.params.shape = Engine::EmissionShape::Sphere;
								spec.emitter.params.shapeRadius = 0.8f;
								spec.emitter.params.startVelocity = {0.0f, 2.0f, 0.0f};
								spec.emitter.params.velocityVariance = {1.5f, 1.0f, 1.5f};
								spec.emitter.params.startColor = {0.4f, 0.4f, 0.4f, 0.15f};
								spec.emitter.params.endColor = {0.2f, 0.2f, 0.2f, 0.0f};
								spec.emitter.params.startSize = {0.8f, 0.8f, 0.8f};
								spec.emitter.params.endSize = {1.8f, 1.8f, 1.8f};
								spec.emitter.params.lifeTime = 0.8f;
								spec.emitter.params.lifeTimeVariance = 0.3f;
								spec.emitter.params.damping = 1.5f;
								spec.emitter.params.isAdditive = false;

								// 明示的に初期化し、その場でバースト放出！
								spec.emitter.Initialize(*ctx.renderer, "ImpactSmoke_Emitter");
								spec.isInitialized = true;
								spec.emitter.EmitBurst(8);

								// スクリプトと光を追加
								auto& sc = registry.emplace<ScriptComponent>(explosionVfx);
								sc.scripts.push_back({"BulletScript", "", nullptr});
								auto& vc = registry.emplace<VariableComponent>(explosionVfx);
								vc.SetValue("Speed", 0.0f);
								vc.SetValue("MaxLifeTime", 1.0f); // パーティクルが消え終わるまでオブジェクトを生かしておく

								auto& sSc = registry.emplace<ScriptComponent>(smokeVfx);
								sSc.scripts.push_back({"BulletScript", "", nullptr});
								auto& sVc = registry.emplace<VariableComponent>(smokeVfx);
								sVc.SetValue("Speed", 0.0f);
								sVc.SetValue("MaxLifeTime", 1.5f);

								// 地面が安っぽく光るのを完全に防ぐため、光源強度と半径を極小に制限
								auto& pointLight = registry.emplace<PointLightComponent>(explosionVfx);
								pointLight.color = {1.0f, 0.6f, 0.2f};
								pointLight.intensity = 1.5f;
								pointLight.range = 3.0f;
								pointLight.atten = {1.0f, 0.8f, 0.2f};
							}
						}

						ctx.scene->DestroyObject(static_cast<uint32_t>(attackerEntity));
						break; // 弾は消えるのでループ抜ける
					}
				}
			}
		}
	}

private:
	struct HurtDeviceInfo {
		entt::entity entity;
		DirectX::XMFLOAT3 center;
		DirectX::XMFLOAT3 extents;
		DirectX::XMFLOAT3 axes[3];
	};
	std::vector<HurtDeviceInfo> m_hurters;
	std::vector<uint32_t> m_nearbyIndices;

	static bool CheckObbOverlap(DirectX::XMVECTOR cA, DirectX::XMVECTOR* axesA, float* extA, 
						      DirectX::XMVECTOR cB, DirectX::XMVECTOR* axesB, float* extB) {
		DirectX::XMVECTOR L_axes[15];
		for (int i = 0; i < 3; ++i) { L_axes[i] = axesA[i]; L_axes[i + 3] = axesB[i]; }
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				L_axes[6 + i * 3 + j] = DirectX::XMVector3Cross(axesA[i], axesB[j]);
			}
		}

		DirectX::XMVECTOR relPos = DirectX::XMVectorSubtract(cA, cB);
		for (int i = 0; i < 15; ++i) {
			DirectX::XMVECTOR L = L_axes[i];
			float lenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(L));
			if (lenSq < 0.001f) continue;
			L = DirectX::XMVectorScale(L, 1.0f / std::sqrt(lenSq));

			float rA = 0, rB = 0;
			for (int m = 0; m < 3; ++m) {
				rA += std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axesA[m], L))) * extA[m];
				rB += std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axesB[m], L))) * extB[m];
			}
			float dist = std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(relPos, L)));
			if (dist > rA + rB) return false;
		}
		return true;
	}

	static void ApplyKnockback(entt::registry& /*registry*/, entt::entity /*attacker*/, entt::entity /*defender*/) {
		// プレイヤーや敵がダメージを受けた際のノックバック機能を無効化
		return;
	}
};

} // namespace Game
