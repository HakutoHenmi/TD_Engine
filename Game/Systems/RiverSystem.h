// Engine/RiverSystem.h
#pragma once
#include <vector>
#include <DirectXMath.h>

namespace Engine {
    class Renderer;
}

namespace Game {

struct SceneObject;
struct RiverComponent;

class RiverSystem {
public:
    static void BuildRiverMesh(RiverComponent& river, Engine::Renderer* renderer, const std::vector<SceneObject>& allObjects, const DirectX::XMFLOAT3& ownerPos = {0,0,0});

private:
    static DirectX::XMVECTOR InterpolateSpline(const std::vector<DirectX::XMFLOAT3>& points, float t);
};

} // namespace Game
