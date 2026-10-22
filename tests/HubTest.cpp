#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "Check.hpp"
#include "Hub/Catalogue.hpp"
#include "Hub/Subprocess.hpp"
#include "TempDir.hpp"

using Hub::Entry;
namespace fs = std::filesystem;


namespace {
void write(const fs::path& path, const std::string& text) {
    std::ofstream(path) << text;
}

// A config of the bodies in `csv`, with `scenario` as its Scenario block if not empty
std::string config(const std::string& csv, const std::string& scenario = "") {
    return R"({"IO": {"universe_infile": ")" + csv + R"(", "universe_outfile": "output.csv"},
            "Simulation": {"algorithm": "Barnes-Hut GPU", "timestep": 60, "theta": 0.3,
            "softening_length_m": 1000})"
           + (scenario.empty() ? "" : R"(, "Scenario": )" + scenario) + "}";
}

const std::string SCENARIO = R"({"name": "alpha", "description": "Two stars.",
        "bodies": 2, "timestep_max_s": 91.88, "timestep_limited_by": "Phobos around Mars",
        "expected_iterations_per_s": 1020000.0, "simulated_seconds_per_second": 1.24e7,
        "tips": ["Press F.", "Zoom in."]})";

void test_catalogue() {
    const TempDir dir;
    write(dir.path / "alpha.json", config("alpha.csv", SCENARIO));
    write(dir.path / "alpha.csv", "id,mass,x,y,vel_x,vel_y\n");
    write(dir.path / "beta.json", config("beta.csv"));  // its CSV is missing
    write(dir.path / "zeta.json", config("zeta.csv"));
    write(dir.path / "zeta.csv", "id,mass,x,y,vel_x,vel_y\nA,1,0,0,0,0\n");
    write(dir.path / "bad.json", "{\"IO\": ");
    write(dir.path / "abs.json", config((dir.path / "alpha.csv").string()));
    write(dir.path / "notes.txt", "not a config");
    const std::vector<Hub::GeneratorEntry> generator = {{"alpha", "systems", "Two stars"},
            {"beta", "systems", "B"}, {"gamma", "disks", "C"}};
    const std::vector<Entry> entries = Hub::build_catalogue(dir.path, generator);
    CHECK(entries.size() == 6);
    if (entries.size() != 6) {
        return;
    }
    // The generator's in its order, then the others by name
    const std::vector<std::string> names = {"alpha", "beta", "gamma", "abs", "bad", "zeta"};
    for (size_t i = 0; i < names.size(); i++) {
        CHECK(entries[i].name == names[i]);
    }
    const Entry& alpha = entries[0];
    CHECK(alpha.status == Entry::Status::READY);
    CHECK(alpha.group == "systems" && alpha.summary == "Two stars");
    CHECK(alpha.config == dir.path / "alpha.json");
    CHECK(alpha.csv == dir.path / "alpha.csv" && alpha.csv_bytes == 24);
    CHECK(alpha.algorithm == "Barnes-Hut GPU" && alpha.theta == 0.3 && alpha.timestep_s == 60.0);
    CHECK(alpha.softening_length_m == 1000.0 && !alpha.softening_factor);
    CHECK(alpha.info && alpha.info->description == "Two stars." && alpha.info->bodies == 2u);
    CHECK(entries[1].status == Entry::Status::BROKEN && !entries[1].error.empty());
    CHECK(entries[2].status == Entry::Status::NOT_GENERATED && entries[2].group == "disks");
    CHECK(entries[3].status == Entry::Status::READY && entries[3].group.empty());  // absolute
    CHECK(entries[4].status == Entry::Status::BROKEN && !entries[4].error.empty());
    CHECK(entries[5].status == Entry::Status::READY && !entries[5].info);
    CHECK(entries[5].csv_bytes == 36);

    // Without the generator, or the folder
    CHECK(Hub::build_catalogue(dir.path, {}).size() == 5);
    CHECK(Hub::build_catalogue(dir.path / "missing", generator).size() == 3);
}

