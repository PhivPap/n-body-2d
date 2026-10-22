#define IMGUI_DEFINE_MATH_OPERATORS
#include "Hub/ScenarioHub.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <ranges>
#include <stdexcept>

#include "Config/Config.hpp"
#include "Constants/Constants.hpp"
#include "Logger/Distance.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Time.hpp"
#include "Ui/Format.hpp"
#include "Ui/Ui.hpp"
#include "Ui/Widgets.hpp"
#include "fmt/format.h"
#include "fmt/std.h"


namespace Hub {
namespace {
using SimType = Config::Simulation::SimType;

// The PC that the generator's speed estimates are for
constexpr std::string_view REFERENCE_PC = "Ryzen 9 9950X3D + RTX 5070 Ti";

std::string lower(std::string_view text) {
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

// As Config matches algorithms: in any letter case
bool is_algorithm(std::string_view algorithm, SimType simtype) {
    return lower(algorithm) == lower(Config::Simulation::simtype_to_string(simtype));
}

// What the list calls the generator's groups
std::string group_title(const std::string& group) {
    if (group == "three-body") {
        return "Three-body";
    }
    if (group == "systems") {
        return "Systems";
    }
    if (group == "disks") {
        return "Disks & rings";
    }
    if (group == "galactic") {
        return "Galactic";
    }
    return group.empty() ? "Other configs" : group;
}

// A body count in at most four characters: "44", "1.5k", "250k", "1M"
std::string short_count(uint64_t n) {
    if (n < 1000) {
        return std::to_string(n);
    }
    return n < 999'500 ? fmt::format("{:.3g}k", static_cast<double>(n) / 1e3)
                       : fmt::format("{:.3g}M", static_cast<double>(n) / 1e6);
}

std::string file_size(uintmax_t bytes) {
    return Ui::Format::si(static_cast<double>(bytes)) + (bytes < 1000 ? " B" : "B");
}

// Why this build can't run the config, if it can't
std::optional<std::string> unsupported([[maybe_unused]] const Entry& entry) {
#ifndef NBODY_CUDA
    if (is_algorithm(entry.algorithm, SimType::BARNES_HUT_GPU)) {
        return "This build has no GPU algorithm (it was built without CUDA). To run the scenario "
               "on the CPU, set the config's algorithm to \"Barnes-Hut\" and its threads to the "
               "number of threads your CPU runs, as the README explains.";
    }
#endif
    return std::nullopt;
}

bool runnable(const Entry& entry) {
    return entry.status == Entry::Status::READY && !unsupported(entry);
}

void wrapped(std::string_view text, sf::Color color) {
    ImGui::PushTextWrapPos(0.f);
    Ui::text(text, Ui::fonts().text, color);
    ImGui::PopTextWrapPos();
}
}  // namespace


fs::path repository_scenarios() {
    return fs::path(NBODY_SOURCE_DIR) / "scenarios";
}

ScenarioHub::ScenarioHub(fs::path folder)
        : folder(std::move(folder)),
          script(fs::path(NBODY_SOURCE_DIR) / "scripts" / "scenario-generator.py") {}

bool ScenarioHub::is_open() const {
    return open_;
}

void ScenarioHub::open(const std::optional<fs::path>& selection) {
    open_ = true;
    focus_search = true;
    scroll_to_selected = true;
    if (list_state == ListState::NOT_ASKED) {
        ask_generator();
    }
    entries = build_catalogue(folder, generator);
    if (selection) {
        selected = *selection;
        chosen = true;
    }
}

void ScenarioHub::close() {
    open_ = false;
}

void ScenarioHub::set_load_error(const fs::path& config, std::optional<std::string> error) {
    if (error) {
        load_errors[config] = std::move(*error);
    }
    else {
        load_errors.erase(config);
    }
}

// Runs `python3 -u <generator> --list --json`, once
void ScenarioHub::ask_generator() {
    std::error_code ec;
    if (!fs::exists(script, ec)) {
        list_state = ListState::FAILED;
        list_notes = {fmt::format("The scenario generator isn't at {}", script)};
        return;
    }
    try {
        list_process.emplace(std::vector<std::string>{
                "python3", "-u", script.string(), "--list", "--json"});
        list_state = ListState::ASKING;
    }
    catch (const std::exception& e) {
        list_state = ListState::FAILED;
        list_notes = {"More scenarios need Python 3 with numpy and scipy: pip install numpy scipy",
                e.what()};
    }
}

void ScenarioHub::poll() {
    if (!list_process) {
        return;
    }
    for (std::string& line : list_process->poll()) {
        list_output.push_back(std::move(line));
    }
    if (list_process->finished()) {
        const int exit_code = list_process->exit_code().value_or(-1);
        list_process.reset();
        list_received(exit_code);
    }
}

// The generator's list is its output's last line: warnings may come before it
void ScenarioHub::list_received(int exit_code) {
    const auto reversed = list_output | std::views::reverse;
    const auto list = std::ranges::find_if(
            reversed, [](const std::string& line) { return line.starts_with('['); });
    try {
        if (exit_code != 0 || list == reversed.end()) {
            throw std::runtime_error(fmt::format("exit code {}", exit_code));
        }
        generator = parse_generator_list(*list);
        list_state = ListState::LISTED;
    }
    catch (const std::exception& e) {
        // Its last line says why, such as a missing module
        const auto last = std::ranges::find_if(
                reversed, [](const std::string& line) { return !line.empty(); });
        list_state = ListState::FAILED;
        list_notes = {"More scenarios need Python 3 with numpy and scipy: pip install numpy scipy",
                last != reversed.end() ? *last : std::string(e.what())};
        Log::warning("The scenario generator didn't list its scenarios: {}", list_notes.back());
    }
    list_output.clear();
    entries = build_catalogue(folder, generator);
    if (!chosen) {
        selected.clear();  // the first of the full list
    }
}

std::vector<size_t> ScenarioHub::shown_entries() const {
    const std::string query = lower(search);
    const auto matches = [&query](const Entry& entry) {
        return lower(entry.name).find(query) != std::string::npos
               || lower(entry.summary).find(query) != std::string::npos
               || (entry.info && lower(entry.info->description).find(query) != std::string::npos);
    };
    // Groups in the order they first come in
    std::vector<std::string> groups;
    for (const Entry& entry : entries) {
        if (std::ranges::find(groups, entry.group) == groups.end()) {
            groups.push_back(entry.group);
        }
    }
    std::vector<size_t> shown;
    for (const std::string& group : groups) {
        for (size_t i = 0; i < entries.size(); i++) {
            if (entries[i].group == group && matches(entries[i])) {
                shown.push_back(i);
            }
        }
    }
    return shown;
}

std::vector<std::string> ScenarioHub::notes() const {
    std::vector<std::string> notes;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        notes.push_back(fmt::format("There is no folder {}", folder));
    }
    if (list_state == ListState::ASKING) {
        notes.push_back("Looking for more scenarios…");
    }
    if (list_state == ListState::FAILED) {
        notes.insert(notes.end(), list_notes.begin(), list_notes.end());
    }
    return notes;
}

std::optional<fs::path> ScenarioHub::draw(sf::Vector2u window_size,
        const std::optional<fs::path>& running) {
    using namespace Constants::Ui;
    if (!open_) {
        return std::nullopt;
    }
    const sf::Vector2f window(window_size);
    const ImVec2 size(std::round(std::min(HUB_SIZE_PX.x, HUB_SIZE_SHARE.x * window.x)),
            std::round(std::min(HUB_SIZE_PX.y, HUB_SIZE_SHARE.y * window.y)));
    ImGui::SetNextWindowPos(
            {std::round((window.x - size.x) / 2.f), std::round((window.y - size.y) / 2.f)});
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Ui::u32(HUB_BG));
    // Closed by its button only while a scenario runs. Keys are handled here, not by navigation.
    constexpr ImGuiWindowFlags FLAGS = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove
                                       | ImGuiWindowFlags_NoResize
                                       | ImGuiWindowFlags_NoSavedSettings
                                       | ImGuiWindowFlags_NoNavInputs
                                       | ImGuiWindowFlags_NoScrollbar
                                       | ImGuiWindowFlags_NoScrollWithMouse;
    bool keep_open = true;
    const bool visible = ImGui::Begin("Scenarios", running ? &keep_open : nullptr, FLAGS);
    ImGui::PopStyleColor();
    std::optional<fs::path> picked;
    if (visible) {
        const std::vector<size_t> shown = shown_entries();
        // The selection is one of the entries shown, moved by Up and Down even while typing
        const auto is_selected = [this](size_t i) { return entries[i].config == selected; };
        auto current = std::ranges::find_if(shown, is_selected);
        if (current == shown.end() && !shown.empty()) {
            current = shown.begin();
            selected = entries[*current].config;
        }
        if (current != shown.end()) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && current + 1 != shown.end()) {
                selected = entries[*++current].config;
                chosen = scroll_to_selected = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && current != shown.begin()) {
                selected = entries[*--current].config;
                chosen = scroll_to_selected = true;
            }
            const bool enter =
                    ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
            if (enter && runnable(entries[*current])) {
                picked = entries[*current].config;
            }
        }

        ImGui::BeginChild("##left", {HUB_LIST_WIDTH_PX, 0.f}, ImGuiChildFlags_None,
                ImGuiWindowFlags_NoNavInputs);
        if (focus_search) {
            ImGui::SetKeyboardFocusHere();
            focus_search = false;
        }
        // Selected whenever it's focused, so that typing replaces an earlier search
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##search", "Search", search, sizeof(search),
                ImGuiInputTextFlags_AutoSelectAll);
        if (const auto list_pick = draw_list(shown, running)) {
            picked = list_pick;
        }
        ImGui::EndChild();
        ImGui::SameLine(0.f, 2.f * WINDOW_PADDING.x);
        ImGui::BeginChild("##details", {0.f, 0.f}, ImGuiChildFlags_None,
                ImGuiWindowFlags_NoNavInputs);
        if (current != shown.end()) {
            if (const auto details_pick = draw_details(entries[*current], running)) {
                picked = details_pick;
            }
        }
        else {
            wrapped(entries.empty() ? "No scenarios" : "No scenarios match the search",
                    DIM_TEXT);
        }
        ImGui::EndChild();
    }
    ImGui::End();
    if (!keep_open || picked) {
        close();
    }
    return picked;
}

