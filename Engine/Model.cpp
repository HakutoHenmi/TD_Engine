#define NOMINMAX
#include "Model.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "d3dx12.h"
#include <DirectXTex.h>

// Assimp Includes
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace Engine {

static std::wstring ToWide(const std::string& s) {
	if (s.empty())
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	std::wstring w(n - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
	return w;
}

static void SplitPath(const std::string& full, std::string& dir, std::string& file) {
	size_t p = full.find_last_of("/\\");
	if (p == std::string::npos) {
		dir = ".";
		file = full;
	} else {
		dir = full.substr(0, p);
		file = full.substr(p + 1);
	}
}

static Matrix4x4 XMToM4(const DirectX::XMMATRIX& xm) {
	Matrix4x4 out{};
	DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&out), xm);
	return out;
}

// ★修正: Assimp(Column) -> DirectX(Row) 転置 + X軸反転
static Matrix4x4 AiToMat4(const aiMatrix4x4& m) {
	Matrix4x4 out;
	// 転置しつつ、0列目と0行目（0,0以外）を反転させてX軸ミラーリングを行う
	out.m[0][0] = m.a1;
	out.m[0][1] = -m.b1;
	out.m[0][2] = -m.c1;
	out.m[0][3] = -m.d1;
	out.m[1][0] = -m.a2;
	out.m[1][1] = m.b2;
	out.m[1][2] = m.c2;
	out.m[1][3] = m.d2;
	out.m[2][0] = -m.a3;
	out.m[2][1] = m.b3;
	out.m[2][2] = m.c3;
	out.m[2][3] = m.d3;
	out.m[3][0] = -m.a4;
	out.m[3][1] = m.b4;
	out.m[3][2] = m.c4;
	out.m[3][3] = m.d4;
	return out;
}

static void ReadNodeHierarchy(Node& node, const aiNode* src) {
	node.name = src->mName.C_Str();
	node.transform = AiToMat4(src->mTransformation);
	node.children.resize(src->mNumChildren);
	for (unsigned int i = 0; i < src->mNumChildren; ++i) {
		ReadNodeHierarchy(node.children[i], src->mChildren[i]);
	}
}

static void ReadAnimation(ModelData& modelData, const aiScene* scene) {
	for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
		aiAnimation* srcAnim = scene->mAnimations[i];
		Animation dstAnim;
		dstAnim.name = srcAnim->mName.C_Str();
		dstAnim.duration = (float)srcAnim->mDuration;
		dstAnim.ticksPerSecond = (srcAnim->mTicksPerSecond != 0) ? (float)srcAnim->mTicksPerSecond : 25.0f;

		for (unsigned int j = 0; j < srcAnim->mNumChannels; ++j) {
			aiNodeAnim* channel = srcAnim->mChannels[j];
			NodeAnimation nodeAnim;
			// Translation: X反転
			for (unsigned int k = 0; k < channel->mNumPositionKeys; ++k) {
				aiVector3D v = channel->mPositionKeys[k].mValue;
				nodeAnim.translations.push_back({
				    (float)channel->mPositionKeys[k].mTime, {-v.x, v.y, v.z}
                });
			}
			// Rotation: Y, Z反転 (X軸ミラー)
			for (unsigned int k = 0; k < channel->mNumRotationKeys; ++k) {
				aiQuaternion q = channel->mRotationKeys[k].mValue;
				nodeAnim.rotations.push_back({
				    (float)channel->mRotationKeys[k].mTime, {q.x, -q.y, -q.z, q.w}
                });
			}
			// Scale: そのまま
			for (unsigned int k = 0; k < channel->mNumScalingKeys; ++k) {
				aiVector3D v = channel->mScalingKeys[k].mValue;
				nodeAnim.scales.push_back({
				    (float)channel->mScalingKeys[k].mTime, {v.x, v.y, v.z}
                });
			}
			dstAnim.nodeAnimations[channel->mNodeName.C_Str()] = nodeAnim;
		}
		modelData.animations.push_back(dstAnim);
	}
}

