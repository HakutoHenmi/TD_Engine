#include "PlayerScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <iostream>

namespace Game {

void PlayerScript::Start(SceneObject& obj, GameScene* /*scene*/) {
	// 初期化処理など
	std::cout << "PlayerScript Started: " << obj.name << std::endl;
}

void PlayerScript::Update(SceneObject& obj, GameScene* scene, float dt) {
	// ==========================================
	// キーボード入力 (Releaseビルド対応でGetAsyncKeyStateを使用)
	// ==========================================
	bool keyW = (GetAsyncKeyState('W') & 0x8000) != 0;
	bool keyS = (GetAsyncKeyState('S') & 0x8000) != 0;
	bool keyA = (GetAsyncKeyState('A') & 0x8000) != 0;
	bool keyD = (GetAsyncKeyState('D') & 0x8000) != 0;
	bool keySpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
	bool keyClick = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	bool keyQ = (GetAsyncKeyState('Q') & 0x8000) != 0;
	bool keyE = (GetAsyncKeyState('E') & 0x8000) != 0;

	// 移動処理 (ローカル向きを加味しない絶対軸移動。必要なら sin/cos でローカル化)
	float moveX = 0, moveZ = 0;
	if (keyW)
		moveZ += 1.0f;
	if (keyS)
		moveZ -= 1.0f;
	if (keyA)
		moveX -= 1.0f;
	if (keyD)
		moveX += 1.0f;

	// GameScene側で CharacterMovementComponent 等が有効な場合、こちらでの直接移動は本来不要（競合する）。
	// 今回はスクリプト単体での動作を保証し GameSceneのハードコードをスキップさせるため、CharacterMovementを一時的にオフにしてこちらで動かすなどの措置が必要。
	// とりあえずPlayerScriptとしての自前計算は残し、Y軸は変化させない。

	// ★修正: コンポーネント側との二重移動を防ぐためコメントアウト
	// obj.translate.x += moveX * speed_ * dt;
	// obj.translate.z += moveZ * speed_ * dt;

	// 旋回 (Yaw)
	if (keyQ)
		obj.rotate.y -= 2.0f * dt;
	if (keyE)
		obj.rotate.y += 2.0f * dt;

	// 射撃処理 (クールダウンあり)
	if (shootTimer_ > 0.0f) {
		shootTimer_ -= dt;
	}

	bool isShoot = keyClick || keySpace;
	if (isShoot && shootTimer_ <= 0.0f) {
		// 向いている方向(Yaw)を元に弾を生成する
		SceneObject bullet;
		bullet.name = "Bullet";
		bullet.translate = obj.translate;
		bullet.translate.x += std::sin(obj.rotate.y) * 1.5f;
		bullet.translate.z += std::cos(obj.rotate.y) * 1.5f;
		bullet.rotate = obj.rotate;
		bullet.scale = {0.2f, 0.2f, 0.2f};

		// メッシュとテクスチャの設定
		auto* renderer = scene->GetRenderer();
		if (renderer) {
			bullet.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
			bullet.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");
			MeshRendererComponent mr;
			mr.modelHandle = bullet.modelHandle;
			mr.textureHandle = bullet.textureHandle;
			bullet.meshRenderers.push_back(mr);
		}

		// 当たり判定
		HitboxComponent hb;
		hb.isActive = true;
		hb.damage = 10.0f;
		hb.tag = "Bullet";
		hb.size = {1.0f, 1.0f, 1.0f};
		bullet.hitboxes.push_back(hb);

		// HP
		HealthComponent hc;
		hc.hp = 1.0f;
		hc.maxHp = 1.0f;
		bullet.healths.push_back(hc);

		// タグ
		TagComponent tc;
		tc.tag = "Bullet";
		bullet.tags.push_back(tc);

		// スクリプト
		ScriptComponent sc;
		sc.scriptPath = "BulletScript";
		bullet.scripts.push_back(sc);

		scene->SpawnObject(bullet);
		shootTimer_ = 0.5f; // クールダウン時間
	}
}

void PlayerScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

// ★追加: この1行を書くだけでエンジンに自動認識されます！
REGISTER_SCRIPT(PlayerScript);

} // namespace Game