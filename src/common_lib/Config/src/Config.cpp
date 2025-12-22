#include "Config/Config.hpp"

#include <fstream>
#include <filesystem>
#include <system_error>

#include "nlohmann/json.hpp"
#include "fmt/base.h"
#include "fmt/color.h"

#include "Logger/Logger.hpp"
#include "Constants/Constants.hpp"
#include "StopWatch/StopWatch.hpp"

using json = nlohmann::json;


Config::Config(const fs::path &path) {
    const StopWatch sw;
    bool echo_config = false;
    try {
        const json json_cfg = json::parse(std::ifstream(path.c_str()));

        const auto j_io = json_cfg.at("IO");
        echo_config = j_io.at("echo_config");
        io = IO {
            .universe_infile = fs::path(j_io.at("universe_infile")),
            .universe_outfile = fs::path(j_io.at("universe_outfile")),
            .echo_bodies = j_io.at("echo_bodies")
        };

        const auto j_sim = json_cfg.at("Simulation");
        sim = Simulation {
            .timestep = j_sim.at("timestep"),
            .iterations = j_sim.at("iterations"),
            .simtype_str = j_sim.at("algorithm"),
            .threads = j_sim.at("threads")
        };

        const auto j_graphics = json_cfg.at("Graphics");
        graphics = Graphics {
            .enabled = j_graphics.at("enabled"),
            .resolution = {
                j_graphics.at("resolution").at(0),
                j_graphics.at("resolution").at(1)
            },
            .vsync_enabled = j_graphics.at("vsync"),
            .fps = j_graphics.at("fps"),
            .pixel_scale = j_graphics.at("pixel_scale"),
            .grid_enabled = j_graphics.at("grid_enabled"),
            .panel_update_hz = j_graphics.at("panel_update_hz"),
            .show_panel = j_graphics.at("show_panel")
        };
    }
    catch (const std::exception &e) {
        Log::error("{}", e.what());
        throw std::runtime_error("Failed to parse config: `" + path.string() + "`");
    }

    if (!validate()) {
        throw std::runtime_error("Failed to validate config: `" + path.string() + "`");
    }

    if (echo_config) {
        print();
    }

    Log::debug("Parsed configuration from `{}`: [{}]", path.c_str(), sw);
}

void Config::print() {
    fmt::println("Configuration:{}{}{}", io.to_string(), sim.to_string(), graphics.to_string());
}

static fs::path resolve_infile_path(const fs::path &infile) {
    const fs::path resolved = fs::canonical(infile);
    if (!fs::is_regular_file(resolved)) {
        throw std::runtime_error("Path `" + resolved.string() + "` is not a regular file");
    }
    if (const auto perms = fs::status(resolved).permissions(); 
            (perms & fs::perms::owner_read) ==       fs::perms::none) {
        throw std::runtime_error("No read permissions for `" + resolved.string() + "`");
    }
    return resolved;
}

static fs::path resolve_outfile_path(const fs::path &outfile) {
    std::error_code ec;
    if (const fs::path resolved = fs::canonical(outfile, ec); !ec) {
        if (!fs::is_regular_file(resolved)) {
            throw std::runtime_error("Path `" + resolved.string() + "` is not a regular file");
        }
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
    else {
        throw std::runtime_error("Cannot resolve parent path of `" + outfile.string() + "`");
    }
}

bool Config::validate() {
    constexpr auto symbol = [](bool ok) constexpr {
        return ok ? "✅" : "❌";
    };

    const bool io_cfg_ok = io.validate();
    const bool sim_cfg_ok = sim.validate();
    const bool graphics_cfg_ok = graphics.validate();
    
    Log::info("IO Configuration: {}", symbol(io_cfg_ok));
    Log::info("Simulation Configuration: {}", symbol(sim_cfg_ok));
    Log::info("Graphics Configuration: {}", symbol(graphics_cfg_ok));

    return io_cfg_ok && sim_cfg_ok && graphics_cfg_ok;
}

template <typename T>
bool in_range(const T& value, const std::pair<T, T>& range) {
    return value >= range.first && value <= range.second;
};

template <typename T>
struct fmt::formatter<Range<T>> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(const Range<T>& r, FormatContext& ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "[{}, {}]", r.first, r.second);
    }
};

std::string Config::IO::to_string() const {
    constexpr const char *fmt_str = R"(
  IO:
    universe_infile:  `{}`
    universe_outfile: `{}`
    echo_bodies:      {})";
    return fmt::format(fmt_str, universe_infile.string(), universe_outfile.string(), echo_bodies);
}

