#include "CLArgs/CLArgs.hpp"

#include <filesystem>

#include "argparse/argparse.hpp"

#include "Logger/Logger.hpp"
#include "StopWatch/StopWatch.hpp"

using namespace argparse;
namespace fs = std::filesystem;

static Log::Verbosity parse_verbosity(const ArgumentParser &argparser) {
    const std::string verbosity_in = argparser.get("--verbosity");
    if (verbosity_in == "DEBUG") {
        return Log::Verbosity::DEBUG;
    }
    else if (verbosity_in == "INFO") {
        return Log::Verbosity::INFO;
    }
    else if (verbosity_in == "WARNING") {
        return Log::Verbosity::WARNING;
    }
    else if (verbosity_in == "ERROR") {
        return Log::Verbosity::ERROR;
    }
    else {
        Log::error("`{}` is not a valid verbosity level", verbosity_in);
        throw std::runtime_error("Failed to parse verbosity");
    }
}

static fs::path parse_config_path(const ArgumentParser &argparser) {
    return fs::canonical(fs::path(argparser.get("config")));
}

CLArgs::CLArgs(int argc, const char *argv[]) {
    StopWatch sw;
    ArgumentParser argparser("n-body-2d");
    argparser.add_argument("--verbosity")
            .default_value(std::string{"DEBUG"})
            .help("Specify verbosity: DEBUG/INFO/WARNING/ERROR")
            .metavar("LEVEL").nargs(1);
    argparser.add_argument("config").help("Path to the JSON configuration file")
            .metavar("CONFIG");
    try {
        argparser.parse_args(argc, argv);
        Log::verbosity = parse_verbosity(argparser);
        config = parse_config_path(argparser);
    } 
    catch (const std::exception &e) {
        Log::error(e.what());
        std::cout << argparser << std::endl;
        throw std::runtime_error("Command line argument parsing failed");
    }
    Log::debug("Parsed command line: [{}]", sw);
}
