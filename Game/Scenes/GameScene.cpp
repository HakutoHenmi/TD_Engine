#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "GameScene.h"
#include "../Editor/EditorUI.h"
#include "../Scripts/ScriptEngine.h"
#include "../Systems/AudioSystem.h"
#include "../Systems/CameraFollowSystem.h"
#include "../Systems/CharacterMovementSystem.h"
#include "../Systems/CleanupSystem.h"
#include "../Systems/CombatSystem.h"
#include "../Systems/HealthSystem.h"
#include "../Systems/PhysicsSystem.h"
#include "../Systems/PlayerInputSystem.h"
#include "../Systems/RiverSystem.h" // ★追加
#include "../Systems/ScriptSystem.h"
#include "../Systems/UISystem.h"
#include "Audio.h"
#include "imgui.h"
#include <Windows.h> // OutputDebugStringA
#include <algorithm>
#include <cmath>

namespace Game {

void GameScene::Initialize(Engine::WindowDX* dx) {
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();
	camera_.Initialize();
	// ★追加: 明示的にプロジェクションを設定 (1920x1080のアスペクト比)
	camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 1000.0f);
	camera_.SetPosition(0, 2, -5);
	camera_.SetRotation(0.2f, 0, 0);
	renderer_->SetAmbientColor({0.4f, 0.4f, 0.45f});

	bool loaded = false;
	// ★ リリース構成等での自動ロード
	std::string scenePath = EditorUI::GetUnifiedProjectPath("Resources/scene.json");
	if (std::filesystem::exists(scenePath)) {
		OutputDebugStringA(("[GameScene] " + scenePath + " found. Loading...\n").c_str());
		EditorUI::LoadScene(this, scenePath);
		isPlaying_ = true; // ロード直後からプレイ状態にする
		loaded = true;
	} else {
		OutputDebugStringA(("[GameScene] " + scenePath + " NOT found.\n").c_str());
	}

	// 既にオブジェクトが存在する場合（リスタート時）やロード失敗時は最低限の内容を作成
	if (objects_.empty() || !loaded) {
		SceneObject sun;
		sun.name = "Sun";
		sun.translate = {0, 10, 0};
		sun.rotate = {DirectX::XMConvertToRadians(45.0f), DirectX::XMConvertToRadians(30.0f), 0};
		sun.directionalLights.push_back(DirectionalLightComponent());
		objects_.push_back(sun);

		SceneObject plane;
		plane.name = "Plane";
		plane.modelHandle = renderer_->LoadObjMesh("Resources/plane.obj");
		plane.textureHandle = renderer_->LoadTexture2D("Resources/white1x1.png");
		plane.modelPath = "Resources/plane.obj";
		plane.texturePath = "Resources/white1x1.png";
		plane.scale = {20, 1, 20};
		objects_.push_back(plane);
	}

	// パーティクルエディターの初期化
	particleEditor_.Initialize();

	// スクリプトエンジンの初期化
	ScriptEngine::GetInstance()->Initialize();

	// ★ Systemの登録（順序が重要）
	systems_.clear();
	systems_.push_back(std::make_unique<PlayerInputSystem>());
	systems_.push_back(std::make_unique<CharacterMovementSystem>());
	systems_.push_back(std::make_unique<PhysicsSystem>());
	systems_.push_back(std::make_unique<CameraFollowSystem>());
	systems_.push_back(std::make_unique<HealthSystem>());

	auto scriptSys = std::make_unique<ScriptSystem>();
	scriptSys->SetScene(this);
	systems_.push_back(std::move(scriptSys));

	systems_.push_back(std::make_unique<CombatSystem>());
	systems_.push_back(std::make_unique<AudioSystem>());
	systems_.push_back(std::make_unique<UISystem>());
	systems_.push_back(std::make_unique<CleanupSystem>());

