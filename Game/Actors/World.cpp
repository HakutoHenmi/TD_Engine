// Game/World.cpp
#define NOMINMAX

#include "World.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

#include "../ObjectTypes.h"
#include "../gimmick/GimmickBase.h"
#include "../gimmick/GimmickFactory.h"

namespace Game {

using namespace DirectX;

static void GetObjectAABB(const Engine::GameObject& obj, Engine::Vector3& outMin, Engine::Vector3& outMax) {
	Engine::Vector3 s = obj.transform.scale;
	Engine::Vector3 t = obj.transform.translate;
	float minX = t.x + obj.localAABBMin.x * s.x;
	float minY = t.y + obj.localAABBMin.y * s.y;
	float minZ = t.z + obj.localAABBMin.z * s.z;
	float maxX = t.x + obj.localAABBMax.x * s.x;
	float maxY = t.y + obj.localAABBMax.y * s.y;
	float maxZ = t.z + obj.localAABBMax.z * s.z;
	outMin = {std::min(minX, maxX), std::min(minY, maxY), std::min(minZ, maxZ)};
	outMax = {std::max(minX, maxX), std::max(minY, maxY), std::max(minZ, maxZ)};
}

static bool IntersectRayAABB(const Engine::Vector3& origin, const Engine::Vector3& dir, const Engine::Vector3& minBox, const Engine::Vector3& maxBox, float& t) {
	float tmin = 0.0f;
	float tmax = std::numeric_limits<float>::infinity();
	auto checkAxis = [&](float oVal, float dVal, float minV, float maxV) {
		if (std::abs(dVal) < 1e-6) {
			if (oVal < minV || oVal > maxV)
				return false;
		} else {
			float invD = 1.0f / dVal;
			float t1 = (minV - oVal) * invD;
			float t2 = (maxV - oVal) * invD;
			tmin = (std::max)(tmin, (std::min)(t1, t2));
			tmax = (std::min)(tmax, (std::max)(t1, t2));
		}
		return true;
	};
	if (!checkAxis(origin.x, dir.x, minBox.x, maxBox.x))
		return false;
	if (!checkAxis(origin.y, dir.y, minBox.y, maxBox.y))
		return false;
	if (!checkAxis(origin.z, dir.z, minBox.z, maxBox.z))
		return false;
	if (tmax < tmin)
		return false;
	t = tmin;
	return true;
}

void World::Initialize(Engine::Renderer* renderer) {
	renderer_ = renderer;
	objects_.reserve(4096);
	checkTex_ = renderer->LoadTexture2D("Resources/uvChecker.png");
	cubeMesh_ = GetMeshHandleFromFile("cube/cube.obj");
	ballMesh_ = GetMeshHandleFromFile("Slope/Slope.obj");
	longFloorMesh_ = GetMeshHandleFromFile("LongFloor/LongFloor.obj");

	std::ifstream file("Resources/level_data.csv");
	if (file.good())
		Load("Resources/level_data.csv");
	else {
		CreateObject(Game::ObjectType::Cube, {0, -2, 0});
		objects_.back().name = "Floor";
		objects_.back().transform.scale = {10, 0.1f, 10};
	}
}

uint32_t World::GetMeshHandleFromFile(const std::string& filename) {
	std::string key = filename;
	if (meshCache_.find(key) != meshCache_.end())
		return meshCache_[key];
	std::string loadPath = "Resources/" + filename;
	if (!std::filesystem::exists(loadPath)) {
		std::filesystem::path p(filename);
		std::string fallback = "Resources/" + p.filename().string();
		if (std::filesystem::exists(fallback))
			loadPath = fallback;
		else
			return cubeMesh_;
	}
	uint32_t handle = renderer_->LoadObjMesh(loadPath);
	if (handle == 0 && filename != "cube/cube.obj")
		return cubeMesh_;
	meshCache_[key] = handle;
	return handle;
}

void World::CalculateAndCacheBounds(const std::string& filename, Engine::Vector3& outMin, Engine::Vector3& outMax) {
	if (collisionCache_.find(filename) != collisionCache_.end()) {
		const auto& mesh = collisionCache_[filename];
		XMVECTOR minV = XMVectorSet(1e9f, 1e9f, 1e9f, 1);
		XMVECTOR maxV = XMVectorSet(-1e9f, -1e9f, -1e9f, 1);
		for (const auto& v : mesh.vertices) {
			minV = XMVectorMin(minV, v);
			maxV = XMVectorMax(maxV, v);
		}
		XMStoreFloat3((XMFLOAT3*)&outMin, minV);
		XMStoreFloat3((XMFLOAT3*)&outMax, maxV);
		return;
	}
	std::string loadPath = "Resources/" + filename;
	if (!std::filesystem::exists(loadPath))
		loadPath = "Resources/" + std::filesystem::path(filename).filename().string();
	std::ifstream file(loadPath);
	if (!file.is_open()) {
		outMin = {-1, -1, -1};
		outMax = {1, 1, 1};
		return;
	}

	CollisionMeshData meshData;
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty())
			continue;
		if (line[0] == 'v' && line[1] == ' ') {
			std::stringstream ss(line.substr(2));
			float x, y, z;
			ss >> x >> y >> z;
			x = -x;
			meshData.vertices.push_back(XMVectorSet(x, y, z, 1.0f));
		} else if (line[0] == 'f' && line[1] == ' ') {
			std::stringstream ss(line.substr(2));
			std::string segment;
			std::vector<int> faceIndices;
			while (std::getline(ss, segment, ' ')) {
				if (segment.empty())
					continue;
				size_t slashPos = segment.find('/');
				try {
					int idx = std::stoi((slashPos != std::string::npos) ? segment.substr(0, slashPos) : segment);
					if (idx > 0)
						idx -= 1;
					faceIndices.push_back(idx);
				} catch (...) {
				}
			}
			if (faceIndices.size() >= 3) {
				for (size_t i = 1; i < faceIndices.size() - 1; ++i) {
					meshData.indices.push_back(faceIndices[0]);
					meshData.indices.push_back(faceIndices[i + 1]);
					meshData.indices.push_back(faceIndices[i]);
				}
			}
		}
	}
	XMVECTOR minV = XMVectorSet(1e9f, 1e9f, 1e9f, 1);
	XMVECTOR maxV = XMVectorSet(-1e9f, -1e9f, -1e9f, 1);
	if (meshData.vertices.empty()) {
		meshData.vertices.push_back(XMVectorSet(-1, -1, -1, 1));
		meshData.vertices.push_back(XMVectorSet(1, 1, 1, 1));
		meshData.indices = {0, 1, 0};
		minV = XMVectorSet(-1, -1, -1, 1);
		maxV = XMVectorSet(1, 1, 1, 1);
	} else {
		for (const auto& v : meshData.vertices) {
			minV = XMVectorMin(minV, v);
			maxV = XMVectorMax(maxV, v);
		}
	}
	XMStoreFloat3((XMFLOAT3*)&outMin, minV);
	XMStoreFloat3((XMFLOAT3*)&outMax, maxV);
	collisionCache_[filename] = meshData;
}