static Vector3 CalculateTranslation(const std::vector<Keyframe<XMFLOAT3>>& keys, float time) {
	if (keys.empty())
		return {0, 0, 0};
	if (keys.size() == 1 || time <= keys.front().time)
		return {keys.front().value.x, keys.front().value.y, keys.front().value.z};
	for (size_t i = 0; i < keys.size() - 1; ++i) {
		if (time >= keys[i].time && time <= keys[i + 1].time) {
			float t = (time - keys[i].time) / (keys[i + 1].time - keys[i].time);
			XMVECTOR p = XMVectorLerp(XMLoadFloat3(&keys[i].value), XMLoadFloat3(&keys[i + 1].value), t);
			Vector3 res;
			XMStoreFloat3((XMFLOAT3*)&res, p);
			return res;
		}
	}
	return {keys.back().value.x, keys.back().value.y, keys.back().value.z};
}

static Vector3 CalculateScale(const std::vector<Keyframe<XMFLOAT3>>& keys, float time) {
	if (keys.empty())
		return {1, 1, 1};
	if (keys.size() == 1 || time <= keys.front().time)
		return {keys.front().value.x, keys.front().value.y, keys.front().value.z};
	for (size_t i = 0; i < keys.size() - 1; ++i) {
		if (time >= keys[i].time && time <= keys[i + 1].time) {
			float t = (time - keys[i].time) / (keys[i + 1].time - keys[i].time);
			XMVECTOR s = XMVectorLerp(XMLoadFloat3(&keys[i].value), XMLoadFloat3(&keys[i + 1].value), t);
			Vector3 res;
			XMStoreFloat3((XMFLOAT3*)&res, s);
			return res;
		}
	}
	return {keys.back().value.x, keys.back().value.y, keys.back().value.z};
}

static XMFLOAT4 CalculateRotation(const std::vector<Keyframe<XMFLOAT4>>& keys, float time) {
	if (keys.empty())
		return {0, 0, 0, 1};
	if (keys.size() == 1 || time <= keys.front().time)
		return keys.front().value;
	for (size_t i = 0; i < keys.size() - 1; ++i) {
		if (time >= keys[i].time && time <= keys[i + 1].time) {
			float t = (time - keys[i].time) / (keys[i + 1].time - keys[i].time);
			XMVECTOR q = XMQuaternionSlerp(XMLoadFloat4(&keys[i].value), XMLoadFloat4(&keys[i + 1].value), t);
			XMFLOAT4 res;
			XMStoreFloat4(&res, q);
			return res;
		}
	}
	return keys.back().value;
}

ComPtr<ID3D12Resource> Model::CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
	D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
	D3D12_RESOURCE_DESC rd{
	    D3D12_RESOURCE_DIMENSION_BUFFER, 0, (UINT64)sizeInBytes, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
               D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE
    };
	ComPtr<ID3D12Resource> res;
	device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
	return res;
}

ComPtr<ID3D12Resource> Model::CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& m) {
	D3D12_RESOURCE_DESC rd{
	    D3D12_RESOURCE_DIMENSION(m.dimension), 0, (UINT64)m.width, (UINT)m.height, (UINT16)m.arraySize, (UINT16)m.mipLevels, m.format, {1, 0},
               D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE
    };
	D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
	ComPtr<ID3D12Resource> tex;
	device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
	return tex;
}

ComPtr<ID3D12Resource> Model::UploadTextureData(ID3D12Resource* tex, const DirectX::ScratchImage& mip, ID3D12Device* dev, ID3D12GraphicsCommandList* cmd) {
	std::vector<D3D12_SUBRESOURCE_DATA> subs;
	const DirectX::Image* imgs = mip.GetImages();
	for (size_t i = 0; i < mip.GetImageCount(); ++i)
		subs.push_back({imgs[i].pixels, (LONG_PTR)imgs[i].rowPitch, (LONG_PTR)imgs[i].slicePitch});
	ComPtr<ID3D12Resource> inter = CreateBufferResource(dev, GetRequiredIntermediateSize(tex, 0, (UINT)subs.size()));
	UpdateSubresources(cmd, tex, inter.Get(), 0, 0, (UINT)subs.size(), subs.data());
	D3D12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmd->ResourceBarrier(1, &b);
	return inter;
}

