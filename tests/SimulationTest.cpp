#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <thread>

#include "Check.hpp"
#include "Logger/Logger.hpp"
#include "Simulation/AllPairs.hpp"
#include "Simulation/BarnesHut.hpp"
#ifdef NBODY_CUDA
#include "Simulation/BarnesHutCuda.hpp"
#endif

using SimType = Config::Simulation::SimType;
using Vector = sf::Vector2<double>;


namespace {
constexpr double G = Constants::Simulation::G;
constexpr double SOLAR_MASS = 2e30;  // kg

// The random systems: a thousand stars spread over a disk, softened to a fifth of their mean
// spacing, with steps of about a thousandth of the disk's dynamical time
constexpr double DISK_RADIUS = 1e13;  // m
constexpr double SOFTENING = 1e11;    // m
constexpr double TIMESTEP = 1e5;      // s

// `iterations` steps of `timestep`, with theta = 0.5, no softening and one thread
Config::Simulation sim_config(SimType simtype, double timestep, uint64_t iterations) {
    return {.timestep = timestep, .iterations = iterations, .simtype_str = "", .simtype = simtype,
            .theta = 0.5, .softening_factor = std::nullopt, .softening_length_m = 0.0,
            .threads = 1};
}

std::unique_ptr<Simulation> create_sim(const Config::Simulation& cfg, Bodies& bodies) {
    switch (cfg.simtype) {
    case SimType::ALL_PAIRS:
        return std::make_unique<AllPairsSim>(cfg, bodies);
    case SimType::BARNES_HUT:
        return std::make_unique<BarnesHut>(cfg, bodies);
    case SimType::BARNES_HUT_GPU:
#ifdef NBODY_CUDA
        return std::make_unique<BarnesHutCuda>(cfg, bodies);
#endif
        break;
    }
    throw std::runtime_error("Algorithm not built");
}

// Runs the simulation of `bodies` until it finished
void simulate(const Config::Simulation& cfg, Bodies& bodies) {
    const std::unique_ptr<Simulation> sim = create_sim(cfg, bodies);
    sim->run();
    while (!sim->is_finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    sim->pause();
    sim->rethrow_error();
}

// `n` stars of 0.5 to 1.5 solar masses spread over a disk, moving in random directions at up to
// the circular speed at its edge
Bodies random_bodies(uint32_t n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    const double max_speed = std::sqrt(G * n * SOLAR_MASS / DISK_RADIUS);
    std::vector<std::string> id;
    std::vector<double> mass;
    std::vector<Vector> pos, vel;
    for (uint32_t i = 0; i < n; i++) {
        id.push_back(std::to_string(i));
        mass.push_back((0.5 + uniform(rng)) * SOLAR_MASS);
        const double r = DISK_RADIUS * std::sqrt(uniform(rng));
        const double phi = 2 * std::numbers::pi * uniform(rng);
        pos.emplace_back(r * std::cos(phi), r * std::sin(phi));
        const double speed = max_speed * uniform(rng);
        const double psi = 2 * std::numbers::pi * uniform(rng);
        vel.emplace_back(speed * std::cos(psi), speed * std::sin(psi));
    }
    return Bodies(std::move(id), std::move(mass), std::move(pos), std::move(vel));
}

bool all_finite(const Bodies& bodies) {
    for (uint64_t i = 0; i < bodies.n; i++) {
        if (!std::isfinite(bodies.pos(i).x) || !std::isfinite(bodies.pos(i).y)
                || !std::isfinite(bodies.vel(i).x) || !std::isfinite(bodies.vel(i).y)) {
            return false;
        }
    }
    return true;
}

// Largest difference of the positions, and of the velocities, of two runs from `initial`, relative
// to the largest change in position, and in velocity, of the reference run
double relative_difference(const Bodies& initial, const Bodies& reference, const Bodies& bodies) {
    double pos_diff = 0.0, pos_change = 0.0, vel_diff = 0.0, vel_change = 0.0;
    for (uint64_t i = 0; i < initial.n; i++) {
        pos_diff = std::max(pos_diff, (bodies.pos(i) - reference.pos(i)).length());
        pos_change = std::max(pos_change, (reference.pos(i) - initial.pos(i)).length());
        vel_diff = std::max(vel_diff, (bodies.vel(i) - reference.vel(i)).length());
        vel_change = std::max(vel_change, (reference.vel(i) - initial.vel(i)).length());
    }
    return std::max(pos_diff / pos_change, vel_diff / vel_change);
}

// A massless probe on a circular orbit around a star, at 300 steps per orbit, both moving along at
// the orbital speed. Leapfrog's orbit is circular again after whole orbits (eccentricity 5e-7
// after 20, while velocities half a step behind the positions would give 1e-2).
void test_orbit(SimType simtype) {
    constexpr double RADIUS = 1.5e11;  // m
    constexpr uint64_t STEPS_PER_ORBIT = 300, ORBITS = 20;
    const double speed = std::sqrt(G * SOLAR_MASS / RADIUS);
    const double period = 2 * std::numbers::pi * RADIUS / speed;
    Bodies bodies({"star", "probe"}, {SOLAR_MASS, 0.0}, {{0.0, 0.0}, {RADIUS, 0.0}},
            {{speed, 0.0}, {speed, speed}});
    simulate(sim_config(simtype, period / STEPS_PER_ORBIT, STEPS_PER_ORBIT * ORBITS), bodies);

    const Vector r = bodies.pos(1) - bodies.pos(0);
    const Vector v = bodies.vel(1) - bodies.vel(0);
    const double h = r.cross(v);
    const double eccentricity =
            (Vector(v.y * h, -v.x * h) / (G * SOLAR_MASS) - r / r.length()).length();
    fmt::println("Eccentricity after {} orbits: {:.3g}", ORBITS, eccentricity);
    CHECK(eccentricity < 1e-5);
    // The star moved for the whole time
    const double distance = speed * period * ORBITS;
    CHECK_NEAR(bodies.pos(0).x, distance, 1e-9 * distance);
}

// With theta = 0 the trees open every node, so they sum the same pulls as All Pairs
void test_agreement(SimType simtype) {
    const Bodies initial = random_bodies(1000, 1);
    Config::Simulation reference_cfg = sim_config(SimType::ALL_PAIRS, TIMESTEP, 20);
    reference_cfg.theta = 0.0;
    reference_cfg.softening_length_m = SOFTENING;
    Bodies reference = initial;
    simulate(reference_cfg, reference);
    Config::Simulation cfg = reference_cfg;
    cfg.simtype = simtype;
    cfg.threads = 2;
    Bodies bodies = initial;
    simulate(cfg, bodies);

    const double difference = relative_difference(initial, reference, bodies);
    // The CPU tree only differs by rounding (1e-13). The GPU tree sums single precision forces: it
    // differs by ~1e-7 (FLT_EPSILON), while theta = 0.1 already makes 3e-5.
    const double tolerance = simtype == SimType::BARNES_HUT_GPU ? 1e-6 : 1e-9;
    fmt::println("Difference to All Pairs: {:.3g} (tolerance {:g})", difference, tolerance);
    CHECK(all_finite(bodies));
    CHECK(difference < tolerance);
}

// Pausing brings the velocities in sync with half a kick and resuming starts with the other half,
// so a run that is paused many times only differs from an uninterrupted one by rounding
void test_pause(SimType simtype) {
    const Bodies initial = random_bodies(1000, 2);
    Config::Simulation cfg = sim_config(simtype, TIMESTEP, 100);
    cfg.softening_length_m = SOFTENING;
    cfg.threads = 2;
    Bodies uninterrupted = initial;
    simulate(cfg, uninterrupted);

    Bodies bodies = initial;
    uint32_t pauses = 0;
    {
        const std::unique_ptr<Simulation> sim = create_sim(cfg, bodies);
        sim->run();
        while (!sim->is_finished()) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            sim->pause();
            sim->run();
            pauses++;
        }
        sim->pause();
        sim->rethrow_error();
    }

    const double difference = relative_difference(initial, uninterrupted, bodies);
    fmt::println("Difference after {} pauses: {:.3g}", pauses, difference);
    CHECK(pauses >= 10);
    CHECK(all_finite(bodies));
    CHECK(difference < 1e-9);
}

// Massless bodies and bodies in the same place, without softening
void test_degenerate(SimType simtype) {
    const Config::Simulation cfg = sim_config(simtype, 1e4, 20);

    // A star with two planets in the same place, a massless body on the star and two more in the
    // same place
    Bodies system({"star", "planet-a", "planet-b", "dust-a", "dust-b", "dust-c"},
            {SOLAR_MASS, 6e24, 6e24, 0.0, 0.0, 0.0},
            {{0.0, 0.0}, {1.5e11, 0.0}, {1.5e11, 0.0}, {0.0, 0.0}, {-1e11, 5e10}, {-1e11, 5e10}},
            {{0.0, 0.0}, {0.0, 3e4}, {0.0, 3e4}, {0.0, 0.0}, {1e4, 3e4}, {1e4, 3e4}});
    simulate(cfg, system);
    CHECK(all_finite(system));

    // Massless bodies don't pull each other
    Bodies massless({"a", "b", "c"}, {0.0, 0.0, 0.0}, {{0.0, 0.0}, {1e11, 0.0}, {1e11, 0.0}},
            {{1e3, 0.0}, {0.0, 1e3}, {0.0, 1e3}});
    const Bodies massless_initial = massless;
    simulate(cfg, massless);
    CHECK(all_finite(massless));
    for (uint64_t i = 0; i < massless.n; i++) {
        CHECK(massless.vel(i) == massless_initial.vel(i));
    }

    // Neither do bodies in the same place, which stay there
    Bodies coincident({"a", "b", "c"}, {SOLAR_MASS, SOLAR_MASS, 0.0},
            {{1e11, -1e11}, {1e11, -1e11}, {1e11, -1e11}}, {{0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}});
    const Bodies coincident_initial = coincident;
    simulate(cfg, coincident);
    for (uint64_t i = 0; i < coincident.n; i++) {
        CHECK(coincident.pos(i) == coincident_initial.pos(i));
        CHECK(coincident.vel(i) == coincident_initial.vel(i));
    }
}
}  // namespace


// test-simulation <test> <algorithm>, e.g. test-simulation orbit "Barnes-Hut GPU"
int main(int argc, const char* argv[]) {
    Log::verbosity = Log::Verbosity::WARNING;
    const std::map<std::string_view, void (*)(SimType)> tests = {{"orbit", test_orbit},
            {"agreement", test_agreement}, {"pause", test_pause},
            {"degenerate", test_degenerate}};
    Config::Simulation algorithm{};
    algorithm.simtype_str = argc == 3 ? argv[2] : "";
    if (argc != 3 || !tests.contains(argv[1]) || !algorithm.parse_simtype()) {
        fmt::println("Usage: {} orbit|agreement|pause|degenerate <algorithm>", argv[0]);
        return 2;
    }
    try {
        tests.at(argv[1])(algorithm.simtype);
    }
    catch (const std::exception& e) {
        fmt::println("Error: {}", e.what());
        return 1;
    }
    return Check::result();
}