void World::Draw(Engine::Renderer* renderer) {
	for (const auto& obj : objects_) {
		if (obj.isVisible && obj.meshHandle != 0)
			renderer->DrawMesh(obj.meshHandle, obj.textureHandle, obj.transform, obj.color, obj.shaderName);
	}
}

void World::CreateObject(Game::ObjectType type, const Engine::Vector3& pos) {
	Engine::GameObject obj;
	obj.transform.translate = pos;
	obj.transform.scale = {1, 1, 1};
	obj.textureHandle = checkTex_;
	obj.type = static_cast<uint32_t>(type);
	obj.shaderName = "Default";
	std::string meshFileName = "";
	switch (type) {
	case Game::ObjectType::Cube:
		obj.name = "Cube";
		obj.meshHandle = cubeMesh_;
		meshFileName = "cube/cube.obj";
		break;
	case Game::ObjectType::Slope:
		obj.name = "Slope";
		obj.meshHandle = ballMesh_;
		meshFileName = "Slope/Slope.obj";
		break;
	case Game::ObjectType::LongFloor:
		obj.name = "LongFloor";
		obj.meshHandle = longFloorMesh_;
		meshFileName = "LongFloor/LongFloor.obj";
		break;
	case Game::ObjectType::Ball:
		obj.name = "Ball";
		obj.meshHandle = ballMesh_;
		meshFileName = "Slope/Slope.obj";
		break;
	default:
		obj.name = "Unknown";
		obj.meshHandle = cubeMesh_;
		break;
	}
	if (!meshFileName.empty()) {
		CalculateAndCacheBounds(meshFileName, obj.localAABBMin, obj.localAABBMax);
		if (collisionCache_.find(meshFileName) != collisionCache_.end())
			obj.collisionMesh = &collisionCache_[meshFileName];
	}
	int count = 0;
	std::string baseName = obj.name;
	std::string newName = baseName;
	while (true) {
		bool exists = false;
		for (const auto& o : objects_) {
			if (o.name == newName) {
				exists = true;
				break;
			}
		}
		if (!exists)
			break;
		count++;
		newName = baseName + "_" + std::to_string(count);
	}
	obj.name = newName;
	objects_.push_back(obj);
}