	// 前回プレイで動的に生成されたオブジェクトの削除
	objects_.erase(
	    std::remove_if(
	        objects_.begin(), objects_.end(),
	        [](const SceneObject& o) {
		        bool isBullet = false;
		        for (const auto& t : o.tags) {
			        if (t.tag == "Bullet")
				        isBullet = true;
		        }
		        return isBullet || o.name == "Bullet";
	        }),
	    objects_.end());

	// 各Systemのリセット
	for (auto& sys : systems_) {
		sys->Reset(objects_);
	}

	// ★追加: 川の初期メッシュ生成
	for (auto& obj : objects_) {
		for (auto& rv : obj.rivers) {
			if (rv.enabled && rv.meshHandle == 0) {
				RiverSystem::BuildRiverMesh(rv, renderer_, objects_);
			}
		}
	}
}

// =====================================================
// ★ Update: 各Systemに処理を委譲
// =====================================================
void GameScene::Update() {
	if (!renderer_)
		return;
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - last).count();
	last = now;

	if (dt > 1.0f / 10.0f)
		dt = 1.0f / 60.0f; // 極端なラグ対策

	// コンテキストを更新
	ctx_.dt = dt;
	ctx_.camera = &camera_;
	ctx_.renderer = renderer_;
	ctx_.input = Engine::Input::GetInstance();
	ctx_.isPlaying = isPlaying_;
	ctx_.scene = this; // ★追加
	ctx_.pendingSpawns = &pendingSpawns_;

	// GPU Collision Dispatch（エンジン固有処理のため残留）
	if (renderer_) {
		uint32_t pairIndex = 0;
		for (size_t i = 0; i < objects_.size(); ++i) {
			auto& objA = objects_[i];
			for (auto& mc : objA.gpuMeshColliders)
				mc.isIntersecting = false;
		}

		for (size_t i = 0; i < objects_.size(); ++i) {
			auto& objA = objects_[i];
			if (objA.gpuMeshColliders.empty() || !objA.gpuMeshColliders[0].enabled)
				continue;

			for (size_t j = i + 1; j < objects_.size(); ++j) {
				auto& objB = objects_[j];
				if (objB.gpuMeshColliders.empty() || !objB.gpuMeshColliders[0].enabled)
					continue;

				if (renderer_->GetCollisionResult(pairIndex)) {
					objA.gpuMeshColliders[0].isIntersecting = true;
					objB.gpuMeshColliders[0].isIntersecting = true;
				}
				pairIndex++;
			}
		}

		uint32_t numPairs = 0;
		for (size_t i = 0; i < objects_.size(); ++i) {
			if (!objects_[i].gpuMeshColliders.empty() && objects_[i].gpuMeshColliders[0].enabled) {
				for (size_t j = i + 1; j < objects_.size(); ++j) {
					if (!objects_[j].gpuMeshColliders.empty() && objects_[j].gpuMeshColliders[0].enabled)
						numPairs++;
				}
			}
		}

		renderer_->BeginCollisionCheck(numPairs);
		pairIndex = 0;
		for (size_t i = 0; i < objects_.size(); ++i) {
			auto& objA = objects_[i];
			if (objA.gpuMeshColliders.empty() || !objA.gpuMeshColliders[0].enabled)
				continue;

			for (size_t j = i + 1; j < objects_.size(); ++j) {
				auto& objB = objects_[j];
				if (objB.gpuMeshColliders.empty() || !objB.gpuMeshColliders[0].enabled)
					continue;

				uint32_t meshA = objA.gpuMeshColliders[0].meshHandle;
				uint32_t meshB = objB.gpuMeshColliders[0].meshHandle;

				if (meshA == 0)
					meshA = objA.modelHandle;
				if (meshB == 0)
					meshB = objB.modelHandle;

				if (meshA != 0 && meshB != 0) {
					renderer_->DispatchCollision(meshA, objA.GetTransform(), meshB, objB.GetTransform(), pairIndex);
				}
				pairIndex++;
			}
		}
		renderer_->EndCollisionCheck();
	}

	// Animation（エンジン固有処理のため残留）
	for (auto& obj : objects_) {
		for (auto& anim : obj.animators) {
			if (anim.enabled && anim.isPlaying) {
				anim.time += dt * 60.0f * anim.speed;
				auto* m = renderer_->GetModel(obj.modelHandle);
				if (m) {
					const auto& data = m->GetData();
					for (const auto& a : data.animations) {
						if (a.name == anim.currentAnimation) {
							if (anim.time > a.duration) {
								if (anim.loop)
									anim.time = std::fmod(anim.time, a.duration);
								else {
									anim.time = a.duration;
									anim.isPlaying = false;
								}
							}
							break;
						}
					}
				}
			}
		}
	}

	// パーティクルエディター
	particleEditor_.Update(dt);

	// ★ 全Systemを順に実行
	for (auto& system : systems_) {
		system->Update(objects_, ctx_);
	}

	// ★ ペンディングオブジェクト（弾など）をflush
	if (!pendingSpawns_.empty()) {
		objects_.insert(objects_.end(), pendingSpawns_.begin(), pendingSpawns_.end());
		pendingSpawns_.clear();
	}

	// Light System（レンダリング設定のため残留）
	if (renderer_) {
		int plCount = 0;
		int slCount = 0;
		bool hasDirLight = false;

		for (const auto& obj : objects_) {
			for (const auto& dl : obj.directionalLights) {
				if (dl.enabled && !hasDirLight) {
					Engine::Matrix4x4 mat = obj.GetTransform().ToMatrix();
					Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
					Engine::Vector3 color = {dl.color.x * dl.intensity, dl.color.y * dl.intensity, dl.color.z * dl.intensity};
					renderer_->SetDirectionalLight(dir, color, true);
					hasDirLight = true;
				}
			}
			for (const auto& pl : obj.pointLights) {
				if (pl.enabled && plCount < Engine::Renderer::kMaxPointLights) {
					Engine::Vector3 pos = {obj.translate.x, obj.translate.y, obj.translate.z};
					Engine::Vector3 color = {pl.color.x * pl.intensity, pl.color.y * pl.intensity, pl.color.z * pl.intensity};
					Engine::Vector3 atten = {pl.atten.x, pl.atten.y, pl.atten.z};
					renderer_->SetPointLight(plCount, pos, color, pl.range, atten, true);
					plCount++;
				}
			}
			for (const auto& sl : obj.spotLights) {
				if (sl.enabled && slCount < Engine::Renderer::kMaxSpotLights) {
					Engine::Matrix4x4 mat = obj.GetTransform().ToMatrix();
					Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
					Engine::Vector3 pos = {obj.translate.x, obj.translate.y, obj.translate.z};
					Engine::Vector3 color = {sl.color.x * sl.intensity, sl.color.y * sl.intensity, sl.color.z * sl.intensity};
					Engine::Vector3 atten = {sl.atten.x, sl.atten.y, sl.atten.z};
					renderer_->SetSpotLight(slCount, pos, dir, color, sl.range, sl.innerCos, sl.outerCos, atten, true);
					slCount++;
				}
			}
		}

		if (!hasDirLight) {
			renderer_->SetDirectionalLight({0, -1, 0}, {0, 0, 0}, false);
		}
		for (int i = plCount; i < Engine::Renderer::kMaxPointLights; ++i) {
			renderer_->SetPointLight(i, {0, 0, 0}, {0, 0, 0}, 0, {1, 0, 0}, false);
		}
		for (int i = slCount; i < Engine::Renderer::kMaxSpotLights; ++i) {
			renderer_->SetSpotLight(i, {0, 0, 0}, {0, -1, 0}, {0, 0, 0}, 0, 0.0f, 0.0f, {1, 0, 0}, false);
		}
	}

	// パーティクルエミッターコンポーネント
	for (auto& obj : objects_) {
		for (auto& emitterComp : obj.particleEmitters) {
			if (!emitterComp.enabled)
				continue;

			if (!emitterComp.isInitialized && renderer_) {
				emitterComp.emitter.Initialize(*renderer_, obj.name + "_Emitter");
				if (!emitterComp.assetPath.empty()) {
					emitterComp.emitter.LoadFromJson(emitterComp.assetPath);
				}
				emitterComp.isInitialized = true;
			}

			emitterComp.emitter.params.position = {obj.translate.x, obj.translate.y, obj.translate.z};
			emitterComp.emitter.Update(dt);
		}
	}
}

