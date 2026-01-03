#include "Quadtree/Quadtree.hpp"

#include <limits>

#include "Body/Body.hpp"
#include "Logger/Logger.hpp"


sf::Rect<double> get_universe_boundaries(const Bodies &bodies) {
    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double min_y = std::numeric_limits<double>::max();
    double max_y = std::numeric_limits<double>::lowest();

    for (uint64_t i = 0; i < bodies.n; i++) {
        if (bodies.pos(i).x < min_x) {
            min_x = bodies.pos(i).x;
        }
        else if (bodies.pos(i).x > max_x) {
            max_x = bodies.pos(i).x;
        }

        if (bodies.pos(i).y < min_y) {
            min_y = bodies.pos(i).y;
        }
        else if (bodies.pos(i).y > max_y) {
            max_y = bodies.pos(i).y;
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
        const auto body_idx = quad->body_idxs.front();
        quad->center_of_mass = bodies->pos(body_idx);
        quad->total_mass = bodies->mass(body_idx);
        quad->momentum = bodies->vel(body_idx) * bodies->mass(body_idx);
        quad->body_idxs.clear();
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

    auto top_left_head = top_left->body_idxs.before_begin();
    auto top_right_head = top_right->body_idxs.before_begin();
    auto bottom_left_head = bottom_left->body_idxs.before_begin();
    auto bottom_right_head = bottom_right->body_idxs.before_begin();

    while (!quad->body_idxs.empty()) {
        const auto body_idx = quad->body_idxs.front();
        if (bodies->pos(body_idx).x < center.x) {
            if (bodies->pos(body_idx).y < center.y) {
                top_left->body_idxs.splice_after(top_left_head, quad->body_idxs, quad->body_idxs.before_begin());
                top_left->body_count++;
            }
            else {
                bottom_left->body_idxs.splice_after(bottom_left_head, quad->body_idxs, quad->body_idxs.before_begin());
                bottom_left->body_count++;
            }
        }
        else {
            if (bodies->pos(body_idx).y < center.y) {
                top_right->body_idxs.splice_after(top_right_head, quad->body_idxs, quad->body_idxs.before_begin());
                top_right->body_count++;
            }
            else {
                bottom_right->body_idxs.splice_after(bottom_right_head, quad->body_idxs, quad->body_idxs.before_begin());
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

    quad->momentum = top_left->momentum + top_right->momentum + 
            bottom_left->momentum + bottom_right->momentum;
}

void Quadtree::build_tree(const Bodies &bodies) {
    this->bodies = &bodies;

    // After the first tree, we always expect the new one to be similar to the previous.
    // This will hopefully be enough to avoid resizes during the tree gen.
    quads.reserve(quads.size() * 1.1);
    quads.clear();

    // insert & init root
    quads.emplace_back(get_universe_boundaries(bodies));
    Quad& root = quads.back();
    for (uint64_t i = 0; i < bodies.n; i++) {
        root.body_idxs.push_front(i);
        root.total_mass += bodies.mass(i);
    }
    root.body_count = bodies.n;

    fill_tree_recursive(0);
}