void World::CreateObjectFromFile(const std::string& objFileName, const Engine::Vector3& pos) {
	Engine::GameObject obj;
	obj.transform.translate = pos;
	obj.transform.scale = {1, 1, 1};
	obj.textureHandle = checkTex_;
	obj.type = static_cast<uint32_t>(Game::ObjectType::Model);
	obj.shaderName = "Default";
	obj.modelFileName = objFileName;

	obj.meshHandle = GetMeshHandleFromFile(objFileName);
	CalculateAndCacheBounds(objFileName, obj.localAABBMin, obj.localAABBMax);
	if (collisionCache_.find(objFileName) != collisionCache_.end())
		obj.collisionMesh = &collisionCache_[objFileName];
	float bottom = obj.localAABBMin.y;
	obj.transform.translate.y += (0.0f - bottom);
	std::filesystem::path p(objFileName);
	obj.name = p.stem().string();
	int count = 0;
	std::string baseName = obj.name;
	std::string newName = baseName;
	while (true) {
		bool exists = false;
		for (const auto& o : objects_) {
			if (o.name == newName) {
				exists = true;
				break;
			}
		}
		if (!exists)
			break;
		count++;
		newName = baseName + "_" + std::to_string(count);
	}
	obj.name = newName;
	objects_.push_back(obj);
}

void World::DeleteObject(Engine::GameObject* ptr) {
	if (!ptr)
		return;
	auto it = std::find_if(objects_.begin(), objects_.end(), [ptr](const Engine::GameObject& o) { return &o == ptr; });
	if (it != objects_.end())
		objects_.erase(it);
}

Engine::GameObject* World::CastRay(const Engine::Vector3& origin, const Engine::Vector3& dir, float& hitDist, bool ignoreLocked) {
	Engine::GameObject* closestObj = nullptr;
	float minT = std::numeric_limits<float>::infinity();
	Engine::Vector3 minBox, maxBox;
	for (auto& obj : objects_) {
		if (!obj.isVisible)
			continue;
		if (ignoreLocked && obj.isLocked)
			continue;

		GetObjectAABB(obj, minBox, maxBox);
		float t = 0;
		if (IntersectRayAABB(origin, dir, minBox, maxBox, t)) {
			if (t < 0.001f)
				continue;

			if (t < minT) {
				minT = t;
				closestObj = &obj;
			}
		}
	}
	hitDist = minT;
	return closestObj;
}

void World::Save(const std::string& filename) {
	std::ofstream ofs(filename);
	if (!ofs)
		return;
	// ★ヘッダーの最後に GimmickParams を追加
	ofs << "Name,Type,Tx,Ty,Tz,Rx,Ry,Rz,Sx,Sy,Sz,Cr,Cg,Cb,Ca,TexName,minX,minY,minZ,maxX,maxY,maxZ,GimmickName,ShaderName,IsLocked,UseMeshCollision,ModelFileName,GimmickParams\n";
	for (const auto& obj : objects_) {
		std::string gName = obj.gimmickName.empty() ? "NONE" : obj.gimmickName;
		std::string sName = obj.shaderName.empty() ? "Default" : obj.shaderName;

		// ギミックのパラメータを取得
		std::string gParam = "";
		if (obj.gimmick) {
			// ★修正: ギミックのSaveParameter()を呼び出して保存
			gParam = obj.gimmick->SaveParameter();
		}

		ofs << obj.name << "," << obj.type << "," << obj.transform.translate.x << "," << obj.transform.translate.y << "," << obj.transform.translate.z << "," << obj.transform.rotate.x << ","
		    << obj.transform.rotate.y << "," << obj.transform.rotate.z << "," << obj.transform.scale.x << "," << obj.transform.scale.y << "," << obj.transform.scale.z << "," << obj.color.x << ","
		    << obj.color.y << "," << obj.color.z << "," << obj.color.w << "," << (obj.textureName.empty() ? "DEFAULT" : obj.textureName) << "," << obj.localAABBMin.x << "," << obj.localAABBMin.y
		    << "," << obj.localAABBMin.z << "," << obj.localAABBMax.x << "," << obj.localAABBMax.y << "," << obj.localAABBMax.z << "," << gName << "," << sName << "," << (obj.isLocked ? 1 : 0) << ","
		    << (obj.useMeshCollision ? 1 : 0) << "," << obj.modelFileName << "," << gParam << "\n";
	}
}