bool Model::Load(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, const std::string& objPath) {
	std::string dir, file;
	SplitPath(objPath, dir, file);
	Assimp::Importer importer;

	// ★修正: 自動変換(ConvertToLeftHanded)を削除。
	// 代わりに FlipWindingOrder と FlipUVs を使用し、座標は手動で反転する
	const unsigned int flags = aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_Triangulate | aiProcess_LimitBoneWeights;

	const aiScene* scene = importer.ReadFile((dir + "/" + file).c_str(), flags);
	if (!scene || !scene->mRootNode)
		return false;

	ReadNodeHierarchy(data_.rootNode, scene->mRootNode);
	if (scene->HasAnimations())
		ReadAnimation(data_, scene);

	uint32_t vertexOffset = 0;
	for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
		aiMesh* mesh = scene->mMeshes[m];
		for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
			VertexData v{};
			// ★修正: 手動X反転 (World.cppと一致させる)
			v.position = {mesh->mVertices[i].x * -1.0f, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f};
			if (mesh->HasNormals())
				v.normal = {mesh->mNormals[i].x * -1.0f, mesh->mNormals[i].y, mesh->mNormals[i].z};
			if (mesh->HasTextureCoords(0))
				v.texcoord = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
			data_.vertices.push_back(v);
		}
		for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
			aiFace& face = mesh->mFaces[f];
			data_.indices.push_back(vertexOffset + face.mIndices[0]);
			data_.indices.push_back(vertexOffset + face.mIndices[1]);
			data_.indices.push_back(vertexOffset + face.mIndices[2]);
		}
		if (mesh->HasBones()) {
			for (unsigned int i = 0; i < mesh->mNumBones; ++i) {
				aiBone* bone = mesh->mBones[i];
				std::string bName = bone->mName.C_Str();
				int bIdx = 0;
				if (data_.boneMapping.find(bName) == data_.boneMapping.end()) {
					bIdx = (int)data_.bones.size();
					// AiToMat4でX反転処理済み
					data_.bones.push_back({bName, AiToMat4(bone->mOffsetMatrix), bIdx});
					data_.boneMapping[bName] = bIdx;
				} else
					bIdx = data_.boneMapping[bName];
				for (unsigned int j = 0; j < bone->mNumWeights; ++j) {
					VertexData& v = data_.vertices[vertexOffset + bone->mWeights[j].mVertexId];
					for (int k = 0; k < 4; ++k) {
						if (v.boneWeights[k] == 0.0f) {
							v.boneWeights[k] = bone->mWeights[j].mWeight;
							v.boneIndices[k] = bIdx;
							break;
						}
					}
				}
			}
		}
		vertexOffset += mesh->mNumVertices;
	}

	// Calculate AABB
	if (!data_.vertices.empty()) {
		data_.min = {FLT_MAX, FLT_MAX, FLT_MAX};
		data_.max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
		for (const auto& v : data_.vertices) {
			data_.min.x = (std::min)(data_.min.x, v.position.x);
			data_.min.y = (std::min)(data_.min.y, v.position.y);
			data_.min.z = (std::min)(data_.min.z, v.position.z);
			data_.max.x = (std::max)(data_.max.x, v.position.x);
			data_.max.y = (std::max)(data_.max.y, v.position.y);
			data_.max.z = (std::max)(data_.max.z, v.position.z);
		}
	}

	// ★★★ 修正箇所: テクスチャ読み込み部分 ★★★
	if (scene->mNumMaterials > 0) {
		aiString str;
		aiMaterial* material = scene->mMaterials[0];

		// 1. 従来(Legacy)のDiffuseテクスチャを探す
		if (material->GetTexture(aiTextureType_DIFFUSE, 0, &str) != aiReturn_SUCCESS) {
			// 2. なければglTF(PBR)のBaseColorテクスチャを探す
			material->GetTexture(aiTextureType_BASE_COLOR, 0, &str);
		}

		// パスが見つかった場合のみ設定する
		if (str.length > 0) {
			data_.material.textureFilePath = dir + "/" + str.C_Str();
		}
	}
	// ★★★ 修正終わり ★★★

	vb_ = CreateBufferResource(device, sizeof(VertexData) * data_.vertices.size());
	void* vmap;
	vb_->Map(0, nullptr, &vmap);
	std::memcpy(vmap, data_.vertices.data(), sizeof(VertexData) * data_.vertices.size());
	vb_->Unmap(0, nullptr);
	vbv_ = {vb_->GetGPUVirtualAddress(), (UINT)(sizeof(VertexData) * data_.vertices.size()), sizeof(VertexData)};
	ib_ = CreateBufferResource(device, sizeof(uint32_t) * data_.indices.size());
	void* imap;
	ib_->Map(0, nullptr, &imap);
	std::memcpy(imap, data_.indices.data(), sizeof(uint32_t) * data_.indices.size());
	ib_->Unmap(0, nullptr);
	ibv_ = {ib_->GetGPUVirtualAddress(), (UINT)(sizeof(uint32_t) * data_.indices.size()), DXGI_FORMAT_R32_UINT};
	indexCount_ = (uint32_t)data_.indices.size();

	if (!data_.material.textureFilePath.empty()) {
		ScratchImage mip;
		if (SUCCEEDED(LoadFromWICFile(ToWide(data_.material.textureFilePath).c_str(), WIC_FLAGS_FORCE_SRGB, nullptr, mip))) {
			tex_ = CreateTextureResource(device, mip.GetMetadata());
			upload_ = UploadTextureData(tex_.Get(), mip, device, cmd);
			srvDesc_.Format = mip.GetMetadata().format;
			srvDesc_.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc_.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc_.Texture2D.MipLevels = (UINT)mip.GetMetadata().mipLevels;
			srvDesc_.Texture2D.MostDetailedMip = 0;
			srvDesc_.Texture2D.PlaneSlice = 0;
			srvDesc_.Texture2D.ResourceMinLODClamp = 0.0f;
			hasTexture_ = true;
		}
	}
	return true;
}

