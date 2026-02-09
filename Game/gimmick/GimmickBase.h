#pragma once
#include "../../Engine/GameObject.h"
#include <string>

namespace Game {

class GimmickBase {
public:
	virtual ~GimmickBase() = default;

	virtual void Start(Engine::GameObject* owner) { owner_ = owner; }

	virtual void Update() {}
	virtual void OnCollision(void* /*other*/) {}
	virtual std::string GetGimmickName() const = 0;

	// Inspectorにパラメータを表示するための関数
	virtual void OnInspectorGUI() {}

	// ★追加: パラメータ保存・読み込み用の仮想関数
	// 各ギミッククラスでオーバーライドして、必要な値をCSV形式などの文字列で返してください
	virtual std::string SaveParameter() { return ""; }
	virtual void LoadParameter(const std::string& /*param*/) {}
	// GimmickBase.h
	virtual void OnBeforeSave() {}

protected:
	Engine::GameObject* owner_ = nullptr;
};

} // namespace Game