#include "CLArgs/CLArgs.hpp"

#include <filesystem>

#include "argparse/argparse.hpp"
#include "Logger/Logger.hpp"
#include "StopWatch/StopWatch.hpp"


using namespace argparse;
namespace fs = std::filesystem;

static Log::Verbosity parse_verbosity(const ArgumentParser& argparser) {
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

static std::optional<fs::path> parse_config_path(const ArgumentParser& argparser) {
    const auto config = argparser.present("config");
    return config ? std::optional(fs::canonical(fs::path(*config))) : std::nullopt;
}

static std::optional<fs::path> parse_scenarios_path(const ArgumentParser& argparser) {
    const auto scenarios = argparser.present("--scenarios");
    if (!scenarios) {
        return std::nullopt;
    }
    fs::path folder = fs::canonical(fs::path(*scenarios));
    if (!fs::is_directory(folder)) {
        throw std::runtime_error(fmt::format("`{}` is not a folder", *scenarios));
    }
    return folder;
}

CLArgs::CLArgs(int argc, const char* argv[]) {
    const StopWatch sw;
    ArgumentParser argparser("n-body-2d");
    argparser.add_argument("--verbosity").default_value(std::string{"DEBUG"})
            .help("Specify verbosity: DEBUG/INFO/WARNING/ERROR").metavar("LEVEL").nargs(1);
    argparser.add_argument("--scenarios")
            .help("Folder of the scenario hub (default: the config's, else the repository's "
                  "scenarios/)")
            .metavar("DIR").nargs(1);
    argparser.add_argument("config")
            .help("Path to the JSON configuration file (none: open the scenario hub)")
            .metavar("CONFIG").nargs(argparse::nargs_pattern::optional);
    try {
        argparser.parse_args(argc, argv);
        Log::verbosity = parse_verbosity(argparser);
        config = parse_config_path(argparser);
        scenarios = parse_scenarios_path(argparser);
    }
    catch (const std::exception& e) {
        Log::error("{}", e.what());
        std::cout << argparser << std::endl;
        throw std::runtime_error("Command line argument parsing failed");
    }
    Log::debug("Parsed command line: [{}]", sw);
}
