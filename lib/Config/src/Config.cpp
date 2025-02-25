#include "Config.hpp"

#include <fstream>
#include <filesystem>

#include "nlohmann/json.hpp"
#include "fmt/base.h"
#include "fmt/color.h"

#include "Logger.hpp"
#include "Constants.hpp"

using json = nlohmann::json;

static std::string parse_path(const std::string& path) {
    return std::filesystem::canonical(std::filesystem::path(path)).string();
}

Config::Config(const std::string &path) {
    bool echo_config = false;
    try {
        const json json_cfg = json::parse(std::ifstream(path));

        const auto io = json_cfg.at("IO");
        universe_infile = parse_path(io.at("universe_infile"));
        universe_outfile = parse_path(io.at("universe_outfile"));
        echo_config = io.at("echo_config");

        const auto sim = json_cfg.at("Simulation");
        timestep = sim.at("timestep");
        iterations = sim.at("iterations");

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
        throw std::runtime_error("Failed to parse config: `" + path + "`");
    }

    if (echo_config)
        print();
}

void Config::print() {
    const auto c = fmt::color::purple;
    fmt::print(fg(c), "Configuration:\n");
    fmt::print(fg(c), "  universe_infile:  `{}`\n", universe_infile);
    fmt::print(fg(c), "  universe_outfile: `{}`\n", universe_outfile);
    fmt::print(fg(c), "  timestep:         {}\n", timestep);
    fmt::print(fg(c), "  iterations:       {}\n", iterations);
    fmt::print(fg(c), "  resolution:       {}x{}\n", resolution.x, resolution.y);
    fmt::print(fg(c), "  fps:              {}\n", fps);
    fmt::print(fg(c), "  pixel_resolution: {}\n", pixel_resolution);
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
    if (fail)
        throw std::runtime_error("Config validation failed");
}
