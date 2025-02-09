#include "Config.hpp"

#include <fstream>

#include "nlohmann/json.hpp"
#include "fmt/base.h"
#include "fmt/color.h"

#include "Logger.hpp"

using json = nlohmann::json;

Config::Config(const std::string &path) {
    bool echo_config = false;
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

    if (echo_config)
        print();
}

void Config::print() {
    const auto c = fmt::color::purple;
    fmt::print(fg(c), "Configuration:\n");
    fmt::print(fg(c), "  universe_infile:  `{}`\n", universe_infile);
    fmt::print(fg(c), "  universe_outfile: `{}`\n", universe_outfile);
    fmt::print(fg(c), "  timestep:         {}\n", timestep);
    fmt::print(fg(c), "  min_timestep:     {}\n", min_timestep);
    fmt::print(fg(c), "  max_timestep:     {}\n", max_timestep);
    fmt::print(fg(c), "  iterations:       {}\n", iterations);
    fmt::print(fg(c), "  window_width:     {}\n", window_width);
    fmt::print(fg(c), "  window_height:    {}\n", window_height);
    fmt::print(fg(c), "  max_fps:          {}\n", max_fps);
    fmt::print(fg(c), "  pixel_resolution: {}\n", pixel_resolution);
}