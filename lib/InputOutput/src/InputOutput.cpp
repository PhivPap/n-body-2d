#include "InputOutput.hpp"

#include "csv.hpp"

#include "Logger.hpp"

std::vector<Body> IO::parse_csv(const std::string& path) {
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
        Log::error(e.what());
        throw std::runtime_error("Failed to parse: " + path);
    }   
    bodies.shrink_to_fit();
    return bodies;
}

void IO::write_csv(const std::string& path, const std::vector<Body> &bodies) {
    try {
        std::ofstream out_file(path.data());
        out_file << "id,mass,x,y,vel_x,vel_y\n";
        for (const Body &body : bodies) {
            out_file << body.id << "," << body.mass << "," << body.pos.x << "," << body.pos.y << "," << body.vel.x <<
                    "," << body.vel.y << "\n";
        }
        out_file.flush();
    }
    catch (const std::exception &e) {
        Log::error(e.what());
        throw std::runtime_error("Failed to write: " + path);
    }
}
