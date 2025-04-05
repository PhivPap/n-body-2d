#pragma once

#include <vector>
#include <thread>


#include "Config/Config.hpp"
#include "Body/Body.hpp"
#include "Constants/Constants.hpp"
#include "StopWatch/StopWatch.hpp"


class Simulation {
private:
    static constexpr double e = 1e39;

    const Config &cfg;
    std::vector<Body> &bodies;
    std::thread sim_thread;
    

    void simulate();
    void update_positions();
    void update_velocities();
    static double euclidean_distance(const sf::Vector2<double> &pos_a, 
            const sf::Vector2<double> &pos_b);
    static sf::Vector2<double> gravitational_force(const Body &body_a, const Body &body_b);
public:
    Simulation(const Config &cfg, std::vector<Body> &bodies);
    void start();
    void join();
};
