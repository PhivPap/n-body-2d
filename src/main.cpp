#include <iostream>
#include "csv.hpp"

constexpr const char* INPUT_FILE_PATH = "../universe-db/universe-unit-test.csv";

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

std::vector<Body> parse_csv(std::string_view path) {
    std::vector<Body> bodies;
    try {
        csv::CSVReader reader(path);
        for (csv::CSVRow& row: reader) {
            Body body;
            body.id = row[0].get<>();
            body.mass = std::stod(row[1].get<>());
            body.pos.x = std::stod(row[2].get<>());
            body.pos.y = std::stod(row[3].get<>());
            body.vel.x = std::stod(row[4].get<>());
            body.vel.y = std::stod(row[5].get<>());
            bodies.push_back(body);
        }
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        throw std::runtime_error("Failed to parse: " + std::string(path));
    }   
    return bodies;
}

int main() {
    try {
        const std::vector<Body> bodies = parse_csv(INPUT_FILE_PATH);
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}