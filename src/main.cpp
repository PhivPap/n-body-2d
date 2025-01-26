#include <iostream>
#include "csv.hpp"

constexpr const char* INPUT_FILE_PATH = "../universe-db/universe-unit-test.csv";
constexpr const char* OUTPUT_FILE_PATH = "../universe-db/universe-unit-test-output.csv";

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

void write_csv(std::string_view path, const std::vector<Body> &bodies) {
    try {
        std::ofstream out_file(path);
        out_file << "id,mass,x,y,vel_x,vel_y\n";
        for (const Body &body : bodies) {
            out_file << body.id << "," << body.mass << "," << body.pos.x << "," << body.pos.y << "," << body.vel.x <<
                    "," << body.vel.y << "\n";
        }
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        throw std::runtime_error("Failed to write: " + std::string(path));
    }
}

int main() {
    try {
        const std::vector<Body> bodies = parse_csv(INPUT_FILE_PATH);
        write_csv(OUTPUT_FILE_PATH, bodies);

    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}