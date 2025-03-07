#include "Quadtree/Quadtree.hpp"

#include <limits>

#include "Body/Body.hpp"


sf::Rect<double> get_universe_boundaries(const std::vector<Body> &bodies) {
    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double min_y = std::numeric_limits<double>::max();
    double max_y = std::numeric_limits<double>::lowest();

    for (const Body &body : bodies) {
        if (body.pos.x < min_x) {
            min_x = body.pos.x;
        }
        else if (body.pos.x > max_x) {
            max_x = body.pos.x;
        }

        if (body.pos.y < min_y) {
            min_y = body.pos.y;
        }
        else if (body.pos.y > max_y) {
            max_y = body.pos.y;
        }
    }

    return {{min_x, min_y}, {max_x - min_x, max_y - min_y}};
}

// root constructor
Quadtree::Quadtree(const std::vector<Body> &bodies):  boundaries(get_universe_boundaries(bodies)), total_mass(0) {
    // insert bodies into root node
    for (const Body &body: bodies) {
        this->bodies.push_back(&body);
        total_mass += body.mass;
    }
    fill_tree_recursive();
}

// subtree constructor
Quadtree::Quadtree(const sf::Rect<double> &boundaries): boundaries(boundaries), total_mass(0) {}

Quadtree::~Quadtree() {}

void Quadtree::fill_tree_recursive() {
    if (bodies.size() == 0) {
        return;
    }
    else if (bodies.size() == 1) {
        center_of_mass = bodies[0]->pos;
        return;
    }

    // set boundaries for child nodes
    const auto center = boundaries.getCenter();
    const auto top_left_pos = boundaries.position;
    const auto half_size = boundaries.size / 2.0;
    top_left = new Quadtree({top_left_pos, half_size});
    top_right = new Quadtree({{center.x, top_left_pos.y}, half_size});
    bottom_left = new Quadtree({{top_left_pos.x, center.y}, half_size});
    bottom_right = new Quadtree({center, half_size});

    // insert bodies positioned within the region
    for (const Body *body : bodies) {
        const auto body_coords = body->pos;
    
        if (body_coords.x < center.x) {
            if (body_coords.y < center.y) {
                top_left->insert(body);
            }
            else {
                bottom_left->insert(body);
            }
        }
        else {
            if (body_coords.y < center.y) {
                top_right->insert(body);
            }
            else {
                bottom_right->insert(body);
            }
        }
    }

    top_left->fill_tree_recursive();
    top_right->fill_tree_recursive();
    bottom_left->fill_tree_recursive();
    bottom_right->fill_tree_recursive();

    set_center_of_mass();
}

void Quadtree::insert(const Body *body) {
    bodies.push_back(body);
    total_mass += body->mass;
}

void Quadtree::set_center_of_mass() {
    center_of_mass =
        (top_left->center_of_mass * top_left->total_mass + bottom_right->center_of_mass * bottom_right->total_mass +
         bottom_left->center_of_mass * bottom_left->total_mass + top_right->center_of_mass * top_right->total_mass) /
        total_mass;
}