void Model::CreateSrv(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT descriptorSize, UINT heapIndex) {
	if (!hasTexture_)
		return;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += (SIZE_T)descriptorSize * heapIndex;
	srvGpu_ = srvHeap->GetGPUDescriptorHandleForHeapStart();
	srvGpu_.ptr += (UINT64)descriptorSize * heapIndex;
	device->CreateShaderResourceView(tex_.Get(), &srvDesc_, cpu);
}

void Model::Draw(ID3D12GraphicsCommandList* cmd, UINT /*root*/) {
	cmd->IASetVertexBuffers(0, 1, &vbv_);
	cmd->IASetIndexBuffer(&ibv_);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void Model::DrawInstanced(ID3D12GraphicsCommandList* cmd, UINT instanceCount, UINT /*root*/) {
	cmd->IASetVertexBuffers(0, 1, &vbv_);
	cmd->IASetIndexBuffer(&ibv_);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawIndexedInstanced(indexCount_, instanceCount, 0, 0, 0);
}

void Model::UpdateSkeleton(const Node& node, const Matrix4x4& parentTransform, const Animation& animation, float time, std::vector<Matrix4x4>& outPalette) {
	Matrix4x4 localTransform = node.transform;
	auto it = animation.nodeAnimations.find(node.name);
	if (it != animation.nodeAnimations.end()) {
		const auto& anim = it->second;
		Vector3 s = CalculateScale(anim.scales, time);
		XMFLOAT4 r = CalculateRotation(anim.rotations, time);
		Vector3 t = CalculateTranslation(anim.translations, time);
		// 一時変数に受けてアドレスエラーを回避
		XMMATRIX m = XMMatrixAffineTransformation(XMLoadFloat3((XMFLOAT3*)&s), XMVectorZero(), XMLoadFloat4(&r), XMLoadFloat3((XMFLOAT3*)&t));
		localTransform = XMToM4(m);
	}
	Matrix4x4 globalTransform = Matrix4x4::Multiply(localTransform, parentTransform);
	if (data_.boneMapping.count(node.name)) {
		int idx = data_.boneMapping[node.name];
		outPalette[idx] = Matrix4x4::Multiply(data_.bones[idx].offsetMatrix, globalTransform);
	}
	for (const auto& child : node.children)
		UpdateSkeleton(child, globalTransform, animation, time, outPalette);
}

} // namespace Engine