// ★ 汎用スポーン
void GameScene::SpawnObject(const SceneObject& obj) { pendingSpawns_.push_back(obj); }

Engine::Matrix4x4 GameScene::GetWorldMatrix(int index) const {
	if (index < 0 || index >= (int)objects_.size()) return Engine::Matrix4x4::Identity();
	const auto& obj = objects_[index];
	Engine::Matrix4x4 local = obj.GetTransform().ToMatrix();
	if (obj.parentId == 0) return local;

	int parentIdx = -1;
	for (int i = 0; i < (int)objects_.size(); ++i) {
		if (objects_[i].id == obj.parentId) {
			parentIdx = i;
			break;
		}
	}
	if (parentIdx == -1) return local;
	return Engine::Matrix4x4::Multiply(local, GetWorldMatrix(parentIdx));
}

void GameScene::Draw() {
	if (!renderer_)
		return;
	renderer_->SetCamera(camera_);
#ifdef USE_IMGUI
	DrawEditorGizmos();
#endif

	// ★追加: プレイヤーの位置を Renderer に同期（草のインタラクション用）
	for (const auto& obj : objects_) {
		if (obj.name == "Player") {
			renderer_->SetPlayerPos(Engine::Vector3{obj.translate.x, obj.translate.y, obj.translate.z});
			break;
		}
	}

	for (const auto& obj : objects_) {
		bool hasMeshRenderer = false;
		for (const auto& mr : obj.meshRenderers) {
			if (mr.enabled && mr.modelHandle != 0) {
				hasMeshRenderer = true;
				bool hasAnim = false;
				std::vector<Engine::Matrix4x4> bonePalette;
				for (const auto& anim : obj.animators) {
					if (anim.enabled && !anim.currentAnimation.empty()) {
						auto* m = renderer_->GetModel(mr.modelHandle);
						if (m) {
							const auto& data = m->GetData();
							const Engine::Animation* currAnim = nullptr;
							for (const auto& a : data.animations) {
								if (a.name == anim.currentAnimation) {
									currAnim = &a;
									break;
								}
							}
							if (currAnim) {
								bonePalette.resize(data.bones.size());
								for (auto& b : bonePalette)
									b = Engine::Matrix4x4::Identity();
								m->UpdateSkeleton(data.rootNode, Engine::Matrix4x4::Identity(), *currAnim, anim.time, bonePalette);
								hasAnim = true;
								break;
							}
						}
					}
				}

				if (hasAnim) {
					renderer_->DrawSkinnedMesh(
					    mr.modelHandle, mr.textureHandle, obj.GetTransform(), bonePalette, {obj.color.x * mr.color.x, obj.color.y * mr.color.y, obj.color.z * mr.color.z, obj.color.w * mr.color.w});
				} else {
					// ★変更: Toon系シェーダーの場合は非インスタンス描画を使用（アウトライン2パス処理のため）
					Engine::Matrix4x4 world = GetWorldMatrix((int)(&obj - &objects_[0]));
					Engine::Transform worldTr;
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&worldTr.translate), DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&world)).r[3]);
					// 注意: 行列からの正確なTRS分解が必要だが、ここでは簡易化。本来はRendererがMatrix4x4を直接受け取るべき。
					// 現状のRenderer::DrawMeshがTransformを受け取る設計のため、Matrix->Transformへの簡易変換を行う。
					if (mr.shaderName == "Toon" || mr.shaderName == "ToonSkinning") {
						renderer_->DrawMesh(mr.modelHandle, mr.textureHandle, obj.GetTransform(), {obj.color.x * mr.color.x, obj.color.y * mr.color.y, obj.color.z * mr.color.z, obj.color.w * mr.color.w}, mr.shaderName);
					} else {
						renderer_->DrawMeshInstanced(
							mr.modelHandle, mr.textureHandle, obj.GetTransform(), {obj.color.x * mr.color.x, obj.color.y * mr.color.y, obj.color.z * mr.color.z, obj.color.w * mr.color.w}, mr.shaderName,
							mr.extraTextureHandles);
					}
				}
			}
		}

		if (!hasMeshRenderer && obj.modelHandle != 0) {
			bool hasAnim = false;
			std::vector<Engine::Matrix4x4> bonePalette;
			for (const auto& anim : obj.animators) {
				if (anim.enabled && !anim.currentAnimation.empty()) {
					auto* m = renderer_->GetModel(obj.modelHandle);
					if (m) {
						const auto& data = m->GetData();
						const Engine::Animation* currAnim = nullptr;
						for (const auto& a : data.animations) {
							if (a.name == anim.currentAnimation) {
								currAnim = &a;
								break;
							}
						}
						if (currAnim) {
							bonePalette.resize(data.bones.size());
							for (auto& b : bonePalette)
								b = Engine::Matrix4x4::Identity();
							m->UpdateSkeleton(data.rootNode, Engine::Matrix4x4::Identity(), *currAnim, anim.time, bonePalette);
							hasAnim = true;
							break;
						}
					}
				}
			}

			if (hasAnim) {
				renderer_->DrawSkinnedMesh(obj.modelHandle, obj.textureHandle, obj.GetTransform(), bonePalette, {obj.color.x, obj.color.y, obj.color.z, obj.color.w});
			} else {
				// ★変更: Toon系シェーダーの場合は非インスタンス描画を使用
				if (obj.shaderName == "Toon" || obj.shaderName == "ToonSkinning") {
					renderer_->DrawMesh(obj.modelHandle, obj.textureHandle, obj.GetTransform(), {obj.color.x, obj.color.y, obj.color.z, obj.color.w}, obj.shaderName);
				} else {
					std::vector<uint32_t> extraHandles;
					for (const auto& p : obj.extraTexturePaths)
						extraHandles.push_back(renderer_->LoadTexture2D(p));
					renderer_->DrawMeshInstanced(obj.modelHandle, obj.textureHandle, obj.GetTransform(), {obj.color.x, obj.color.y, obj.color.z, obj.color.w}, obj.shaderName, extraHandles);
				}
			}
		}

		// ★追加: 川コンポーネントの描画 (メッシュはワールド座標で生成済みなのでIdentity変換)
		for (const auto& rv : obj.rivers) {
			if (rv.enabled && rv.meshHandle != 0) {
				auto tex = renderer_->LoadTexture2D(rv.texturePath);
				Engine::Transform identity;
				identity.translate = {0,0,0};
				identity.rotate = {0,0,0};
				identity.scale = {1,1,1};
				// gColorとして {flowSpeed, uvScale, 0, 0} を渡す
				renderer_->DrawMesh(rv.meshHandle, tex, identity, {rv.flowSpeed, rv.uvScale, 0.0f, 0.0f}, "River");
			}
		}
	}
