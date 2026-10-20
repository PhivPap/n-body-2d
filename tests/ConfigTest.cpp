#include <fstream>

#include "Check.hpp"
#include "Config/Config.hpp"
#include "Logger/Logger.hpp"
#include "TempDir.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using SimType = Config::Simulation::SimType;
using ColorMode = Config::Graphics::ColorMode;


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
            "show_inspector_panel": true,
            "show_legend_panel": true,
            "panel_update_hz": 1,
            "selection": {"show": true, "show_center_of_mass": true, "follow": false,
                    "show_orbit": true},
            "named_bodies": {"show_labels": true, "scale_by_mass": false},
            "trails_fade_time_s": 3,
            "render": {
                "color_mode": "Groups",
                "exposure_ev": 0.5,
                "bloom": true,
                "groups": {
                    "G1-d": {"color": "#c4d6ff", "brightness": 1.0},
                    "star-0": {"color": "#7D6AA8", "brightness": 0.25}
                }
            }
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
    CHECK(cfg.graphics.show_inspector_panel);
    CHECK(cfg.graphics.selection_show_orbit);
    CHECK(cfg.graphics.named_bodies_show_labels);
    CHECK(!cfg.graphics.named_bodies_scale_by_mass);
    CHECK(cfg.graphics.trails_fade.count() == 3.0);
    CHECK(cfg.graphics.show_legend_panel);
    CHECK(cfg.graphics.color_mode == ColorMode::GROUPS && cfg.graphics.color_mode_str == "Groups");
    CHECK(cfg.graphics.exposure_ev == 0.5);
    CHECK(cfg.graphics.bloom);
    const auto& styles = cfg.graphics.group_styles;
    CHECK(styles.size() == 2);
    CHECK(styles.at("G1-d").color == sf::Color(0xc4, 0xd6, 0xff));
    CHECK(styles.at("G1-d").brightness == 1.f);
    CHECK(styles.at("star-0").color == sf::Color(0x7d, 0x6a, 0xa8));
    CHECK(styles.at("star-0").brightness == 0.25f);
    CHECK(parse(dir, with("/Graphics/render/groups", json::object()))
                    .graphics.group_styles.empty());

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

void test_color_mode_case(const fs::path& dir) {
    const Config cfg = parse(dir, with("/Graphics/render/color_mode", "dOPPLER"));
    CHECK(cfg.graphics.color_mode == ColorMode::DOPPLER);
    CHECK(cfg.graphics.color_mode_str == "Doppler");
    CHECK(parse(dir, with("/Graphics/render/color_mode", "white")).graphics.color_mode
            == ColorMode::WHITE);
    CHECK(parse(dir, with("/Graphics/render/color_mode", "SPEED")).graphics.color_mode
            == ColorMode::SPEED);
    CHECK(parse(dir, with("/Graphics/render/color_mode", "Density")).graphics.color_mode
            == ColorMode::DENSITY);
}

void test_invalid(const fs::path& dir) {
    fmt::println("Invalid configs, the errors below are expected:");
    CHECK_THROWS(parse(dir, without("/IO/echo_config")));
    CHECK_THROWS(parse(dir, without("/Simulation/timestep")));
    CHECK_THROWS(parse(dir, without("/Graphics/selection/follow")));
    CHECK_THROWS(parse(dir, without("/Graphics/show_inspector_panel")));
    CHECK_THROWS(parse(dir, without("/Graphics/selection/show_orbit")));
    CHECK_THROWS(parse(dir, without("/Graphics/named_bodies/show_labels")));
    CHECK_THROWS(parse(dir, without("/Graphics/named_bodies/scale_by_mass")));
    CHECK_THROWS(parse(dir, with("/IO/universe_infile", "missing.csv")));
    CHECK_THROWS(parse(dir, with("/Simulation/algorithm", "Barnes Hut")));
    CHECK_THROWS(parse(dir, with("/Simulation/softening_factor", 0.01)));  // both softenings
    CHECK_THROWS(parse(dir, without("/Simulation/softening_length_m")));  // neither
    CHECK_THROWS(parse(dir, with("/Simulation/softening_length_m", -1.0)));
    CHECK_THROWS(parse(dir, with("/Simulation/timestep", 0.0)));
    CHECK_THROWS(parse(dir, with("/Simulation/timestep", 1e20)));
    CHECK_THROWS(parse(dir, without("/Graphics/render")));
    CHECK_THROWS(parse(dir, without("/Graphics/show_legend_panel")));
    CHECK_THROWS(parse(dir, without("/Graphics/render/groups")));
    CHECK_THROWS(parse(dir, without("/Graphics/render/bloom")));
    CHECK_THROWS(parse(dir, with("/Graphics/render/groups", json::array())));
    CHECK_THROWS(parse(dir, without("/Graphics/render/groups/G1-d/brightness")));
    CHECK_THROWS(parse(dir, with("/Graphics/render/color_mode", "Rainbow")));
    for (const char* color : {"#12345", "red", "#gg0000", "12345678", "#1234567", "#-12345"}) {
        CHECK_THROWS(parse(dir, with("/Graphics/render/groups/G1-d/color", color)));
    }
    CHECK_THROWS(parse(dir, with("/Graphics/render/groups/G1-d/brightness", -0.1)));
    CHECK_THROWS(parse(dir, with("/Graphics/render/groups/G1-d/brightness", 16.1)));
    CHECK_THROWS(parse(dir, with("/Graphics/render/exposure_ev", 10.5)));
    CHECK_THROWS(parse(dir, with("/Graphics/render/exposure_ev", -10.5)));
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
    test_color_mode_case(dir.path);
    test_invalid(dir.path);
    return Check::result();
}