// The entries shown, under their groups' titles, then the notes, at the bottom
std::optional<fs::path> ScenarioHub::draw_list(const std::vector<size_t>& shown,
        const std::optional<fs::path>& running) {
    using namespace Constants::Ui;
    const std::vector<std::string> note_lines = notes();
    const float width = ImGui::GetContentRegionAvail().x;
    float notes_height = 0.f;
    for (const std::string& note : note_lines) {
        notes_height += ImGui::CalcTextSize(note.c_str(), nullptr, false, width).y
                        + ImGui::GetStyle().ItemSpacing.y;
    }
    std::optional<fs::path> picked;
    ImGui::BeginChild("##list", {0.f, -notes_height}, ImGuiChildFlags_None,
            ImGuiWindowFlags_NoNavInputs);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (size_t k = 0; k < shown.size(); k++) {
        const Entry& entry = entries[shown[k]];
        if (k == 0 || entry.group != entries[shown[k - 1]].group) {
            if (k > 0) {
                ImGui::Spacing();
            }
            Ui::text(group_title(entry.group), Ui::fonts().text, DIM_TEXT);
        }
        // The name, and right-aligned how many bodies it has or why it can't run
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float right = pos.x + ImGui::GetContentRegionAvail().x;
        ImGui::PushID(static_cast<int>(shown[k]));
        const bool is_selected = entry.config == selected;
        if (ImGui::Selectable("##row", is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            selected = entry.config;
            chosen = true;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && runnable(entry)) {
                picked = entry.config;
            }
        }
        ImGui::PopID();
        if (is_selected && scroll_to_selected) {
            ImGui::SetScrollHereY();
            scroll_to_selected = false;
        }
        std::string status;
        sf::Color status_color = DIM_TEXT;
        if (entry.config == running) {
            status = "running";
            status_color = ACCENT;
        }
        else if (entry.status == Entry::Status::NOT_GENERATED) {
            status = "not generated";
        }
        else if (entry.status == Entry::Status::BROKEN || load_errors.contains(entry.config)) {
            status = "broken";
        }
        else if (entry.info && entry.info->bodies) {
            status = short_count(*entry.info->bodies);
        }
        ImFont* font = Ui::fonts().text;
        const float status_width =
                font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, status.c_str()).x;
        draw_list->AddText(font, font->FontSize, pos, Ui::draw_color(TEXT), entry.name.c_str());
        draw_list->AddText(font, font->FontSize, {std::round(right - status_width), pos.y},
                Ui::draw_color(status_color), status.c_str());
    }
    ImGui::EndChild();
    for (const std::string& note : note_lines) {
        wrapped(note, DIM_TEXT);
    }
    return picked;
}

