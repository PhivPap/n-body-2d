#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>


// The scenarios that the hub offers: those the generator makes, and the configs in a folder
namespace Hub {
namespace fs = std::filesystem;

// A config's Scenario block, which the generator writes and the simulator ignores
struct ScenarioInfo {
    std::string name{};
    std::string description{};
    std::vector<std::string> tips{};
    std::optional<uint64_t> bodies{};
    std::optional<double> timestep_max_s{};  // none if nothing limits it
    std::string timestep_limited_by{};
    // On the PC the generator's estimates are for, at the config's timestep
    std::optional<double> expected_steps_per_s{};
    std::optional<double> simulated_s_per_s{};
};

// The Scenario block of a config: nullopt if it has none, or can't be read
std::optional<ScenarioInfo> read_scenario_info(const fs::path& config_path);

// A scenario that the generator makes
struct GeneratorEntry {
    std::string name{};
    std::string group{};
    std::string summary{};
};

// The scenarios in the generator's `--list --json` output. Throws std::runtime_error if it isn't
// such a list, or a name isn't a plain file name.
std::vector<GeneratorEntry> parse_generator_list(std::string_view json);

struct Entry {
    enum class Status { READY, NOT_GENERATED, BROKEN };

    std::string name{};     // its config's file name, without ".json"
    std::string group{};    // the generator's, empty for a config that it doesn't make
    std::string summary{};  // the generator's
    Status status = Status::NOT_GENERATED;
    std::string error{};    // why it is broken
    fs::path config{};
    // From a config that can be read: what the hub shows of it
    std::optional<ScenarioInfo> info{};
    std::string algorithm{};
    std::optional<double> theta{};
    std::optional<double> timestep_s{};
    std::optional<double> softening_length_m{};
    std::optional<double> softening_factor{};
    fs::path csv{};  // its bodies
    uintmax_t csv_bytes = 0;
};

// The generator's scenarios in its order, ready if their config is in `folder` with its bodies,
// then the folder's other configs, by name. The configs aren't validated: running them does that.
std::vector<Entry> build_catalogue(const fs::path& folder,
        std::span<const GeneratorEntry> generator);
}  // namespace Hub
