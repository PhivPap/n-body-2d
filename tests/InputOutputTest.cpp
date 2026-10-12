#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <numbers>
#include <random>

#include "Check.hpp"
#include "InputOutput/InputOutput.hpp"
#include "Logger/Logger.hpp"
#include "TempDir.hpp"

namespace fs = std::filesystem;


namespace {
// Values are written in the shortest form that reads back exactly
void test_round_trip(const fs::path& dir) {
    std::vector<std::string> id = {"Sun", "Halley's comet", "probe"};
    std::vector<double> mass = {1.9884098713123476e30, 1.0 / 3.0, 0.0};
    std::vector<sf::Vector2<double>> pos = {{-1068260582.9736726, 0.1}, {DBL_MAX, -DBL_MIN},
            {-0.0, 123456789.0}};
    std::vector<sf::Vector2<double>> vel = {{9.318971902730485, -1e-300},
            {std::numbers::pi, 1e300}, {-2.5e-7, 0.3}};
    // and random values of any magnitude
    std::mt19937_64 rng(1);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    std::uniform_int_distribution<int> exponent(-960, 960);
    const auto random = [&] { return std::ldexp(uniform(rng), exponent(rng)); };
    for (int i = 0; i < 100; i++) {
        id.push_back("body-" + std::to_string(i));
        mass.push_back(std::abs(random()));
        const double x = random(), y = random(), vel_x = random(), vel_y = random();
        pos.emplace_back(x, y);
        vel.emplace_back(vel_x, vel_y);
    }
    const Bodies bodies(std::move(id), std::move(mass), std::move(pos), std::move(vel));

    IO::write_csv(dir / "bodies.csv", bodies);
    const Bodies parsed = IO::parse_csv(dir / "bodies.csv", false);
    CHECK(parsed.n == bodies.n);
    for (uint64_t i = 0; i < std::min(parsed.n, bodies.n); i++) {
        CHECK(parsed.id(i) == bodies.id(i));
        CHECK(parsed.mass(i) == bodies.mass(i));
        CHECK(parsed.pos(i) == bodies.pos(i));
        CHECK(parsed.vel(i) == bodies.vel(i));
    }
}

// Writes a universe of `rows` (after the header) and parses it
Bodies parse_rows(const fs::path& dir, const std::string& rows) {
    const fs::path path = dir / "universe.csv";
    std::ofstream(path) << "id,mass,x,y,vel_x,vel_y\n" << rows;
    return IO::parse_csv(path, false);
}

// True if the universe of `rows` parses but fails validation
bool rejected(const fs::path& dir, const std::string& rows) {
    try {
        parse_rows(dir, rows);
    }
    catch (const std::runtime_error& e) {
        return std::string_view(e.what()).starts_with("Failed to validate");
    }
    return false;
}

void test_validation(const fs::path& dir) {
    const std::string star = "star,2e30,0,0,0,0\n";
    const std::string planet = "planet,6e24,1.5e11,0,0,29800\n";
    fmt::println("Invalid universes, the errors below are expected:");
    CHECK(rejected(dir, star + planet + "dust,nan,1e11,0,0,0\n"));
    CHECK(rejected(dir, star + planet + "dust,0,inf,0,0,0\n"));
    CHECK(rejected(dir, star + planet + "dust,0,1e11,nan,0,0\n"));
    CHECK(rejected(dir, star + planet + "dust,0,1e11,0,-inf,0\n"));
    CHECK(rejected(dir, star + planet + "dust,0,1e11,0,0,NaN\n"));
    CHECK(rejected(dir, star + planet + "dust,-1,1e11,0,0,0\n"));
    CHECK(rejected(dir, star + planet + "star,1,1e11,0,0,0\n"));

    // Massless bodies are fine
    const Bodies bodies = parse_rows(dir, star + planet + "dust,0,1e11,0,0,0\n");
    CHECK(bodies.n == 3 && bodies.mass(2) == 0.0);
}
}  // namespace


int main() {
    Log::verbosity = Log::Verbosity::WARNING;
    const TempDir dir;
    test_round_trip(dir.path);
    test_validation(dir.path);
    return Check::result();
}
