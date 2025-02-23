#include "CLArgs.hpp"

#include "argparse/argparse.hpp"

#include "Logger.hpp"

using namespace argparse;

static void parse_verbosity(const ArgumentParser &argparser) {
    const std::string verbosity_in = argparser.get("--verbosity");
    if (verbosity_in == "DEBUG") {
        Log::verbosity = Log::Verbosity::DEBUG;
    } 
    else if (verbosity_in == "INFO") {
        Log::verbosity = Log::Verbosity::INFO;
    }
    else if (verbosity_in == "WARNING") {
        Log::verbosity = Log::Verbosity::WARNING;
    }
    else if (verbosity_in == "ERROR") {
        Log::verbosity = Log::Verbosity::ERROR;
    }
    else {
        Log::error("`{}` is not a valid verbosity level", verbosity_in);
        throw std::runtime_error("Failed to parse verbosity");
    }
}

CLArgs::CLArgs(int argc, const char *argv[]) {
    ArgumentParser argparser("n-body-2d");
    argparser.add_argument("--verbosity")
            .default_value(std::string{"DEBUG"})
            .help("Specify verbosity: DEBUG/INFO/WARNING/ERROR")
            .metavar("LEVEL").nargs(1);
    argparser.add_argument("config").help("Path to the JSON configuration file")
            .metavar("CONFIG");
    try {
        argparser.parse_args(argc, argv);
        parse_verbosity(argparser);
        config_path = argparser.get("config");
    } 
    catch (const std::exception &e) {
        Log::error(e.what());
        std::cout << argparser << std::endl;
        throw std::runtime_error("Command line argument parsing failed");
    }
}