#ifdef USE_IMGUI
	DrawSelectionHighlight();
	DrawLightGizmos();
#endif
	for (auto& obj : objects_) {
		for (auto& emitterComp : obj.particleEmitters) {
			if (emitterComp.enabled) {
				emitterComp.emitter.Draw(camera_);
			}
		}
	}

	// ★ 各Systemの描画処理を呼び出す（UISystem等）
	for (auto& system : systems_) {
		system->Draw(objects_, ctx_);
	}
}

extern GizmoMode currentGizmoMode;
extern bool gizmoDragging;
extern int gizmoDragAxis;

void GameScene::DrawSelectionHighlight() {
	if (!renderer_)
		return;

	for (int idx : selectedIndices_) {
		if (idx < 0 || idx >= (int)objects_.size())
			continue;
		auto& obj = objects_[idx];
		Engine::Vector3 pos = {obj.translate.x, obj.translate.y, obj.translate.z};

		Engine::Matrix4x4 mat = GetWorldMatrix(idx);
		DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));

		Engine::Vector4 hlColor = {1.0f, 0.85f, 0.0f, 1.0f};
		Engine::Vector3 v[8] = {
		    {-1.0f, -1.0f, -1.0f},
            {1.0f,  -1.0f, -1.0f},
            {1.0f,  1.0f,  -1.0f},
            {-1.0f, 1.0f,  -1.0f},
            {-1.0f, -1.0f, 1.0f },
            {1.0f,  -1.0f, 1.0f },
            {1.0f,  1.0f,  1.0f },
            {-1.0f, 1.0f,  1.0f },
		};

		for (int i = 0; i < 8; ++i) {
			DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(v[i].x, v[i].y, v[i].z, 1.0f), worldMat);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&v[i]), p);
		}
		int edges[][2] = {
		    {0, 1},
            {1, 2},
            {2, 3},
            {3, 0},
            {4, 5},
            {5, 6},
            {6, 7},
            {7, 4},
            {0, 4},
            {1, 5},
            {2, 6},
            {3, 7}
        };
		for (auto& eg : edges)
			renderer_->DrawLine3D(v[eg[0]], v[eg[1]], hlColor, true);

		for (const auto& bc : obj.boxColliders) {
			if (!bc.enabled)
				continue;
			float hx = bc.size.x * 0.5f, hy = bc.size.y * 0.5f, hz = bc.size.z * 0.5f;
			Engine::Vector3 cp = {bc.center.x, bc.center.y, bc.center.z};
			Engine::Vector4 colColor = {0.2f, 1.0f, 0.2f, 0.8f};
			Engine::Vector3 cv[8] = {
			    {cp.x - hx, cp.y - hy, cp.z - hz},
                {cp.x + hx, cp.y - hy, cp.z - hz},
                {cp.x + hx, cp.y + hy, cp.z - hz},
                {cp.x - hx, cp.y + hy, cp.z - hz},
			    {cp.x - hx, cp.y - hy, cp.z + hz},
                {cp.x + hx, cp.y - hy, cp.z + hz},
                {cp.x + hx, cp.y + hy, cp.z + hz},
                {cp.x - hx, cp.y + hy, cp.z + hz},
			};
			for (int i = 0; i < 8; ++i) {
				DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
				DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
			}
			for (auto& eg : edges)
				renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], colColor, true);
		}

		for (const auto& gmc : obj.gpuMeshColliders) {
			if (!gmc.enabled)
				continue;
			Engine::Vector4 gColor = gmc.isIntersecting ? Engine::Vector4{1.0f, 0.2f, 0.2f, 0.8f} : Engine::Vector4{0.2f, 0.2f, 1.0f, 0.8f};
			float hs = 1.0f;
			Engine::Vector3 cv[8] = {
			    {-hs, -hs, -hs},
                {hs,  -hs, -hs},
                {hs,  hs,  -hs},
                {-hs, hs,  -hs},
                {-hs, -hs, hs },
                {hs,  -hs, hs },
                {hs,  hs,  hs },
                {-hs, hs,  hs }
            };
			for (int i = 0; i < 8; ++i) {
				DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
				DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
			}
			for (auto& eg : edges)
				renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], gColor, true);
		}

		for (const auto& hb : obj.hitboxes) {
			if (!hb.enabled)
				continue;
			float hx = hb.size.x * 0.5f, hy = hb.size.y * 0.5f, hz = hb.size.z * 0.5f;
			Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
			Engine::Vector4 hbColor = hb.isActive ? Engine::Vector4{1.0f, 0.2f, 0.2f, 1.0f} : Engine::Vector4{1.0f, 0.2f, 0.2f, 0.3f};
			Engine::Vector3 hv[8] = {
			    {cp.x - hx, cp.y - hy, cp.z - hz},
                {cp.x + hx, cp.y - hy, cp.z - hz},
                {cp.x + hx, cp.y + hy, cp.z - hz},
                {cp.x - hx, cp.y + hy, cp.z - hz},
			    {cp.x - hx, cp.y - hy, cp.z + hz},
                {cp.x + hx, cp.y - hy, cp.z + hz},
                {cp.x + hx, cp.y + hy, cp.z + hz},
                {cp.x - hx, cp.y + hy, cp.z + hz},
			};
			for (int i = 0; i < 8; ++i) {
				DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
				DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
			}
			for (auto& eg : edges)
				renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
		}

		for (const auto& hb : obj.hurtboxes) {
			if (!hb.enabled)
				continue;
			float hx = hb.size.x * 0.5f, hy = hb.size.y * 0.5f, hz = hb.size.z * 0.5f;
			Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
			Engine::Vector4 hbColor = {0.2f, 1.0f, 0.5f, 0.6f};
			Engine::Vector3 hv[8] = {
			    {cp.x - hx, cp.y - hy, cp.z - hz},
                {cp.x + hx, cp.y - hy, cp.z - hz},
                {cp.x + hx, cp.y + hy, cp.z - hz},
                {cp.x - hx, cp.y + hy, cp.z - hz},
			    {cp.x - hx, cp.y - hy, cp.z + hz},
                {cp.x + hx, cp.y - hy, cp.z + hz},
                {cp.x + hx, cp.y + hy, cp.z + hz},
                {cp.x - hx, cp.y + hy, cp.z + hz},
			};
			for (int i = 0; i < 8; ++i) {
				DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
				DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
			}
			for (auto& eg : edges)
				renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
		}

		DirectX::XMMATRIX gizmoMat = DirectX::XMMatrixRotationRollPitchYaw(obj.rotate.x, obj.rotate.y, obj.rotate.z) * DirectX::XMMatrixTranslation(obj.translate.x, obj.translate.y, obj.translate.z);
		auto drawLocalLine = [&](const Engine::Vector3& localP0, const Engine::Vector3& localP1, const Engine::Vector4& col) {
			DirectX::XMVECTOR p0 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP0.x, localP0.y, localP0.z, 1.0f), gizmoMat);
			DirectX::XMVECTOR p1 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP1.x, localP1.y, localP1.z, 1.0f), gizmoMat);
			Engine::Vector3 wp0, wp1;
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp0), p0);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
			renderer_->DrawLine3D(wp0, wp1, col, true);
		};

		const float al = 2.0f, ar = 0.3f;
		int dAxis = (gizmoDragging && idx == selectedObjectIndex_) ? gizmoDragAxis : -1;
		auto axCol = [](int axis, int drag) -> Engine::Vector4 {
			bool a = (drag == axis);
			switch (axis) {
			case 0:
				return a ? Engine::Vector4{1, .6f, .6f, 1} : Engine::Vector4{1, .2f, .2f, 1};
			case 1:
				return a ? Engine::Vector4{.6f, 1, .6f, 1} : Engine::Vector4{.2f, 1, .2f, 1};
			case 2:
				return a ? Engine::Vector4{.6f, .6f, 1, 1} : Engine::Vector4{.2f, .2f, 1, 1};
			default:
				return {1, 1, 1, 1};
			}
		};
		auto cX = axCol(0, dAxis), cY = axCol(1, dAxis), cZ = axCol(2, dAxis);

		if (currentGizmoMode == GizmoMode::Translate) {
			drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, ar * .4f, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, -ar * .4f, 0}, cX);
			drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
			drawLocalLine({0, al, 0}, {ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, al, 0}, {-ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
			drawLocalLine({0, 0, al}, {0, ar * .4f, al - ar}, cZ);
			drawLocalLine({0, 0, al}, {0, -ar * .4f, al - ar}, cZ);
		} else if (currentGizmoMode == GizmoMode::Rotate) {
			const int seg = 32;
			const float rad = 1.5f;
			for (int i = 0; i < seg; ++i) {
				float a0 = (float)i / seg * DirectX::XM_2PI, a1 = (float)(i + 1) / seg * DirectX::XM_2PI;
				drawLocalLine({0, cosf(a0) * rad, sinf(a0) * rad}, {0, cosf(a1) * rad, sinf(a1) * rad}, cX);
				drawLocalLine({cosf(a0) * rad, 0, sinf(a0) * rad}, {cosf(a1) * rad, 0, sinf(a1) * rad}, cY);
				drawLocalLine({cosf(a0) * rad, sinf(a0) * rad, 0}, {cosf(a1) * rad, sinf(a1) * rad, 0}, cZ);
			}
		} else {
			float e = 0.15f;
			drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
			drawLocalLine({al - e, -e, 0}, {al + e, e, 0}, cX);
			drawLocalLine({al + e, -e, 0}, {al - e, e, 0}, cX);
			drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
			drawLocalLine({-e, al - e, 0}, {e, al + e, 0}, cY);
			drawLocalLine({e, al - e, 0}, {-e, al + e, 0}, cY);
			drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
			drawLocalLine({0, -e, al - e}, {0, e, al + e}, cZ);
			drawLocalLine({0, e, al - e}, {0, -e, al + e}, cZ);
		}
	}
}

