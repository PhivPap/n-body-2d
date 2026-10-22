#include "Hub/Catalogue.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <system_error>

#include "fmt/format.h"
#include "fmt/std.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;


namespace Hub {
namespace {
std::optional<double> number(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number() ? std::optional(it->get<double>()) : std::nullopt;
}

std::string text(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// An exception's message, without nlohmann::json's "[json.exception.parse_error.101] "
std::string message(const std::exception& e) {
    const std::string_view what = e.what();
    const size_t prefix_end = what.starts_with("[json.exception.") ? what.find("] ") : what.npos;
    return std::string(prefix_end == what.npos ? what : what.substr(prefix_end + 2));
}

json read_json(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error(fmt::format("Can't open {}", path.filename()));
    }
    return json::parse(file);
}

std::optional<ScenarioInfo> scenario_info(const json& config) {
    const auto block = config.find("Scenario");
    if (block == config.end() || !block->is_object()) {
        return std::nullopt;
    }
    ScenarioInfo info{.name = text(*block, "name"),
            .description = text(*block, "description"),
            .timestep_max_s = number(*block, "timestep_max_s"),
            .timestep_limited_by = text(*block, "timestep_limited_by"),
            .expected_steps_per_s = number(*block, "expected_iterations_per_s"),
            .simulated_s_per_s = number(*block, "simulated_seconds_per_second")};
    if (const auto tips = block->find("tips"); tips != block->end() && tips->is_array()) {
        for (const json& tip : *tips) {
            if (tip.is_string()) {
                info.tips.push_back(tip.get<std::string>());
            }
        }
    }
    if (const auto bodies = block->find("bodies");
            bodies != block->end() && bodies->is_number_unsigned()) {
        info.bodies = bodies->get<uint64_t>();
    }
    return info;
}

// What the hub shows of the entry's config, in `folder`, and whether it is ready or broken
void read_config(Entry& entry, const fs::path& folder) {
    try {
        const json config = read_json(entry.config);
        entry.info = scenario_info(config);
        // Relative to the config's folder, as Config resolves it
        entry.csv = folder / config.at("IO").at("universe_infile").get<std::string>();
        if (const auto sim = config.find("Simulation"); sim != config.end() && sim->is_object()) {
            entry.algorithm = text(*sim, "algorithm");
            entry.theta = number(*sim, "theta");
            entry.timestep_s = number(*sim, "timestep");
            entry.softening_length_m = number(*sim, "softening_length_m");
            entry.softening_factor = number(*sim, "softening_factor");
        }
    }
    catch (const std::exception& e) {
        entry.status = Entry::Status::BROKEN;
        entry.error = fmt::format("Not a config that can be read: {}", message(e));
        return;
    }
    std::error_code ec;
    entry.csv_bytes = fs::file_size(entry.csv, ec);
    entry.status = ec ? Entry::Status::BROKEN : Entry::Status::READY;
    if (ec) {
        entry.error = fmt::format("Its bodies, {}, are missing", entry.csv.filename());
    }
}
}  // namespace


std::optional<ScenarioInfo> read_scenario_info(const fs::path& config_path) {
    try {
        return scenario_info(read_json(config_path));
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<GeneratorEntry> parse_generator_list(std::string_view list_json) {
    std::vector<GeneratorEntry> entries;
    try {
        const json list = json::parse(list_json);
        if (!list.is_array()) {
            throw std::runtime_error("not a list");
        }
        for (const json& item : list) {
            entries.push_back({.name = item.at("name").get<std::string>(),
                    .group = item.at("group").get<std::string>(),
                    .summary = item.at("summary").get<std::string>()});
            // Configs are found and written by name
            const std::string& name = entries.back().name;
            if (name.empty() || name.front() == '.' || name.find('/') != std::string::npos) {
                throw std::runtime_error(fmt::format("`{}` isn't a file name", name));
            }
        }
    }
    catch (const std::exception& e) {
        throw std::runtime_error(
                fmt::format("The generator's list can't be read: {}", message(e)));
    }
    return entries;
}

std::vector<Entry> build_catalogue(const fs::path& folder,
        std::span<const GeneratorEntry> generator) {
    std::vector<Entry> entries;
    std::set<std::string> listed;
    std::error_code ec;
    for (const GeneratorEntry& scenario : generator) {
        Entry entry{.name = scenario.name, .group = scenario.group, .summary = scenario.summary,
                .config = folder / (scenario.name + ".json")};
        if (fs::exists(entry.config, ec)) {
            read_config(entry, folder);
        }
        entries.push_back(std::move(entry));
        listed.insert(scenario.name);
    }
    std::vector<Entry> others;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& path = it->path();
        if (path.extension() == ".json" && it->is_regular_file(ec)
                && !listed.contains(path.stem().string())) {
            Entry entry{.name = path.stem().string(), .config = path};
            read_config(entry, folder);
            others.push_back(std::move(entry));
        }
    }
    std::ranges::sort(others, {}, &Entry::name);
    entries.insert(entries.end(), std::make_move_iterator(others.begin()),
            std::make_move_iterator(others.end()));
    return entries;
}
}  // namespace Hub
