#include <fstream>

#include "Check.hpp"
#include "Config/Config.hpp"
#include "Logger/Logger.hpp"
#include "TempDir.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using SimType = Config::Simulation::SimType;


namespace {
// A valid config, whose universe is data/universe.csv next to it
json valid_config() {
    return json::parse(R"({
        "IO": {
            "universe_infile": "data/universe.csv",
            "universe_outfile": "output.csv",
            "echo_config": false,
            "echo_bodies": false
        },
        "Simulation": {
            "timestep": 60,
            "iterations": 1000,
            "theta": 0.5,
            "algorithm": "All Pairs",
            "softening_length_m": 1000,
            "threads": 4
        },
        "Graphics": {
            "enabled": false,
            "resolution": [1600, 900],
            "fps": 100,
            "vsync": false,
            "pixel_scale": 1e9,
            "show_grid": false,
            "show_commands_panel": true,
            "show_config_panel": true,
            "show_stats_panel": true,
            "show_action_log_panel": true,
            "panel_update_hz": 1,
            "selection": {"show": true, "show_center_of_mass": true, "follow": false},
            "trails_fade_time_s": 3
        }
    })");
}

// The valid config with the value at `pointer` set, or removed
json with(const char* pointer, const json& value) {
    json j = valid_config();
    j[json::json_pointer(pointer)] = value;
    return j;
}

json without(const char* pointer) {
    json j = valid_config();
    const json::json_pointer key(pointer);
    j[key.parent_pointer()].erase(key.back());
    return j;
}

// Writes `j` to config.json in `dir` and parses it
Config parse(const fs::path& dir, const json& j) {
    const fs::path path = dir / "config.json";
    std::ofstream(path) << j.dump(4);
    return Config(path);
}

void test_valid(const fs::path& dir) {
    const Config cfg = parse(dir, valid_config());
    CHECK(cfg.sim.timestep == 60.0);
    CHECK(cfg.sim.iterations == 1000);
    CHECK(cfg.sim.simtype == SimType::ALL_PAIRS);
    CHECK(cfg.sim.theta == 0.5);
    CHECK(!cfg.sim.softening_factor);
    CHECK(cfg.sim.softening_length_m == 1000.0);
    CHECK(cfg.sim.threads == 4);
    CHECK(!cfg.graphics.enabled);
    CHECK(cfg.graphics.resolution == sf::Vector2<uint16_t>(1600, 900));
    CHECK(cfg.graphics.trails_fade.count() == 3.0);

    json j = without("/Simulation/softening_length_m");
    j["Simulation"]["softening_factor"] = 0.01;
    const Config cfg_factor = parse(dir, j);
    CHECK(cfg_factor.sim.softening_factor == 0.01);
    CHECK(!cfg_factor.sim.softening_length_m);
}

// Relative universe paths are relative to the config file, not to the working directory
void test_paths(const fs::path& dir) {
    const fs::path universe = fs::canonical(dir / "data" / "universe.csv");
    CHECK(fs::current_path() != fs::canonical(dir));
    const Config cfg = parse(dir, valid_config());
    CHECK(cfg.io.universe_infile == universe);
    CHECK(cfg.io.universe_outfile == fs::canonical(dir) / "output.csv");
    CHECK(parse(dir, with("/IO/universe_infile", universe.string())).io.universe_infile
            == universe);
}

void test_algorithm_case(const fs::path& dir) {
    const Config cfg = parse(dir, with("/Simulation/algorithm", "bARNES-hUT"));
    CHECK(cfg.sim.simtype == SimType::BARNES_HUT);
    CHECK(cfg.sim.simtype_str == "Barnes-Hut");
    CHECK(parse(dir, with("/Simulation/algorithm", "all pairs")).sim.simtype
            == SimType::ALL_PAIRS);
#ifdef NBODY_CUDA
    CHECK(parse(dir, with("/Simulation/algorithm", "BARNES-HUT GPU")).sim.simtype
            == SimType::BARNES_HUT_GPU);
#else
    CHECK_THROWS(parse(dir, with("/Simulation/algorithm", "BARNES-HUT GPU")));
#endif
}

void test_invalid(const fs::path& dir) {
    fmt::println("Invalid configs, the errors below are expected:");
    CHECK_THROWS(parse(dir, without("/IO/echo_config")));
    CHECK_THROWS(parse(dir, without("/Simulation/timestep")));
    CHECK_THROWS(parse(dir, without("/Graphics/selection/follow")));
    CHECK_THROWS(parse(dir, with("/IO/universe_infile", "missing.csv")));
    CHECK_THROWS(parse(dir, with("/Simulation/algorithm", "Barnes Hut")));
    CHECK_THROWS(parse(dir, with("/Simulation/softening_factor", 0.01)));  // both softenings
    CHECK_THROWS(parse(dir, without("/Simulation/softening_length_m")));  // neither
    CHECK_THROWS(parse(dir, with("/Simulation/softening_length_m", -1.0)));
    CHECK_THROWS(parse(dir, with("/Simulation/timestep", 0.0)));
    CHECK_THROWS(parse(dir, with("/Simulation/timestep", 1e20)));
}
}  // namespace


int main() {
    Log::verbosity = Log::Verbosity::WARNING;
    const TempDir dir;
    fs::create_directory(dir.path / "data");
    std::ofstream(dir.path / "data" / "universe.csv") << "id,mass,x,y,vel_x,vel_y\n"
                                                      << "sun,2e30,0,0,0,0\n";

    test_valid(dir.path);
    test_paths(dir.path);
    test_algorithm_case(dir.path);
    test_invalid(dir.path);
    return Check::result();
}
