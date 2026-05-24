#include "PipeScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "Renderer.h"
#include "Camera.h"
#include <cmath>
#include <vector>
#include <DirectXMath.h>

namespace Game {

// ★ マスクを保存するグローバルマップ（隣のパイプが曲がりかどうかを判定するため）
static std::unordered_map<entt::entity, int> g_PipeConnectionMasks;

static bool HasTag(GameScene* scene, entt::entity entity, TagType tagName) {
	if (!scene->GetRegistry().all_of<TagComponent>(entity)) return false;
	return scene->GetRegistry().get<TagComponent>(entity).tag == tagName;
}

static float DistanceBetween(GameScene* scene, entt::entity a, entt::entity b) {
	if (!scene->GetRegistry().all_of<TransformComponent>(a) || !scene->GetRegistry().all_of<TransformComponent>(b)) return 99999.0f;
	
	auto& aTc = scene->GetRegistry().get<TransformComponent>(a);
	auto& bTc = scene->GetRegistry().get<TransformComponent>(b);

	float dx = bTc.translate.x - aTc.translate.x;
   float dy = bTc.translate.y - aTc.translate.y;
	float dz = bTc.translate.z - aTc.translate.z;

  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static bool IsConnectedSphere(GameScene* scene, entt::entity a, entt::entity b, float connectRange) {
	entt::registry& registry = scene->GetRegistry();

	if (!registry.all_of<TransformComponent>(a)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(b)) {
		return false;
	}

	TransformComponent& aTc = registry.get<TransformComponent>(a);
	TransformComponent& bTc = registry.get<TransformComponent>(b);

	float dx = bTc.translate.x - aTc.translate.x;
	float dy = bTc.translate.y - aTc.translate.y;
	float dz = bTc.translate.z - aTc.translate.z;

	float connectRangeSq = connectRange * connectRange;
	float distance3DSq = dx * dx + dy * dy + dz * dz;

	if (distance3DSq <= connectRangeSq) {
		return true;
	}

	float distanceXZSq = dx * dx + dz * dz;
	float heightDifference = std::abs(dy);

	if (heightDifference >= 0.1f) {
		if (distanceXZSq <= connectRangeSq) {
			return true;
		}
	}

	return false;
}
static bool IsAlreadyVisited(const std::vector<entt::entity>& visitedObjects, entt::entity obj) {
	for (size_t i = 0; i < visitedObjects.size(); ++i) {
		if (visitedObjects[i] == obj) {
			return true;
		}
	}
	return false;
}

static bool IsConnectedToBulletTankRecursive(GameScene* scene, entt::entity currentPipe, std::vector<entt::entity>& visitedObjects, float connectRange) {
	visitedObjects.push_back(currentPipe);

	// ★最適化: パイプとタンクのみを対象に走査 (高速タグ検索を利用)
	auto checkEntities = [&](const std::vector<entt::entity>& entities, TagType expectedTag) -> bool {
		for (auto other : entities) {
			if (other == currentPipe) continue;
			if (!scene->GetRegistry().valid(other)) continue;
			if (!IsConnectedSphere(scene, currentPipe, other, connectRange)) continue;

			if (expectedTag == TagType::BulletTank) {
				return true;
			}

			if (expectedTag == TagType::Pipe) {
				if (IsAlreadyVisited(visitedObjects, other)) continue;
				if (IsConnectedToBulletTankRecursive(scene, other, visitedObjects, connectRange)) {
					return true;
				}
			}
		}
		return false;
	};

	if (checkEntities(scene->GetEntitiesByTag(TagType::BulletTank), TagType::BulletTank)) return true;
	if (checkEntities(scene->GetEntitiesByTag(TagType::Pipe), TagType::Pipe)) return true;

	return false;
}

static bool IsConnectedToBulletTank(GameScene* scene, entt::entity selfObj) {
    const float connectRange = 2.5f;

	std::vector<entt::entity> visitedObjects;

	return IsConnectedToBulletTankRecursive(scene, selfObj, visitedObjects, connectRange);
}

// 接続対象となるタグかどうか判定
static bool IsConnectableTag(GameScene* scene, entt::entity entity) {
	return HasTag(scene, entity, TagType::Pipe) ||
	       HasTag(scene, entity, TagType::BulletTank) ||
	       HasTag(scene, entity, TagType::Cannon) ||
	       HasTag(scene, entity, TagType::Canon) ||
		HasTag(scene, entity, TagType::PipeCannon) ||
		   HasTag(scene, entity, TagType::Poison) ||
		   HasTag(scene, entity, TagType::Missile);
}

void PipeScript::Start(entt::entity obj, GameScene* scene) {
	(void)obj;
	if (scene && scene->GetRenderer()) {
		auto* renderer = scene->GetRenderer();
		pipe1Model_ = renderer->LoadObjMesh("Resources/Models/3Dmodel/pipe/pipe1.obj");
		pipe1Tex_ = renderer->LoadTexture2D("Resources/Models/3Dmodel/pipe/pipe1.png");
		pipe2Model_ = renderer->LoadObjMesh("Resources/Models/3Dmodel/pipe/pipe2.obj");
		pipe2Tex_ = renderer->LoadTexture2D("Resources/Models/3Dmodel/pipe/pipe2.png");
		pipe3Model_ = renderer->LoadObjMesh("Resources/Models/3Dmodel/pipe/pipe3.obj");
		pipe3Tex_ = renderer->LoadTexture2D("Resources/Models/3Dmodel/pipe/pipe3.png");
		glowMesh_ = renderer->LoadObjMesh("Resources/Models/plane.obj");
		glowTex_ = renderer->LoadTexture2D("Resources/Textures/particles/diamond_flare.png");
	}

	// タイマーをランダムに分散させて全パイプの同時更新スパイクを防ぐ
	connectionCheckTimer_ = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * connectionCheckInterval_;
}

void PipeScript::Update(entt::entity obj, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	timer_ += dt;
	connectionCheckTimer_ += dt;

	if (connectionCheckTimer_ >= connectionCheckInterval_) {
		connectionCheckTimer_ = 0.0f;

		if (registry.valid(obj) && registry.all_of<TransformComponent>(obj)) {
			const float connectRange = 2.5f;

			currentConnections_.clear();
			currentConnections_.reserve(8);
			allConnections_.clear();
			allConnections_.reserve(8);

			const TagType connectableTags[] = {
				TagType::Pipe, TagType::BulletTank, TagType::Cannon, 
				TagType::Canon, TagType::PipeCannon, TagType::Poison, TagType::Missile
			};

			for (TagType tag : connectableTags) {
				const auto& entities = scene->GetEntitiesByTag(tag);
				for (entt::entity other : entities) {
					if (other == obj) continue;
					if (!registry.valid(other)) continue;
					if (!IsConnectedSphere(scene, obj, other, connectRange)) continue;

					allConnections_.push_back(other);

					bool shouldCreate = false;

					if (tag == TagType::Pipe) {
						if (static_cast<uint32_t>(obj) < static_cast<uint32_t>(other)) {
							shouldCreate = true;
						}
					} else {
						shouldCreate = true;
					}

					if (shouldCreate) {
						currentConnections_.push_back(other);
					}
				}
			}
			
			// 接続状態からパイプの形状（まっすぐ、曲がり、交差）を決定する
			int connectionMask = 0;
			auto& myTc = registry.get<TransformComponent>(obj);
			Engine::Vector3 myPos = {myTc.translate.x, myTc.translate.y, myTc.translate.z};

			for (auto target : allConnections_) {
				if (!registry.valid(target) || !registry.all_of<TransformComponent>(target)) continue;
				auto& tgtTc = registry.get<TransformComponent>(target);
				float dx = tgtTc.translate.x - myPos.x;
				float dz = tgtTc.translate.z - myPos.z;
				if (std::abs(dx) > std::abs(dz)) {
					if (dx > 0) connectionMask |= 1; // +X
					else connectionMask |= 2; // -X
				} else {
					if (dz > 0) connectionMask |= 4; // +Z
					else connectionMask |= 8; // -Z
				}
			}

			uint32_t targetModel = pipe1Model_;
			uint32_t targetTex = pipe1Tex_;
			float targetRotY = 0.0f;
			float targetOffsetX = 0.0f;
			float targetOffsetY = -3.5f; // 直線パイプの完璧な高さ
			float targetOffsetZ = 0.0f;
			float targetScale = 2.0f;

			// 接続状況から描画する方向フラグを初期化
			drawPipeX_ = false;
			drawPipeZ_ = false;

			// ビット数を数えて接続数を判定
			int connectionCount = 0;
			if (connectionMask & 1) connectionCount++;
			if (connectionMask & 2) connectionCount++;
			if (connectionMask & 4) connectionCount++;
			if (connectionMask & 8) connectionCount++;

			// 1. 交差（T字や十字）の場合は、直線パイプを十字に交差させて表現（すっきりして太くならない）
			if (connectionCount >= 3) {
				targetModel = pipe1Model_;
				targetTex = pipe1Tex_;
				targetOffsetY = -3.5f;
				targetScale = 2.0f;
				drawPipeX_ = true;
				drawPipeZ_ = true;
			}
			// 2. 角（L字）の場合は、曲がりパイプ（pipe2）を使用してスッキリ曲げる
			else if (connectionMask == 5 || connectionMask == 6 || connectionMask == 9 || connectionMask == 10) {
				targetModel = pipe2Model_;
				targetTex = pipe2Tex_;
				targetOffsetY = 2.7f; // 曲がりパイプの本来の高さ（浮きを抑えるため 3.1f から 2.7f に下げました）
				targetScale = 1.8f;  // 先ほど決定したベストサイズ
				
				float dx = 1.0f; // 先ほど決定したベストオフセット
				float dz = 1.0f;

				if (connectionMask == 5) {
					targetRotY = 3.14159265f; // 180度反転
					targetOffsetX = dx;
					targetOffsetZ = dz;
				}
				else if (connectionMask == 6) {
					targetRotY = -3.14159265f * 0.5f; // 180度反転
					targetOffsetX = -dz;
					targetOffsetZ = dx;
				}
				else if (connectionMask == 10) {
					targetRotY = 0.0f; // 180度反転
					targetOffsetX = -dx;
					targetOffsetZ = -dz;
				}
				else if (connectionMask == 9) {
					targetRotY = 3.14159265f * 0.5f; // 180度反転
					targetOffsetX = dz;
					targetOffsetZ = -dx;
				}
			}
			// 3. 直線（または端点）
			else {
				targetModel = pipe1Model_;
				targetTex = pipe1Tex_;
				targetOffsetY = -3.5f;
				targetScale = 2.0f;

				if (connectionMask & 1 || connectionMask & 2) {
					drawPipeX_ = true;
				}
				if (connectionMask & 4 || connectionMask & 8) {
					drawPipeZ_ = true;
				}
				if (!drawPipeX_ && !drawPipeZ_) {
					drawPipeX_ = true;
				}
			}

			// 保存しておく
			currentModel_ = targetModel;
			currentTex_ = targetTex;
			currentRotY_ = targetRotY;
			currentScale_ = targetScale;
			currentOffsetX_ = targetOffsetX;
			currentOffsetY_ = targetOffsetY;
			currentOffsetZ_ = targetOffsetZ;

			// 今のマスク状態を保存しておく（描画時に隣をチェックするため）
			g_PipeConnectionMasks[obj] = connectionMask;

			// 標準のメッシュ描画を無効化し、手動で描画するようにする
			if (registry.all_of<MeshRendererComponent>(obj)) {
				const MeshRendererComponent& pipeMesh = registry.get<MeshRendererComponent>(obj);
				pipeColor_ = {pipeMesh.color.x, pipeMesh.color.y, pipeMesh.color.z, pipeMesh.color.w};
				// enabled = false がエンジン側で無視されて二重描画されるのを防ぐため、コンポーネントごと削除する
				registry.remove<MeshRendererComponent>(obj);
			}
		}
	}
}

void PipeScript::Draw(entt::entity obj, GameScene* scene) {
	if (!scene) return;
	auto* renderer = scene->GetRenderer();
	if (!renderer) return;

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(obj) || !registry.all_of<TransformComponent>(obj)) {
		return;
	}

	TransformComponent& objTc = registry.get<TransformComponent>(obj);

	// パイプのMeshRendererカラーはUpdate時に保存したものを使用
	Engine::Vector4 color = pipeColor_;

	// パイプ自体の描画
	if (currentModel_ != 0) {
		if (currentModel_ == pipe2Model_) {
			// 曲がりパイプ（pipe2）の描画
			Engine::Vector3 finalRot = {0.0f, currentRotY_, 0.0f};
			Engine::Matrix4x4 myWorld = Engine::Matrix4x4::MakeAffineMatrix(
				{currentScale_, currentScale_, currentScale_},
				finalRot,
				{objTc.translate.x + currentOffsetX_, objTc.translate.y + currentOffsetY_, objTc.translate.z + currentOffsetZ_}
			);
			renderer->DrawMeshInstanced(currentModel_, currentTex_, myWorld, color, "Toon");
		}
		else {
			// 直線パイプ（pipe1）の描画（交差や直線のフラグ制御）
			float lengthScale = 1.3f; // 直線パイプの長さスケール
			
			// X軸方向の直線パイプ
			if (drawPipeX_) {
				Engine::Vector3 finalRot = {objTc.rotate.x, objTc.rotate.y + 0.0f, objTc.rotate.z};
				Engine::Matrix4x4 myWorld = Engine::Matrix4x4::MakeAffineMatrix(
					{currentScale_, currentScale_, lengthScale},
					finalRot,
					{objTc.translate.x + currentOffsetX_, objTc.translate.y + currentOffsetY_, objTc.translate.z + currentOffsetZ_}
				);
				renderer->DrawMeshInstanced(currentModel_, currentTex_, myWorld, color, "Toon");
			}

			// Z軸方向の直線パイプ
			if (drawPipeZ_) {
				Engine::Vector3 finalRot = {objTc.rotate.x, objTc.rotate.y + 3.14159265f * 0.5f, objTc.rotate.z};
				Engine::Matrix4x4 myWorld = Engine::Matrix4x4::MakeAffineMatrix(
					{currentScale_, currentScale_, lengthScale},
					finalRot,
					{objTc.translate.x + currentOffsetX_, objTc.translate.y + currentOffsetY_, objTc.translate.z + currentOffsetZ_}
				);
				renderer->DrawMeshInstanced(currentModel_, currentTex_, myWorld, color, "Toon");
			}
		}
	}

	// 接続されている全てのパイプ/施設へのシリンダーを描画（ポーズ中も呼ばれる）
	for (size_t i = 0; i < currentConnections_.size(); ++i) {
		entt::entity target = currentConnections_[i];

		if (!registry.valid(target) || !registry.all_of<TransformComponent>(target)) {
			continue;
		}

		TransformComponent& tgtTc = registry.get<TransformComponent>(target);

		Engine::Vector3 startPos = {objTc.translate.x, objTc.translate.y, objTc.translate.z};
		Engine::Vector3 endPos = {tgtTc.translate.x, tgtTc.translate.y, tgtTc.translate.z};

		Engine::Vector3 diff = endPos - startPos;
		float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;

		if (distSq <= 0.0001f) {
			continue;
		}

		float dist = std::sqrt(distSq);
		Engine::Vector3 dir = diff * (1.0f / dist);

		// ★シリンダーモデルは描画しない（パイプモデル自体が形を成すため）
		// renderer->DrawMeshInstanced(cylinderModelHandle_, cylinderTextureHandle_, world, color, "Toon");

		// --- パイプラインに沿ってエネルギーの光点を流す ---
		float flowSpeed = 1.5f;
		int pointCount = static_cast<int>(dist / 1.5f); // 1.5ユニットにつき1つの光点
		if (pointCount < 1) pointCount = 1;

		auto camPosRaw = scene->GetCamera().GetPosition();
		using namespace DirectX;
		XMVECTOR camPos = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(&camPosRaw));

		for (int p = 0; p < pointCount; ++p) {
			float phase = std::fmod(timer_ * flowSpeed + static_cast<float>(p) / pointCount, 1.0f);
			
			Engine::Vector3 pPos = startPos + diff * phase;
			XMFLOAT3 pfPos = {pPos.x, pPos.y, pPos.z};
			XMVECTOR pVec = XMLoadFloat3(&pfPos);
			XMVECTOR toCam = XMVector3Normalize(camPos - pVec);
			XMVECTOR upVec = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			XMVECTOR rightVec = XMVector3Normalize(XMVector3Cross(upVec, toCam));
			upVec = XMVector3Cross(toCam, rightVec);

			float pScale = 0.8f;
			XMMATRIX pm;
			pm.r[0] = rightVec * pScale;
			pm.r[1] = upVec * pScale;
			pm.r[2] = toCam * pScale;
			pm.r[3] = XMVectorSetW(pVec, 1.0f);

			Engine::Matrix4x4 pWorld;
			XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&pWorld), pm);

			Engine::Vector4 pColor = {0.2f, 1.0f, 0.8f, 1.0f}; // タンクと同系色のエネルギー
			renderer->DrawParticleInstanced(glowMesh_, glowTex_, pWorld, pColor, {1.0f, 1.0f, 0.0f, 0.0f}, "ParticleAdditive");
		}
	}
}

void PipeScript::OnDestroy(entt::entity obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

REGISTER_SCRIPT(PipeScript);

} // namespace Game