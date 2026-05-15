#include "AssetManager/AssetManager.hpp"

#include <array>
#include <unistd.h>
#include <limits.h>

static std::filesystem::path executable_dir() {
    std::array<char, PATH_MAX> buffer;
    const auto len = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (len == -1 || len == static_cast<ssize_t>(buffer.size())) {
        throw std::runtime_error("Failed to get executable path");
    }
    return std::filesystem::path(std::string(buffer.data(), len)).parent_path();
}

AssetManager& AssetManager::instance() {
    static AssetManager instance;
    return instance;
}

AssetManager::AssetManager() : assets_dir(executable_dir() / "assets") {
    if (!std::filesystem::exists(assets_dir) || !std::filesystem::is_directory(assets_dir)) {
        throw std::runtime_error("Assets directory `" + assets_dir.string() + 
                "` does not exist or is not a directory");
    }
}