void test_generator_list() {
    const auto entries = Hub::parse_generator_list(
            R"([{"name": "solar-system", "group": "systems", "summary": "The Sun – and moons"},
                {"name": "galaxy-spiral", "group": "galactic", "summary": "A disk"}])");
    CHECK(entries.size() == 2);
    if (entries.size() == 2) {
        CHECK(entries[0].name == "solar-system" && entries[0].group == "systems");
        CHECK(entries[0].summary == "The Sun – and moons");
        CHECK(entries[1].name == "galaxy-spiral" && entries[1].group == "galactic");
    }
    CHECK(Hub::parse_generator_list("[]").empty());
    CHECK_THROWS(Hub::parse_generator_list(""));
    CHECK_THROWS(Hub::parse_generator_list("Traceback (most recent call last):"));
    CHECK_THROWS(Hub::parse_generator_list(R"({"name": "a", "group": "b", "summary": "c"})"));
    CHECK_THROWS(Hub::parse_generator_list(R"([{"name": "a", "group": "b"}])"));
    CHECK_THROWS(Hub::parse_generator_list(R"([{"name": 1, "group": "b", "summary": "c"}])"));
    CHECK_THROWS(Hub::parse_generator_list(R"([{"name": "../a", "group": "b", "summary": ""}])"));
    CHECK_THROWS(Hub::parse_generator_list(R"([{"name": "", "group": "b", "summary": ""}])"));
}

void test_scenario_info() {
    const TempDir dir;
    write(dir.path / "with.json", config("a.csv", SCENARIO));
    write(dir.path / "without.json", config("a.csv"));
    write(dir.path / "unlimited.json",
            config("a.csv", R"({"name": "u", "timestep_max_s": null, "tips": [1, "Tip"]})"));
    const auto info = Hub::read_scenario_info(dir.path / "with.json");
    CHECK(info.has_value());
    if (info) {
        CHECK(info->name == "alpha" && info->description == "Two stars.");
        CHECK(info->tips == std::vector<std::string>({"Press F.", "Zoom in."}));
        CHECK(info->bodies == 2u && info->timestep_max_s == 91.88);
        CHECK(info->timestep_limited_by == "Phobos around Mars");
        CHECK(info->expected_steps_per_s == 1020000.0 && info->simulated_s_per_s == 1.24e7);
    }
    CHECK(!Hub::read_scenario_info(dir.path / "without.json"));
    CHECK(!Hub::read_scenario_info(dir.path / "missing.json"));
    const auto unlimited = Hub::read_scenario_info(dir.path / "unlimited.json");
    CHECK(unlimited && !unlimited->timestep_max_s && !unlimited->bodies);
    CHECK(unlimited && unlimited->tips == std::vector<std::string>({"Tip"}));
}

// Polls `process` until it finished, for at most `timeout`
std::vector<std::string> run_out(Hub::Subprocess& process, std::chrono::milliseconds timeout) {
    using namespace std::chrono_literals;
    std::vector<std::string> lines;
    const auto start = std::chrono::steady_clock::now();
    while (!process.finished() && std::chrono::steady_clock::now() - start < timeout) {
        for (std::string& line : process.poll()) {
            lines.push_back(std::move(line));
        }
        std::this_thread::sleep_for(5ms);
    }
    for (std::string& line : process.poll()) {
        lines.push_back(std::move(line));
    }
    return lines;
}

void test_subprocess() {
    using namespace std::chrono_literals;
    {
        // Both outputs, in order, and the exit code
        Hub::Subprocess process({"/bin/sh", "-c", "echo a; echo b >&2; printf c; exit 3"});
        const std::vector<std::string> lines = run_out(process, 5s);
        CHECK(process.finished());
        CHECK(lines == std::vector<std::string>({"a", "b", "c"}));
        CHECK(process.exit_code() == 3);
    }
    {
        // Found in PATH
        Hub::Subprocess process({"sh", "-c", "exit 0"});
        run_out(process, 5s);
        CHECK(process.exit_code() == 0);
    }
    {
        // Terminated promptly
        Hub::Subprocess process({"sleep", "30"});
        CHECK(process.running() && !process.exit_code());
        const auto start = std::chrono::steady_clock::now();
        process.terminate();
        CHECK(std::chrono::steady_clock::now() - start < 2s);
        CHECK(!process.running() && process.exit_code() == 128 + 15);  // SIGTERM
    }
    {
        // The destructor terminates it
        const auto start = std::chrono::steady_clock::now();
        { Hub::Subprocess process({"sleep", "30"}); }
        CHECK(std::chrono::steady_clock::now() - start < 2s);
    }
    CHECK_THROWS(Hub::Subprocess({"n-body-2d-no-such-program"}));
}
}  // namespace


int main() {
    test_catalogue();
    test_generator_list();
    test_scenario_info();
    test_subprocess();
    return Check::result();
}
