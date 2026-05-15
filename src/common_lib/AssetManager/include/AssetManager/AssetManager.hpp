#pragma once

#include <string>
#include <filesystem>

#include "SFML/Graphics/Font.hpp"

class AssetManager {
public:
    static AssetManager& instance();

    template<typename Asset> 
    Asset& get(const std::filesystem::path& rel_path);
private:
    template<typename T> 
    using Cache = std::unordered_map<std::string, T>;

    AssetManager();
    template<typename Asset> Asset& get(Cache<Asset>& cache, const std::filesystem::path& rel_path);

    const std::filesystem::path assets_dir;
    Cache<sf::Font> font_cache;
};

template<typename Asset>
Asset& AssetManager::get(const std::filesystem::path& rel_path) {
    if constexpr (std::is_same_v<Asset, sf::Font>) {
        return get(font_cache, rel_path);
    }
    else {
        static_assert(false, "AssetManager: unsupported asset type");
    }
}

template<typename Asset> Asset& AssetManager::get(Cache<Asset>& cache, 
        const std::filesystem::path& rel_path) {
    const auto key = rel_path.string();
    if (auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    auto asset = Asset(assets_dir / rel_path);
    cache.emplace(key, std::move(asset));
    return cache[key];
}
