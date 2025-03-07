#include "InputOutput/InputOutput.hpp"

#include "csv.hpp"

#include "Logger/Logger.hpp"
#include "StopWatch/StopWatch.hpp"

static void print_bodies(const std::vector<Body> &bodies) {
    // const auto c = fmt::color::medium_purple;
    fmt::print("Bodies:\n");
    fmt::print("{:^14}-{:^14}-{:^14}-{:^14}-{:^14}-{:^14}\n", "ID", "Mass [kg]", "pos.x [m]", 
            "pos.y [m]", "vel.x [m/s]", "vel.y [m/s]");
    for (const auto b: bodies) {
        fmt::print("{:^14} {:^14} {:^14g} {:^14g} {:^14g} {:^14g}\n", b.id, b.mass, b.pos.x, 
                b.pos.y, b.vel.x, b.vel.y);
    }
}

static bool validate_bodies(const std::vector<Body> &bodies) {
    std::unordered_set<std::string_view> unique_body_ids;
    unique_body_ids.reserve(bodies.size());
    uint64_t index = 0;
    for (const auto &body : bodies) {
        if (unique_body_ids.count(body.id) != 0) {
            Log::error("Body #{} duplicate ID `{}`", index, body.id);
        }
        else {
            unique_body_ids.emplace(body.id);
        }
        index++;
    }
    return unique_body_ids.size() == bodies.size();
}

std::vector<Body> IO::parse_csv(const fs::path& path, bool echo_bodies) {
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

    if (!validate_bodies(bodies)) {
        throw std::runtime_error("Failed to validate: `" + path.string() + "`");
    }

    if (echo_bodies) {
        print_bodies(bodies);
    }

    Log::debug("Parsed {} bodies from `{}`: [{}]", bodies.size(), path.c_str(), sw);
    return bodies;
}

void IO::write_csv(const fs::path& path, const std::vector<Body> &bodies) {
    StopWatch sw;
    try {
        std::ofstream out_file(path.c_str());
        if (out_file.fail()) {
            throw std::runtime_error("Failed to open file");
        }
        out_file << "id,mass,x,y,vel_x,vel_y\n";
        for (const Body &body : bodies) {
            out_file << fmt::format("{},{},{},{},{},{}\n", body.id, body.mass, body.pos.x, 
                    body.pos.y, body.vel.x, body.vel.y);
        }
    }
    catch (const std::exception &e) {
        Log::error(e.what());
        throw std::runtime_error("Failed to write: `" + path.string() + "`");
    }
    Log::debug("Wrote {} bodies to `{}`: [{}]", bodies.size(), path.c_str(), sw);
}
