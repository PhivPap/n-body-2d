#include <iostream>
#include <fmt/core.h>
#include "InputOutput.hpp"

constexpr const char* INPUT_FILE_PATH = "../universe-db/universe-unit-test.csv";
constexpr const char* OUTPUT_FILE_PATH = "../universe-db/universe-unit-test-output.csv";


int main() {
    try {
        fmt::print("Reading input...\n");
        const std::vector<Body> bodies = IO::parse_csv(INPUT_FILE_PATH);
        IO::write_csv(OUTPUT_FILE_PATH, bodies);
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}