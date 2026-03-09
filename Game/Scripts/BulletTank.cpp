#include "BulletTank.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {
void BulletTank::Start(SceneObject& obj, GameScene* /*scene*/) { (void)obj; }

void BulletTank::Update(SceneObject& obj, GameScene* scene, float dt) { (void)scene;

obj.rotate.y += rotationSpeed_ * dt;

}

void BulletTank::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}
REGISTER_SCRIPT(BulletTank);
} // namespace Game