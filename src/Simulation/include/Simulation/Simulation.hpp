#pragma once

#include <vector>
#include <thread>


#include "Config/Config.hpp"
#include "Body/Body.hpp"
#include "Constants/Constants.hpp"
#include "StopWatch/StopWatch.hpp"


class Simulation {
protected:
    const Config &cfg;
    std::vector<Body> &bodies;
    volatile bool done = false;
public:
    Simulation(const Config &cfg, std::vector<Body> &bodies);
    virtual ~Simulation();
    bool is_done();
    virtual void start() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
};

class NaiveSim : public Simulation {
private:
    std::thread sim_thread;

    void simulate();
    void update_positions();
    void update_velocities();
    static double euclidean_distance(const sf::Vector2<double> &pos_a, 
            const sf::Vector2<double> &pos_b);
    static sf::Vector2<double> gravitational_force(const Body &body_a, const Body &body_b);
public:
    NaiveSim(const Config &cfg, std::vector<Body> &bodies);
    virtual ~NaiveSim();
    virtual void start();
    virtual void pause();
    virtual void stop();
};

// class BarnesHutSim : public Simulation {

// };
