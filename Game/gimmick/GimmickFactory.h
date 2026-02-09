#pragma once
#include "GimmickBase.h" // 同階層なので ".." は不要
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Game {

class GimmickFactory {
public:
	using CreateFunc = std::function<GimmickBase*()>;

	static GimmickFactory& Instance() {
		static GimmickFactory instance;
		return instance;
	}

	// ギミックを登録する関数
	void Register(const std::string& name, CreateFunc func) {
		registry_[name] = func;
		names_.push_back(name);
	}

	// 名前からギミックを生成する関数
	GimmickBase* Create(const std::string& name) {
		if (registry_.find(name) != registry_.end()) {
			return registry_[name]();
		}
		return nullptr;
	}

	// 登録されているギミック名のリストを取得（エディタ用）
	const std::vector<std::string>& GetGimmickNames() const { return names_; }

private:
	std::map<std::string, CreateFunc> registry_;
	std::vector<std::string> names_;
};

// ★重要: ここに template <typename T> が必要です
template<typename T> struct GimmickRegistrar {
	GimmickRegistrar(const std::string& name) {
		GimmickFactory::Instance().Register(name, []() { return new T(); });
	}
};

} // namespace Game