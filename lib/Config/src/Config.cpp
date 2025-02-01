#include "Config.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

#include "logger.hpp"

using json = nlohmann::json;

Config::Config(const std::string &path, bool print) {
    try {
        const json json_cfg = json::parse(std::ifstream(path));
        universe_infile = json_cfg["universe_infile"];
        universe_outfile = json_cfg["universe_outfile"];
        timestep = json_cfg["timestep"];
        min_timestep = json_cfg["min_timestep"];
        max_timestep = json_cfg["max_timestep"];
        iterations = json_cfg["iterations"];
        max_fps = json_cfg["max_fps"];
    }
    catch (const std::exception &e) {
        Log::error(e.what());
        throw std::runtime_error("Failed to parse config: " + path);
    }
}
