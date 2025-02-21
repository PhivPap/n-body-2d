#pragma once

#include <vector>

#include "SFML/Graphics/Rect.hpp"

#include "Body.hpp"

class Quadtree {
public:
    Quadtree *top_left, *top_right, *bottom_left, *bottom_right; 
    const sf::Rect<double> boundaries;
    std::vector<const Body*> bodies;
    sf::Vector2<double> center_of_mass;
    double total_mass;
    
    Quadtree(const std::vector<Body> &bodies);
    ~Quadtree();
private:
    void fill_tree_recursive();
    void insert(const Body* body);
    void set_center_of_mass();

    Quadtree(const sf::Rect<double> &boundaries);
};

