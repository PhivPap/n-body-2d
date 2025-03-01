#include "InputOutput.hpp"

#include "csv.hpp"

#include "Logger.hpp"
#include "StopWatch.hpp"

std::vector<Body> IO::parse_csv(const fs::path& path) {
    StopWatch sw;
    std::vector<Body> bodies;
    try {
        csv::CSVReader reader(path.c_str());
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
        throw std::runtime_error("Failed to parse: `" + path.string() + "`");
    }   
    bodies.shrink_to_fit();
    Log::debug("Parsed {} bodies from `{}`: [{}]", bodies.size(), path.c_str(), sw);
    return bodies;
}

void IO::write_csv(const fs::path& path, const std::vector<Body> &bodies) {
    StopWatch sw;
    try {
        std::ofstream out_file(path.c_str());
        if (out_file.fail())
            throw std::runtime_error("Failed to open file");
        out_file << "id,mass,x,y,vel_x,vel_y\n";
        for (const Body &body : bodies) {
            out_file << body.id << "," << body.mass << "," << body.pos.x << "," << body.pos.y << "," << body.vel.x <<
                    "," << body.vel.y << "\n";
        }
        out_file.flush();
    }
    catch (const std::exception &e) {
        Log::error(e.what());
        throw std::runtime_error("Failed to write: `" + path.string() + "`");
    }
    Log::debug("Wrote {} bodies to `{}`: [{}]", bodies.size(), path.c_str(), sw);
}
