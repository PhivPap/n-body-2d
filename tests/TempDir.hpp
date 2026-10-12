#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <system_error>


// A new directory in the system's temporary directory, removed with its contents on destruction
class TempDir {
public:
    const std::filesystem::path path;

    TempDir() : path(create()) {}
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

private:
    static std::filesystem::path create() {
        std::random_device random;
        while (true) {
            const auto dir = std::filesystem::temp_directory_path()
                             / ("n-body-2d-test-" + std::to_string(random()));
            if (std::filesystem::create_directory(dir)) {
                return dir;
            }
        }
    }
};