void World::Load(const std::string& filename) {
	std::ifstream ifs(filename);
	if (!ifs)
		return;
	objects_.clear();
	std::string line;
	std::getline(ifs, line);
	while (std::getline(ifs, line)) {
		if (line.empty())
			continue;
		std::stringstream ss(line);
		std::string segment;
		std::vector<std::string> seglist;
		while (std::getline(ss, segment, ','))
			seglist.push_back(segment);
		if (seglist.size() < 11)
			continue;
		try {
			Game::ObjectType type = (Game::ObjectType)std::stoi(seglist[1]);
			Engine::Vector3 t = {std::stof(seglist[2]), std::stof(seglist[3]), std::stof(seglist[4])};
			Engine::GameObject obj;
			obj.transform.translate = t;
			obj.textureHandle = checkTex_;
			obj.type = static_cast<uint32_t>(type);
			obj.name = seglist[0];
			obj.transform.rotate = {std::stof(seglist[5]), std::stof(seglist[6]), std::stof(seglist[7])};
			obj.transform.scale = {std::stof(seglist[8]), std::stof(seglist[9]), std::stof(seglist[10])};
			obj.shaderName = "Default";
			if (seglist.size() >= 15) {
				obj.color.x = std::stof(seglist[11]);
				obj.color.y = std::stof(seglist[12]);
				obj.color.z = std::stof(seglist[13]);
				obj.color.w = std::stof(seglist[14]);
			}
			if (seglist.size() >= 16) {
				std::string texName = seglist[15];
				if (texName != "DEFAULT" && !texName.empty()) {
					obj.textureName = texName;
					uint32_t h = renderer_->LoadTexture2D("Resources/" + texName);
					if (h != 0)
						obj.textureHandle = h;
				}
			}
			if (seglist.size() >= 22) {
				obj.localAABBMin.x = std::stof(seglist[16]);
				obj.localAABBMin.y = std::stof(seglist[17]);
				obj.localAABBMin.z = std::stof(seglist[18]);
				obj.localAABBMax.x = std::stof(seglist[19]);
				obj.localAABBMax.y = std::stof(seglist[20]);
				obj.localAABBMax.z = std::stof(seglist[21]);
			}
			std::string gName = "";
			if (seglist.size() >= 23) {
				gName = seglist[22];
				gName.erase(std::remove(gName.begin(), gName.end(), '\r'), gName.end());
				gName.erase(std::remove(gName.begin(), gName.end(), '\n'), gName.end());
			}
			if (seglist.size() >= 24) {
				std::string sName = seglist[23];
				sName.erase(std::remove(sName.begin(), sName.end(), '\r'), sName.end());
				sName.erase(std::remove(sName.begin(), sName.end(), '\n'), sName.end());
				if (!sName.empty())
					obj.shaderName = sName;
			}
			if (seglist.size() >= 25) {
				obj.isLocked = (std::stoi(seglist[24]) != 0);
			}
			if (seglist.size() >= 26) {
				obj.useMeshCollision = (std::stoi(seglist[25]) != 0);
			}
			if (seglist.size() >= 27) {
				std::string mFn = seglist[26];
				mFn.erase(std::remove(mFn.begin(), mFn.end(), '\r'), mFn.end());
				mFn.erase(std::remove(mFn.begin(), mFn.end(), '\n'), mFn.end());
				obj.modelFileName = mFn;
			}

			// メッシュのロード処理
			if (type == Game::ObjectType::Cube)
				obj.meshHandle = cubeMesh_;
			else if (type == Game::ObjectType::Slope)
				obj.meshHandle = ballMesh_;
			else if (type == Game::ObjectType::LongFloor)
				obj.meshHandle = longFloorMesh_;
			else if (type == Game::ObjectType::Ball)
				obj.meshHandle = ballMesh_;
			else {
				std::string mPath;
				if (!obj.modelFileName.empty()) {
					mPath = obj.modelFileName;
				} else {
					std::string path = seglist[0];
					size_t u = path.find_last_of('_');
					if (u != std::string::npos && std::isdigit(path[u + 1]))
						path = path.substr(0, u);
					mPath = path + ".obj";
					if (!std::filesystem::exists("Resources/" + mPath)) {
						std::string pPath = "parts/" + mPath;
						if (std::filesystem::exists("Resources/" + pPath))
							mPath = pPath;
					}
				}

				obj.meshHandle = GetMeshHandleFromFile(mPath);
				CalculateAndCacheBounds(mPath, obj.localAABBMin, obj.localAABBMax);
				if (collisionCache_.find(mPath) != collisionCache_.end())
					obj.collisionMesh = &collisionCache_[mPath];
			}

			// プリミティブ形状の衝突判定設定
			std::string mFile = "";
			if (type == Game::ObjectType::Cube)
				mFile = "cube/cube.obj";
			else if (type == Game::ObjectType::Slope)
				mFile = "Slope/Slope.obj";
			else if (type == Game::ObjectType::LongFloor)
				mFile = "LongFloor/LongFloor.obj";
			if (!mFile.empty()) {
				CalculateAndCacheBounds(mFile, obj.localAABBMin, obj.localAABBMax);
				if (collisionCache_.find(mFile) != collisionCache_.end())
					obj.collisionMesh = &collisionCache_[mFile];
			}

			objects_.push_back(obj);
			if (!gName.empty() && gName != "NONE") {
				Engine::GameObject* ptr = &objects_.back();
				Game::GimmickBase* newGimmick = Game::GimmickFactory::Instance().Create(gName);
				if (newGimmick) {
					newGimmick->Start(ptr);
					ptr->gimmick = newGimmick;
					ptr->gimmickName = gName;

					// ★追加: ギミックパラメータの読み込み
					// CSVの残り全てのカラムを結合してパラメータとする（パラメータ内にカンマがある場合への対策）
					if (seglist.size() >= 28) {
						std::string gParam = "";
						for (size_t i = 27; i < seglist.size(); ++i) {
							gParam += seglist[i];
							if (i < seglist.size() - 1)
								gParam += ",";
						}
						// 改行削除
						gParam.erase(std::remove(gParam.begin(), gParam.end(), '\r'), gParam.end());
						gParam.erase(std::remove(gParam.begin(), gParam.end(), '\n'), gParam.end());

						// ★修正: ギミックのLoadParameter()を呼び出して復元
						newGimmick->LoadParameter(gParam);
					}
				}
			}
		} catch (const std::exception& e) {
			OutputDebugStringA(("CSV Load Error: " + std::string(e.what()) + "\n").c_str());
			continue;
		}
	}
}

