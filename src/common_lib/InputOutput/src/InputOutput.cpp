#include "InputOutput/InputOutput.hpp"

#include "csv.hpp"

#include "Logger/Logger.hpp"
#include "StopWatch/StopWatch.hpp"

static void print_bodies(const Bodies &bodies) {
    fmt::print("Bodies:\n");
    fmt::print("{:^14}-{:^14}-{:^14}-{:^14}-{:^14}-{:^14}\n", "ID", "Mass [kg]", "pos.x [m]", 
            "pos.y [m]", "vel.x [m/s]", "vel.y [m/s]");
    for (uint64_t i = 0; i < bodies.n; i++) {
        fmt::print("{:^14} {:^14} {:^14g} {:^14g} {:^14g} {:^14g}\n", bodies.id(i), bodies.mass(i), 
                bodies.pos(i).x, bodies.pos(i).y, bodies.vel(i).x, bodies.vel(i).y);
    }
}

Bodies IO::parse_csv(const fs::path& path, bool echo_bodies) {
    const StopWatch sw;
    std::vector<std::string> id;
    std::vector<double> mass;
    std::vector<sf::Vector2<double>> pos;
    std::vector<sf::Vector2<double>> vel;
    try {
        csv::CSVReader reader(path.c_str());
        for (csv::CSVRow& row: reader) {
            id.emplace_back(row[0].get<std::string>());
            mass.emplace_back(std::stod(row[1].get<>()));
            pos.emplace_back(sf::Vector2<double>{std::stod(row[2].get<>()), 
                    std::stod(row[3].get<>())});
            vel.emplace_back(sf::Vector2<double>{std::stod(row[4].get<>()), 
                    std::stod(row[5].get<>())});
        }
    }
    catch (const std::exception &e) {
        Log::error("{}", e.what());
        throw std::runtime_error("Failed to parse: `" + path.string() + "`");
    }

    Bodies bodies(std::move(id), std::move(mass), std::move(pos), std::move(vel));

    if (!bodies.validate()) {
        throw std::runtime_error("Failed to validate: `" + path.string() + "`");
    }

    if (echo_bodies) {
        print_bodies(bodies);
    }

    Log::debug("Parsed {} bodies from `{}`: [{}]", bodies.n, path.c_str(), sw);
    return bodies;
}

void IO::write_csv(const fs::path& path, const Bodies &bodies) {
    const StopWatch sw;
    try {
        std::ofstream out_file(path.c_str());
        if (out_file.fail()) {
            throw std::runtime_error("Failed to open file");
        }
        out_file << "id,mass,x,y,vel_x,vel_y\n";
        for (uint64_t i = 0; i < bodies.n; i++) {
            out_file << fmt::format("{},{},{},{},{},{}\n", bodies.id(i), bodies.mass(i), 
                    bodies.pos(i).x, bodies.pos(i).y, bodies.vel(i).x, bodies.vel(i).y);
        }
    }
    catch (const std::exception &e) {
        Log::error("{}", e.what());
        throw std::runtime_error("Failed to write: `" + path.string() + "`");
    }
    Log::debug("Wrote {} bodies to `{}`: [{}]", bodies.n, path.c_str(), sw);
}