void GameScene::DrawEditorGizmos() {
	if (!renderer_)
		return;
	const float gridSize = 100.0f, step = 1.0f;
	for (float i = -gridSize; i <= gridSize; i += step) {
		if (std::fabs(i) < 0.01f)
			continue;
		bool isMajor = std::fmod(std::fabs(i), 10.0f) < 0.01f;
		float alpha = isMajor ? 0.35f : 0.15f;
		Engine::Vector4 gc = {0.6f, 0.6f, 0.6f, alpha};
		renderer_->DrawLine3D({-gridSize, 0.0f, i}, {gridSize, 0.0f, i}, gc, false);
		renderer_->DrawLine3D({i, 0.0f, -gridSize}, {i, 0.0f, gridSize}, gc, false);
	}
	renderer_->DrawLine3D({-gridSize, 0.0f, 0.0f}, {gridSize, 0.0f, 0.0f}, {0.8f, 0.2f, 0.2f, 0.7f}, false);
	renderer_->DrawLine3D({0.0f, 0.0f, -gridSize}, {0.0f, 0.0f, gridSize}, {0.2f, 0.2f, 0.8f, 0.7f}, false);
	renderer_->DrawLine3D({0, 0, 0}, {1.5f, 0, 0}, {1.f, 0.2f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 1.5f, 0}, {0.2f, 1.f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 0, 1.5f}, {0.2f, 0.2f, 1.f, 1.f}, true);
}

