#pragma once

#include <vector>
#include <thread>


#include "Config/Config.hpp"
#include "Body/Body.hpp"
#include "Constants/Constants.hpp"
#include "StopWatch/StopWatch.hpp"
#include "Quadtree/Quadtree.hpp"


class Simulation {
protected:
    enum class State { STOPPED, RUNNING, PAUSED, TERMINATED } state;
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
    static sf::Vector2<double> gravitational_force(const Body &body_a, const Body &body_b);
    
public:
    NaiveSim(const Config &cfg, std::vector<Body> &bodies);
    static double euclidean_distance(const sf::Vector2<double> &pos_a, 
        const sf::Vector2<double> &pos_b);
    virtual ~NaiveSim();
    virtual void start();
    virtual void pause();
    virtual void stop();
};

class BarnesHutSim : public Simulation {
private:
    std::thread sim_thread;
    // Quadtree quadtree;

    void simulate();
    void update_positions();
    void update_velocities(const Quadtree &quadtree);
    void update_velocity(Body &body, const Quadtree *node);
    sf::Vector2<double> body_to_quad_force(const Body &body, const Quadtree *node);

public:
    BarnesHutSim(const Config &cfg, std::vector<Body> &bodies);
    virtual ~BarnesHutSim();
    virtual void start();
    virtual void pause();
    virtual void stop();
};
