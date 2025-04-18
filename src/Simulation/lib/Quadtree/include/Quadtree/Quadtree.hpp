#pragma once

#include <vector>
#include <cstdint>
#include <forward_list>

#include "SFML/Graphics/Rect.hpp"

#include "Body/Body.hpp"

class Quad {
public:
    uint32_t top_left_idx = 0;
    uint32_t body_count = 0;
    const sf::Rect<double> boundaries;
    std::forward_list<const Body*> bodies;
    sf::Vector2<double> center_of_mass;
    double total_mass = 0;

    Quad(sf::Rect<double> &&boundaries);
    bool is_leaf() const;
};

class Quadtree {
public:
    Quadtree();
    ~Quadtree();

    void build_tree(const std::vector<Body> &bodies);
    std::vector<Quad> quads;
private:
    void fill_tree_recursive(uint32_t quad_idx);
};
