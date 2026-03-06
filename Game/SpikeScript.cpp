#include "SpikeScript.h"
#include "../imgui/imgui.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <vector>
namespace Game {

static bool IsConnectedSphere(const SceneObject& a, const SceneObject& b, float connectRange) {
	float dx = b.translate.x - a.translate.x;
	float dy = b.translate.y - a.translate.y;
	float dz = b.translate.z - a.translate.z;

	float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

	if (dist > connectRange) {
		return false;
	}

	return true;
}
// タグ走査
static bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) {
		if (obj.tags[i].tag == tagName) {
			return true;
		}
	}
	return false;
}
void SpikeScript::Start(SceneObject& obj, GameScene* /*scene*/) { (void)obj; }
void SpikeScript::Update(SceneObject& obj, GameScene* scene, float dt) {

	(void)scene;
	


}

void SpikeScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(SpikeScript);

} // namespace Game