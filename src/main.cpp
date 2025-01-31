#include <iostream>
#include "InputOutput.hpp"
#include "logger.hpp"

constexpr const char* INPUT_FILE_PATH = "../universe-db/universe-unit-test.csv";
constexpr const char* OUTPUT_FILE_PATH = "../universe-db/universe-unit-test-output.csv";
constexpr double G = 6.67430e-11;

double calculate_distance(const Pos2D &pos1, const Pos2D &pos2) {
    const double dx = pos1.x - pos2.x;
    const double dy = pos1.y - pos2.y;
    return std::sqrt(dx * dx + dy * dy);
}

double calculate_force(double mass1, double mass2, double distance) {
    return G * mass1 * mass2 / (distance * distance);
}

// O(n^2) solution
void update_velocities(std::vector<Body> &bodies, double timestep) {
    for (Body &body_a : bodies) {
        for (const Body &body_b : bodies) {
            if (&body_a != &body_b) {
                const double distance = calculate_distance(body_a.pos, body_b.pos);
                const double force = calculate_force(body_a.mass, body_b.mass, distance);
                const double Fx = force * (body_b.pos.x - body_a.pos.x) / distance;
                const double Fy = force * (body_b.pos.y - body_a.pos.y) / distance;

                body_a.vel.x += Fx / body_a.mass * timestep;
                body_a.vel.y += Fy / body_a.mass * timestep;
            }
        }
    }
}

void update_positions(std::vector<Body> &bodies, double timestep) {
    for (Body &body : bodies) {
        const double dx = body.vel.x * timestep;
        const double dy = body.vel.y * timestep;
        body.pos.x += dx;
        body.pos.y += dy;
    }
}

void simulate(std::vector<Body> &bodies, uint64_t iterations, double timestep) {
    for (uint64_t i = 0; i < iterations; i++) {
        update_positions(bodies, timestep);
        update_velocities(bodies, timestep);
    }
}

int main() {
    try {
        std::vector<Body> bodies = IO::parse_csv(INPUT_FILE_PATH);
        Log::debug("Parsed {} bodies from `{}`", bodies.size(), INPUT_FILE_PATH);

        simulate(bodies, 100, 12);

        IO::write_csv(OUTPUT_FILE_PATH, bodies);
        Log::debug("Wrote {} bodies to `{}`", bodies.size(), OUTPUT_FILE_PATH);
    }
    catch (const std::exception &e) {
        Log::error(e.what());
    }
}