void GameScene::DrawEditor() {
#ifdef USE_IMGUI
	EditorUI::Show(renderer_, this);
	particleEditor_.DrawUI();
#endif
}

void GameScene::DrawLightGizmos() {
	if (!renderer_)
		return;
	for (size_t i = 0; i < objects_.size(); ++i) {
		auto& obj = objects_[i];
		Engine::Vector3 pos = {obj.translate.x, obj.translate.y, obj.translate.z};
		Engine::Matrix4x4 mat = obj.GetTransform().ToMatrix();
		Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
		bool isSelected = (selectedIndices_.find((int)i) != selectedIndices_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		for (const auto& dl : obj.directionalLights) {
			if (!dl.enabled)
				continue;
			Engine::Vector4 col = {1.0f, 0.9f, 0.2f, alpha};
			renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
			float s = 0.5f;
			renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
			renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
		}
		for (const auto& pl : obj.pointLights) {
			if (!pl.enabled)
				continue;
			Engine::Vector4 col = {0.2f, 0.9f, 0.2f, alpha};
			float s = 0.5f;
			renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
			renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
			renderer_->DrawLine3D({pos.x, pos.y, pos.z - s}, {pos.x, pos.y, pos.z + s}, col, true);
		}
		for (const auto& sl : obj.spotLights) {
			if (!sl.enabled)
				continue;
			Engine::Vector4 col = {0.2f, 0.8f, 1.0f, alpha};
			renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
			float s = 0.5f;
			renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
			renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
		}
	}
}

} // namespace Game