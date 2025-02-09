#include "Config.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "Logger.hpp"

using json = nlohmann::json;

Config::Config(const std::string &path) {
    try {
        const json json_cfg = json::parse(std::ifstream(path));

        const auto io = json_cfg.at("IO");
        universe_infile = io.at("universe_infile");
        universe_outfile = io.at("universe_outfile");
        echo_config = io.at("echo_config");


        const auto sim = json_cfg.at("Simulation");
        timestep = sim.at("timestep");
        min_timestep = sim.at("timestep_range").at(0);
        max_timestep = sim.at("timestep_range").at(1);
        iterations = sim.at("iterations");

        const auto graphics = json_cfg.at("Graphics");
        window_width = graphics.at("window_size").at(0);
        window_height = graphics.at("window_size").at(1);
        max_fps = graphics.at("max_fps");
        pixel_resolution = graphics.at("pixel_resolution");
    }
    catch (const std::exception &e) {
        Log::error(e.what());
        throw std::runtime_error("Failed to parse config: " + path);
    }
}