Engine::GameObject* World::DuplicateObject(Engine::GameObject* src) {
	if (!src)
		return nullptr;
	Engine::GameObject newObj = *src;
	newObj.gimmick = nullptr;
	std::string base = src->name;
	std::string suffix = "_Copy";
	while (base.length() >= suffix.length() && base.compare(base.length() - suffix.length(), suffix.length(), suffix) == 0)
		base = base.substr(0, base.length() - suffix.length());
	std::string root = base;
	int num = 0;
	size_t lastU = base.find_last_of('_');
	if (lastU != std::string::npos) {
		std::string nP = base.substr(lastU + 1);
		if (!nP.empty() && std::all_of(nP.begin(), nP.end(), [](unsigned char c) { return std::isdigit(c); })) {
			try {
				num = std::stoi(nP);
				root = base.substr(0, lastU);
			} catch (...) {
			}
		}
	}
	int newNum = num + 1;
	std::string cName;
	while (true) {
		cName = root + "_" + std::to_string(newNum);
		bool ex = false;
		for (const auto& o : objects_) {
			if (o.name == cName) {
				ex = true;
				break;
			}
		}
		if (!ex)
			break;
		newNum++;
	}
	newObj.name = cName;
	objects_.push_back(newObj);
	Engine::GameObject* ptr = &objects_.back();
	if (src->gimmick) {
		std::string gName = src->gimmick->GetGimmickName();
		Game::GimmickBase* newG = Game::GimmickFactory::Instance().Create(gName);
		if (newG) {
			newG->Start(ptr);

			// ★追加: 複製時もパラメータをコピー
			newG->LoadParameter(src->gimmick->SaveParameter());

			ptr->gimmick = newG;
			ptr->gimmickName = gName;
		}
	}
	return ptr;
}

} // namespace Game