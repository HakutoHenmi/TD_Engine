#include "ToObjectMove.h"
#include "../imgui/imgui.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"

namespace Game {

static bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) { // タグ配列を最初から最後までループして、指定されたタグがあるか確認
		if (obj.tags[i].tag == tagName) {            // タグが見つかったらtrueを返す
			return true;
		}
	}
	return false; // タグが見つからなかったらfalseを返す
}

void ToObjectMove::Start(SceneObject& /*obj*/, GameScene* scene) {
	// ここに初期設定を記述
	// まだプレイヤーが見つかってなければシーン内のオブジェクトから探す
	if (target_ == nullptr && scene != nullptr) {
		auto& objects = scene->GetObjects();
		for (size_t i = 0; i < objects.size(); ++i) {
			// そのオブジェクトのTagを調べる
			if (HasTag(objects[i], targetName_.c_str())) {
				target_ = &objects[i];
				break;
			}
		}
	}
}

void ToObjectMove::Update(SceneObject& obj, GameScene* scene, float dt) {
	// ここに毎フレームの挙動を記述
	// タグの変更があれば更新
	ChangeTargetTag(obj, scene, dt);

	// ターゲットが存在しなければ止める
	if (target_ == nullptr) {
		return;
	}

	// プレイヤーに向かって移動
	// 自分の位置とプレイヤーの位置を取得
	myPos_.x = obj.GetTransform().translate.x;
	myPos_.y = obj.GetTransform().translate.y;
	myPos_.z = obj.GetTransform().translate.z;
	targetPos_.x = target_->GetTransform().translate.x;
	targetPos_.y = target_->GetTransform().translate.y;
	targetPos_.z = target_->GetTransform().translate.z;

	// プレイヤーへの方向ベクトルを計算 (Target - Self)
	float diffX = targetPos_.x - myPos_.x;
	float diffY = targetPos_.y - myPos_.y;
	float diffZ = targetPos_.z - myPos_.z;

	// 距離を計算（三平方の定理）
	float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

	// ある程度離れている時だけ移動（ピタッと止まらせるならこの値を調整）
	if (distance > 0.1f) {
		// 正規化（方向ベクトルの長さを1にする）
		float dirX = diffX / distance;
		float dirY = diffY / distance;
		float dirZ = diffZ / distance;

		// 新しい位置を計算
		myPos_.x += dirX * speed_ * dt;
		myPos_.y += dirY * speed_ * dt;
		myPos_.z += dirZ * speed_ * dt;

		// オブジェクトの位置を更新
		obj.translate.x = myPos_.x;
		obj.translate.y = myPos_.y;
		obj.translate.z = myPos_.z;
	}
}

void ToObjectMove::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {
	// 終了時のクリーンアップなどを記述
}

void ToObjectMove::ChangeTargetTag(SceneObject& /*obj*/, GameScene* scene, float /*dt*/) {
	if (ImGui::InputText("Target Tag", tagBuffer_, sizeof(tagBuffer_))) {
		targetName_ = tagBuffer_;

		// 再検索の前に一旦クリアする
		target_ = nullptr;

		if (scene != nullptr) {
			auto& objects = scene->GetObjects();
			for (size_t i = 0; i < objects.size(); ++i) {
				// そのオブジェクトのTagを調べる
				if (HasTag(objects[i], targetName_.c_str())) {
					target_ = &objects[i];
					break;
				}
			}
		}
	}
}

// ★ スクリプト自動登録
REGISTER_SCRIPT(ToObjectMove);

} // namespace Game