bool Config::IO::validate() {
    bool ok = true;
    try {
        universe_infile = resolve_infile_path(universe_infile);
    }
    catch (const std::exception &e) {
        ok = false;
        Log::error("Config::IO::universe_infile: {}", e.what());
    }
    try {
        universe_outfile = resolve_outfile_path(universe_outfile);
    }
    catch (const std::exception &e) {
        ok = false;
        Log::error("Config::IO::universe_outfile: {}", e.what());
    }
    return ok;
}

std::string_view Config::Simulation::simtype_to_string(SimType simtype) {
    switch (simtype) {
    case SimType::BARNES_HUT: return "Barnes-Hut";
    case SimType::NAIVE: return "All Pairs";
    }
    assert(false);
    return {};
}

bool Config::Simulation::parse_simtype() {
    const auto to_lower = [](std::string_view sv) {
        std::string s(sv.size(), 0);
        for (char c : sv) {
            s += std::tolower(c);
        }
        return s;
    };
    const auto simtype_str_lower = to_lower(simtype_str);
    Log::debug("`{}`", simtype_str_lower);
    
    bool ok = true;
    if (simtype_str_lower == to_lower(simtype_to_string(SimType::BARNES_HUT))) {
        simtype = SimType::BARNES_HUT;
    }
    else if (simtype_str_lower == to_lower(simtype_to_string(SimType::NAIVE))) {
        simtype = SimType::NAIVE;
    }
    else {
        ok = false;
    }

    if (ok) {
        simtype_str = simtype_to_string(simtype);
    }

    return ok;
}

std::string Config::Simulation::to_string() const {
    constexpr const char *fmt_str = R"(
  Simulation:
    timestep:         {}
    iterations:       {}
    algorithm:        `{}`
    threads:          {})";
    return fmt::format(fmt_str, timestep, iterations, simtype_str, threads);
}

bool Config::Simulation::validate() {
    using namespace Constants::Simulation;
    bool ok = true;
    if (!in_range(timestep, TIMESTEP_RANGE)) {
        ok = false;
        Log::error("Config::Simulation::timestep {} not within allowed range {}", timestep, 
                TIMESTEP_RANGE);
    }
    if (!parse_simtype()) {
        ok = false;
        Log::error("Config::Simulation::simtype `{}` is not one of the valid options `{}`, `{}`",
                simtype_str, simtype_to_string(SimType::BARNES_HUT), 
                simtype_to_string(SimType::NAIVE));
    }
    if (!in_range(threads, THREADS_RANGE)) {
        ok = false;
        Log::error("Config::Simulation::threads {} not within allowed range {}", threads, 
                THREADS_RANGE);
    }
    return ok;
}

std::string Config::Graphics::to_string() const {
    constexpr const char *fmt_str = R"(
  Graphics:
    enabled:          {}
    resolution:       {}x{}
    vsync_enabled:    {}
    fps:              {}
    pixel_scale:      {}
    grid_enabled:     {}
    show_panel:       {}
    panel_update_hz   {})";
    return fmt::format(fmt_str, enabled, resolution.x, resolution.y, vsync_enabled, fps, 
            pixel_scale, grid_enabled, show_panel, panel_update_hz);
}

bool Config::Graphics::validate() const {
    using namespace Constants::Graphics;
    bool ok = true;
    if (!in_range(resolution.x, WINDOW_WIDTH_RANGE) || 
                !in_range(resolution.y, WINDOW_HEIGHT_RANGE)) {
        ok = false;
        Log::error("Config::Graphics::resolution {}x{} not within allowed range {}x{}", 
                resolution.x, resolution.y, WINDOW_WIDTH_RANGE, WINDOW_HEIGHT_RANGE);
    }
    if (!in_range(fps, FPS_RANGE)) {
        ok = false;
        Log::error("Config::Graphics::fps {} not within allowed range {}", fps, FPS_RANGE);
    }
    if (!in_range(pixel_scale, PIXEL_RES_RANGE)) {
        ok = false;
        Log::error("Config::Graphics::pixel_scale {} not within allowed range {}", pixel_scale, 
                PIXEL_RES_RANGE);
    }
    if (!in_range(panel_update_hz, PANEL_UPDATE_HZ_RANGE)) {
        ok = false;
        Log::error("Config::Graphics::panel_update_hz {} not within allowed range {}", 
                panel_update_hz, PANEL_UPDATE_HZ_RANGE);
    }
    return ok;
}