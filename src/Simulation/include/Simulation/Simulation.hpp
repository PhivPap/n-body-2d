#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "Config/Config.hpp"
#include "Body/Body.hpp"
#include "StopWatch/StopWatch.hpp"
#include "Quadtree/Quadtree.hpp"


class Simulation {
private:
    enum class State : uint8_t { PAUSED, RUNNING, COMPLETED, TERMINATED };
    State state {State::PAUSED};
    StopWatch sw {StopWatch::State::PAUSED};
    std::thread sim_thread;
    std::mutex state_mtx;
    std::condition_variable state_cv;

    void simulate();
protected:
    const Config &cfg;
    std::vector<Body> &bodies;

    virtual void iteration() = 0;
public:
    Simulation(const Config &cfg, std::vector<Body> &bodies);
    virtual ~Simulation();
    void run();
    void pause();
    void terminate();
    bool completed();
};

class NaiveSim : public Simulation {
private:
    void update_positions();
    void update_velocities();
    static sf::Vector2<double> gravitational_force(const Body &body_a, const Body &body_b);
    virtual void iteration();
    
public:
    NaiveSim(const Config &cfg, std::vector<Body> &bodies);
    virtual ~NaiveSim();
    static double euclidean_distance(const sf::Vector2<double> &pos_a, 
        const sf::Vector2<double> &pos_b);
};

class BarnesHutSim : public Simulation {
private:
    void update_positions();
    void update_velocities(const Quadtree &quadtree);
    void update_velocity(Body &body, const Quadtree *node);
    sf::Vector2<double> body_to_quad_force(const Body &body, const Quadtree *node);
    virtual void iteration();

public:
    BarnesHutSim(const Config &cfg, std::vector<Body> &bodies);
    virtual ~BarnesHutSim();
};