// What the entry is, then what can be done with it
std::optional<fs::path> ScenarioHub::draw_details(const Entry& entry,
        const std::optional<fs::path>& running) {
    using namespace Constants::Ui;
    const ScenarioInfo* info = entry.info ? &*entry.info : nullptr;
    Ui::text(entry.name, Ui::fonts().heading, TEXT);
    if (!entry.summary.empty()) {
        wrapped(entry.summary, DIM_TEXT);
    }
    ImGui::Spacing();
    if (info && !info->description.empty()) {
        wrapped(info->description, TEXT);
        ImGui::Spacing();
    }
    if (info) {
        for (const std::string& tip : info->tips) {
            ImGui::Bullet();
            ImGui::SameLine();
            wrapped(tip, TEXT);
        }
        ImGui::Spacing();
    }

    if (entry.status != Entry::Status::NOT_GENERATED && Ui::begin_rows("##facts")) {
        if (info && info->bodies) {
            Ui::row("Bodies", Ui::Format::with_thousands(*info->bodies));
        }
        if (!entry.algorithm.empty()) {
            Ui::row("Algorithm", is_algorithm(entry.algorithm, SimType::ALL_PAIRS) || !entry.theta
                                         ? entry.algorithm
                                         : fmt::format("{}, theta {:g}", entry.algorithm,
                                                   *entry.theta));
        }
        if (entry.softening_length_m) {
            Ui::row("Softening", *entry.softening_length_m > 0.0
                                         ? Log::Distance::from(*entry.softening_length_m).to_string()
                                         : "none");
        }
        else if (entry.softening_factor) {
            Ui::row("Softening", fmt::format("{:g} of the mean distance", *entry.softening_factor));
        }
        if (entry.timestep_s) {
            std::string timestep = Log::Time::from(*entry.timestep_s).to_string();
            if (info && info->timestep_max_s) {
                timestep += fmt::format(", at most {}", Log::Time::from(*info->timestep_max_s));
                if (!info->timestep_limited_by.empty()) {
                    timestep += fmt::format(" ({})", info->timestep_limited_by);
                }
            }
            Ui::row("Timestep", timestep);
        }
        Ui::row("Config", entry.config.filename().string());
        if (entry.status == Entry::Status::READY) {
            Ui::row("Bodies file", fmt::format("{} ({})", entry.csv.filename().string(),
                                           file_size(entry.csv_bytes)));
        }
        Ui::end_rows();
    }
    if (info && info->expected_steps_per_s && info->simulated_s_per_s) {
        ImGui::Spacing();
        wrapped(fmt::format("About {} steps and {} simulated per second on the reference PC ({})",
                        Ui::Format::si(*info->expected_steps_per_s),
                        Log::Time::from(*info->simulated_s_per_s), REFERENCE_PC),
                DIM_TEXT);
    }

    // What can be done with it
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    std::optional<fs::path> picked;
    if (const auto error = load_errors.find(entry.config); error != load_errors.end()) {
        wrapped(fmt::format("{}: the terminal says why", error->second), ERROR_TEXT);
    }
    switch (entry.status) {
    case Entry::Status::READY: {
        const std::optional<std::string> why_not = unsupported(entry);
        ImGui::BeginDisabled(why_not.has_value());
        if (ImGui::Button(entry.config == running ? "Restart" : "Run", {120.f, 0.f})) {
            picked = entry.config;
        }
        ImGui::EndDisabled();
        if (why_not) {
            wrapped(*why_not, DIM_TEXT);
        }
        break;
    }
    case Entry::Status::NOT_GENERATED:
        wrapped(fmt::format("Not generated yet. In a terminal, from the repository: python3 "
                            "scripts/scenario-generator.py --out-dir {} {}",
                        folder, entry.name),
                DIM_TEXT);
        break;
    case Entry::Status::BROKEN:
        wrapped(entry.error, ERROR_TEXT);
        break;
    }
    return picked;
}
}  // namespace Hub
