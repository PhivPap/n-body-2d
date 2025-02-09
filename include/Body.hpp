#pragma once

#include <string>

struct Pos2D {
    double x, y;
};

struct Vec2D {
    double x, y;
};

struct Body {
    std::string id;
    double mass;
    Pos2D pos;
    Vec2D vel;
};
