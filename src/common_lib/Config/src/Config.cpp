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
            .algorithm = string_to_enum(j_sim.at("algorithm")),
            .threads = j_sim.at("threads"),
            .stats_update_hz = j_sim.at("stats_update_hz")
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
            .grid_enabled = j_graphics.at("grid_enabled")
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

Config::Simulation::Algorithm Config::string_to_enum(const std::string& str_alg) {
    for (unsigned short i = 0; const char *s : Constants::Simulation::ALLOWED_ALGORITHMS) {
        if (str_alg == s) {
            return static_cast<Config::Simulation::Algorithm>(i);
        }
        i++;
    }
    return static_cast<Config::Simulation::Algorithm>(-1);
}

std::string Config::enum_to_string(Simulation::Algorithm alg) {
    switch (alg) {
    case Simulation::Algorithm::BARNES_HUT:
        return "Barnes Hut";
    case Simulation::Algorithm::NAIVE:
        return "Naive";
    }
    assert(false);
    return "Invalid";
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
    constexpr const char * fmt_str = R"(
  IO:
    universe_infile:  `{}`
    universe_outfile: `{}`
    echo_bodies:      {})";
    return fmt::format(fmt_str, universe_infile.string(), universe_outfile.string(), echo_bodies);
}

bool Config::IO::validate() {
    bool fail = false;
    try {
        universe_infile = resolve_infile_path(universe_infile);
    }
    catch (const std::exception &e) {
        fail = true;
        Log::error("Config::IO::universe_infile: {}", e.what());
    }
    try {
        universe_outfile = resolve_outfile_path(universe_outfile);
    }
    catch (const std::exception &e) {
        fail = true;
        Log::error("Config::IO::universe_outfile: {}", e.what());
    }
    return !fail;
}

std::string Config::Simulation::to_string() const {
    constexpr const char * fmt_str = R"(
  Simulation:
    timestep:         {}
    iterations:       {}
    algorithm:        `{}`
    threads:          {}
    stats_update_hz:  {})";
    return fmt::format(fmt_str, timestep, iterations, enum_to_string(algorithm), threads, 
            stats_update_hz);
}

bool Config::Simulation::validate() const {
    using namespace Constants::Simulation;
    bool fail = false;
    if (fail |= !in_range(timestep, TIMESTEP_RANGE)) {
        Log::error("Config::Simulation::timestep {} not within allowed range {}", timestep, 
                TIMESTEP_RANGE);
    }
    if (fail |= algorithm == static_cast<Config::Simulation::Algorithm>(-1)) {
        Log::error("Config::Simulation::algorithm must be one of allowed values [{}, {}]",
                ALLOWED_ALGORITHMS[0], ALLOWED_ALGORITHMS[1]);
    }
    if (fail |= !in_range(threads, THREADS_RANGE)) {
        Log::error("Config::Simulation::threads {} not within allowed range {}", threads, 
                THREADS_RANGE);
    }
    if (fail |= !in_range(stats_update_hz, STATS_UPDATE_HZ_RANGE)) {
        Log::error("Config::Simulation::stats_update_hz {} not within allowed range {}", 
                stats_update_hz, STATS_UPDATE_HZ_RANGE);
    }
    return !fail;
}

std::string Config::Graphics::to_string() const {
    constexpr const char * fmt_str = R"(
  Graphics:
    enabled:          {}
    resolution:       {}x{}
    vsync_enabled:    {}
    fps:              {}
    pixel_scale:      {}
    grid_enabled:     {})";
    return fmt::format(fmt_str, enabled, resolution.x, resolution.y, vsync_enabled, fps, 
            pixel_scale, grid_enabled);
}

bool Config::Graphics::validate() const {
    using namespace Constants::Graphics;
    bool fail = false;
    if (fail |= !in_range(resolution.x, WINDOW_WIDTH_RANGE) || 
            !in_range(resolution.y, WINDOW_HEIGHT_RANGE)) {
        Log::error("Config::Graphics::resolution {}x{} not within allowed range {}x{}", resolution.x,
                resolution.y, WINDOW_WIDTH_RANGE, WINDOW_HEIGHT_RANGE);
    }
    if (fail |= !in_range(fps, FPS_RANGE)) {
        Log::error("Config::Graphics::fps {} not within allowed range {}", fps, FPS_RANGE);
    }
    if (fail |= !in_range(pixel_scale, PIXEL_RES_RANGE)) {
        Log::error("Config::Graphics::pixel_scale {} not within allowed range {}", pixel_scale, 
                PIXEL_RES_RANGE);
    }
    return !fail;
}