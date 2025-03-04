#include "Config.hpp"

#include <fstream>
#include <filesystem>
#include <system_error>
		

#include "nlohmann/json.hpp"
#include "fmt/base.h"
#include "fmt/color.h"

#include "Logger.hpp"
#include "Constants.hpp"
#include "StopWatch.hpp"

using json = nlohmann::json;

Config::Config(const fs::path &path) {
    StopWatch sw;
    bool echo_config = false;
    try {
        const json json_cfg = json::parse(std::ifstream(path.c_str()));

        const auto io = json_cfg.at("IO");
        universe_infile = fs::path(io.at("universe_infile"));
        universe_outfile = fs::path(io.at("universe_outfile"));
        echo_config = io.at("echo_config");

        const auto sim = json_cfg.at("Simulation");
        timestep = sim.at("timestep");
        iterations = sim.at("iterations");
        algorithm = string_to_enum(sim.at("algorithm"));

        const auto graphics = json_cfg.at("Graphics");
        resolution = {
            graphics.at("resolution").at(0),
            graphics.at("resolution").at(1)
        };
        fps = graphics.at("fps");
        pixel_resolution = graphics.at("pixel_resolution");

        validate();
    }
    catch (const std::exception &e) {
        Log::error(e.what());
        throw std::runtime_error("Failed to parse config: `" + path.string() + "`");
    }

    if (echo_config)
        print();

    Log::debug("Parsed configuration from `{}`: [{}]", path.c_str(), sw);
}

Config::Algorithm Config::string_to_enum(std::string str) {
    for (unsigned short i = 0; const char *s : Constants::ALLOWED_ALGORITHMS) {
        if (str == s) {
            return static_cast<Config::Algorithm>(i);
        }
        i++;
    }
    return static_cast<Config::Algorithm>(-1);
}

std::string Config::enum_to_string(Algorithm algorithm) {
    switch (algorithm)
        {
        case Algorithm::BARNES_HUT:
            return "Barnes Hut";
            break;
        case Algorithm::BRUTE_FORCE:
            return "Brute force";
            break;
        default:
            return "Invalid";
    }
}


void Config::print() {
    const auto c = fmt::color::purple;
    fmt::print(fg(c), "Configuration:\n");
    fmt::print(fg(c), "  universe_infile:  `{}`\n", universe_infile.string());
    fmt::print(fg(c), "  universe_outfile: `{}`\n", universe_outfile.string());
    fmt::print(fg(c), "  timestep:         {}\n", timestep);
    fmt::print(fg(c), "  iterations:       {}\n", iterations);
    fmt::print(fg(c), "  algorithm:        {}\n", enum_to_string(algorithm));
    fmt::print(fg(c), "  resolution:       {}x{}\n", resolution.x, resolution.y);
    fmt::print(fg(c), "  fps:              {}\n", fps);
    fmt::print(fg(c), "  pixel_resolution: {}\n", pixel_resolution);
}

static fs::path resolve_infile_path(const fs::path &infile) {
    const fs::path resolved = fs::canonical(infile);
    if (!fs::is_regular_file(resolved))
        throw std::runtime_error("Path `" + resolved.string() + "` is not a regular file");
    if (const auto perms = fs::status(resolved).permissions(); 
            (perms & fs::perms::owner_read) ==       fs::perms::none) {
        throw std::runtime_error("No read permissions for `" + resolved.string() + "`");
    }
    return resolved;
}

static fs::path resolve_outfile_path(const fs::path &outfile) {
    std::error_code ec;
    if (const fs::path resolved = fs::canonical(outfile, ec); !ec) {
        if (!fs::is_regular_file(resolved))
            throw std::runtime_error("Path `" + resolved.string() + "` is not a regular file");
        if (const auto perms = fs::status(resolved).permissions(); 
                (perms & fs::perms::owner_write) == fs::perms::none) {
            throw std::runtime_error("No write permissions for `" + resolved.string() + "`");
        }
        return resolved;
    }
    const fs::path parent = outfile.parent_path();
    if (const fs::path parent_resolved = fs::canonical(parent, ec); !ec) {
        if (const auto perms = fs::status(parent_resolved).permissions(); 
                (perms & fs::perms::owner_write) == fs::perms::none) {
            throw std::runtime_error("No write permissions for parent directory `" + 
                    parent_resolved.string() + "`");
        }
        return parent_resolved / outfile.filename();
    }
    else 
        throw std::runtime_error("Cannot resolve parent path of `" + outfile.string() + "`");
}

void Config::validate() {
    bool fail = false;
    if (timestep < Constants::MIN_TIMESTEP || timestep > Constants::MAX_TIMESTEP) {
        fail = true;
        Log::error("Config::timestep {} not within allowed range [{}, {}]", timestep, 
                Constants::MIN_TIMESTEP, Constants::MAX_TIMESTEP);
    }
    if (resolution.x > Constants::MAX_WINDOW_WIDTH || 
            resolution.y > Constants::MAX_WINDOW_HEIGHT) {
        fail = true;
        Log::error("Config::resolution {}x{} not within allowed range {}x{}", resolution.x,  
                resolution.y, Constants::MAX_WINDOW_WIDTH, Constants::MAX_WINDOW_HEIGHT);
    }
    if (fps < Constants::MIN_FPS || fps > Constants::MAX_FPS) {
        fail = true;
        Log::error("Config::fps {} not within allowed range [{}, {}]", fps, 
                Constants::MIN_FPS, Constants::MAX_FPS);
    }
    if (pixel_resolution < Constants::MIN_PIXEL_RES ||
            pixel_resolution > Constants::MAX_PIXEL_RES) {
        fail = true;
        Log::error("Config::timestep {} not within allowed range [{}, {}]", pixel_resolution, 
                Constants::MIN_PIXEL_RES, Constants::MAX_PIXEL_RES);
    }
    if (algorithm == static_cast<Config::Algorithm>(-1)) {
        fail = true;
        Log::error("Config::algorithm must be one of allowed values [{}, {}]",
                Constants::ALLOWED_ALGORITHMS[0], Constants::ALLOWED_ALGORITHMS[1]);
    }
    try {
        universe_infile = resolve_infile_path(universe_infile);
    }
    catch (const std::exception &e) {
        fail = true;
        Log::error("Config::universe_infile: {}", e.what());
    }
    try {
        universe_outfile = resolve_outfile_path(universe_outfile);
    }
    catch (const std::exception &e) {
        fail = true;
        Log::error("Config::universe_outfile: {}", e.what());
    }
    if (fail)
        throw std::runtime_error("Config validation failed");
}
