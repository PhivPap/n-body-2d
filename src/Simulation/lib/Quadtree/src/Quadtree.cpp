#include "Quadtree/Quadtree.hpp"

#include <limits>

#include "Body/Body.hpp"
#include "Logger/Logger.hpp"


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

Quad::Quad(sf::Rect<double> &&boundaries) : boundaries(boundaries) {}

bool Quad::is_leaf() const {
    return top_left_idx == 0;
}

Quadtree::Quadtree() {}

Quadtree::~Quadtree() {}

void Quadtree::fill_tree_recursive(uint32_t quad_idx) {
    Quad *quad = &quads[quad_idx];
    if (quad->body_count == 0) {
        return;
    }
    else if (quad->body_count == 1) {
        quad->center_of_mass = quad->bodies.front()->pos;
        quad->total_mass = quad->bodies.front()->mass;
        quad->bodies.clear();
        return;
    }

    // set boundaries for child nodes
    const auto center = quad->boundaries.getCenter();
    const auto top_left_pos = quad->boundaries.position;
    const auto half_size = quad->boundaries.size / 2.0;
    const uint32_t top_left_idx = quads.size();

    quad->top_left_idx = top_left_idx;
    // Stmts below invalidate quad references by appending to the vector
    quads.emplace_back(Quad{{top_left_pos, half_size}});
    quads.emplace_back(Quad{{{center.x, top_left_pos.y}, half_size}});
    quads.emplace_back(Quad{{{top_left_pos.x, center.y}, half_size}});
    quads.emplace_back(Quad{{center, half_size}});

    quad = &quads[quad_idx];
    Quad *top_left = &quads[top_left_idx];
    Quad *top_right = top_left + 1;
    Quad *bottom_left = top_left + 2;
    Quad *bottom_right = top_left + 3;

    auto top_left_head = top_left->bodies.before_begin();
    auto top_right_head = top_right->bodies.before_begin();
    auto bottom_left_head = bottom_left->bodies.before_begin();
    auto bottom_right_head = bottom_right->bodies.before_begin();

    while (!quad->bodies.empty()) {
        auto &body = quad->bodies.front();
        if (body->pos.x < center.x) {
            if (body->pos.y < center.y) {
                top_left->bodies.splice_after(top_left_head, quad->bodies, quad->bodies.before_begin());
                top_left->body_count++;
            }
            else {
                bottom_left->bodies.splice_after(bottom_left_head, quad->bodies, quad->bodies.before_begin());
                bottom_left->body_count++;
            }
        }
        else {
            if (body->pos.y < center.y) {
                top_right->bodies.splice_after(top_right_head, quad->bodies, quad->bodies.before_begin());
                top_right->body_count++;
            }
            else {
                bottom_right->bodies.splice_after(bottom_right_head, quad->bodies, quad->bodies.before_begin());
                bottom_right->body_count++;
            }
        }
    }

    // Stmts below invalidate quad references by appending to the vector
    fill_tree_recursive(top_left_idx);
    fill_tree_recursive(top_left_idx + 1);
    fill_tree_recursive(top_left_idx + 2);
    fill_tree_recursive(top_left_idx + 3);

    quad = &quads[quad_idx];
    top_left = &quads[top_left_idx];
    top_right = top_left + 1;
    bottom_left = top_left + 2;
    bottom_right = top_left + 3;

    quad->total_mass = top_left->total_mass + top_right->total_mass +
            bottom_left->total_mass + bottom_right->total_mass;

    quad->center_of_mass =
            (top_left->center_of_mass     * top_left->total_mass + 
            bottom_right->center_of_mass * bottom_right->total_mass +
            bottom_left->center_of_mass  * bottom_left->total_mass + 
            top_right->center_of_mass    * top_right->total_mass) 
            / quad->total_mass;
}

void Quadtree::build_tree(const std::vector<Body> &bodies) {
    // After the first tree, we always expect the new one to be similar to the previous.
    // This will hopefully be enough to avoid resizes during the tree gen.
    quads.reserve(quads.size() * 1.1);
    quads.clear();

    // insert & init root
    quads.emplace_back(get_universe_boundaries(bodies));
    Quad& root = quads.back();
    for (const Body& body : bodies) {
        root.bodies.push_front(&body);
        root.total_mass += body.mass;
    }
    root.body_count = bodies.size();

    fill_tree_recursive(0);
}
