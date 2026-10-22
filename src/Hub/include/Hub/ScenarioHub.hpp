#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Hub/Catalogue.hpp"
#include "Hub/Subprocess.hpp"
#include "SFML/System/Vector2.hpp"


namespace Hub {
// The repository's scenarios/ folder, the hub's without a config or --scenarios
fs::path repository_scenarios();

// The scenario hub: a Dear ImGui window, over the running scenario or alone, that lists the
// scenarios of a folder and those the generator makes, shows what they are and runs them. It asks
// the generator for its scenarios in the background, the first time it opens.
class ScenarioHub {
public:
    explicit ScenarioHub(fs::path folder);
    bool is_open() const;
    // Reads the folder again, and selects `selection`, if any: the running scenario's config
    void open(const std::optional<fs::path>& selection);
    void close();
    // That a config failed to load, with why, or loaded (nullopt)
    void set_load_error(const fs::path& config, std::optional<std::string> error);
    // Reads what the generator wrote: every frame, open or not
    void poll();
    // Draws it if it is open, between Ui::Context::new_frame() and render(), over a window of
    // `window_size`: returns the config to run once one is picked. Keys and the mouse are its
    // while it is open, and it can only be closed while a scenario runs.
    std::optional<fs::path> draw(sf::Vector2u window_size,
            const std::optional<fs::path>& running);

private:
    enum class ListState { NOT_ASKED, ASKING, LISTED, FAILED };

    const fs::path folder;
    const fs::path script;  // the generator
    bool open_ = false;
    std::vector<GeneratorEntry> generator;
    ListState list_state = ListState::NOT_ASKED;
    std::optional<Subprocess> list_process;
    std::vector<std::string> list_output;
    std::vector<std::string> list_notes;  // why the generator didn't list its scenarios
    std::vector<Entry> entries;
    std::map<fs::path, std::string> load_errors;
    fs::path selected;      // a config
    bool chosen = false;    // whether the selection is the user's or the running scenario's
    char search[128] = {};
    bool focus_search = false;
    bool scroll_to_selected = false;

    void ask_generator();
    void list_received(int exit_code);
    // The entries that match the search, grouped as listed
    std::vector<size_t> shown_entries() const;
    std::optional<fs::path> draw_list(const std::vector<size_t>& shown,
            const std::optional<fs::path>& running);
    std::optional<fs::path> draw_details(const Entry& entry,
            const std::optional<fs::path>& running);
    std::vector<std::string> notes() const;
};
}  // namespace Hub
