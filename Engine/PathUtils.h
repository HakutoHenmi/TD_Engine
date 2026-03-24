#pragma once
#include <string>
#include <filesystem>
#include <windows.h>
#include <algorithm>

namespace Engine {

class PathUtils {
public:
    static std::string GetUnifiedPath(const std::string& relPath) {
        if (relPath.empty()) return "";
        std::filesystem::path p(relPath);
        if (p.is_absolute()) return relPath;
        std::string root = GetRootPath();
        std::filesystem::path finalPath = std::filesystem::path(root) / relPath;
        std::string result = finalPath.string();
        std::replace(result.begin(), result.end(), '\\', '/');
        return result;
    }

    static std::wstring GetUnifiedPathW(const std::wstring& relPath) {
        if (relPath.empty()) return L"";
        std::filesystem::path p(relPath);
        if (p.is_absolute()) return relPath;
        std::string root = GetRootPath();
        std::filesystem::path finalPath = std::filesystem::path(root) / relPath;
        std::wstring result = finalPath.wstring();
        std::replace(result.begin(), result.end(), L'\\', L'/');
        return result;
    }

private:
    static std::string GetRootPath() {
        static std::string rootPath = "";
        if (rootPath.empty()) {
            char buffer[MAX_PATH];
            GetModuleFileNameA(NULL, buffer, MAX_PATH);
            std::filesystem::path exeDir = std::filesystem::path(buffer).parent_path();
            
            std::filesystem::path current = exeDir;
            bool found = false;
            for (int i = 0; i < 6; ++i) {
                // 1. 直下に .sln があるか確認
                if (std::filesystem::exists(current / "DirectXGame_New.sln")) {
                    rootPath = current.string();
                    found = true;
                    break;
                }
                // 2. [追加] 子フォルダに TD_Engine があり、その中に .sln があるか確認 (並列フォルダ対策)
                if (std::filesystem::exists(current / "TD_Engine" / "DirectXGame_New.sln")) {
                    rootPath = (current / "TD_Engine").string();
                    found = true;
                    break;
                }
                
                if (current.has_parent_path()) {
                    current = current.parent_path();
                } else {
                    break;
                }
            }
            if (!found) {
                rootPath = exeDir.string();
            }
        }
        return rootPath;
    }
};

} // namespace Engine
