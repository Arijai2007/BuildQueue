#include "CLI.hpp"
#include "HttpServer.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#ifndef BUILDQUEUE_ROOT
#define BUILDQUEUE_ROOT "."
#endif
int main(int argc, char **argv) {
    try {
        std::map<std::string, std::string> args;
        for (int i = 1; i < argc; ++i) {
            std::string k = argv[i];
            if (k == "--help" || k == "--serve" || k == "--interactive" || k == "--json")
                args[k] = "true";
            else if (k == "--demo" || k == "--load" || k == "--port" || k == "--policy" ||
                     k == "--quantum" || k == "--workers" || k == "--slots" || k == "--mode" ||
                     k == "--mem-policy" || k == "--delay" || k == "--root") {
                if (i + 1 >= argc)
                    throw std::invalid_argument("Missing value for " + k);
                args[k] = argv[++i];
            } else
                throw std::invalid_argument("Unknown option " + k + "; use --help");
        }
        std::string root = args.count("--root") ? args["--root"] : BUILDQUEUE_ROOT;
        if (!std::filesystem::is_directory(root + "/demos"))
            throw std::runtime_error(
                "Project resources missing; specify --root /path/to/BuildQueue");
        std::filesystem::create_directories("logs");
        bq::BuildQueueEngine engine(root);
        bq::CLI cli(engine, std::cout);
        if (args.count("--help")) {
            cli.banner();
            cli.help();
            return 0;
        }
        auto numeric = [&](const std::string &k, int d) {
            if (!args.count(k))
                return d;
            std::size_t pos = 0;
            int n = std::stoi(args[k], &pos);
            if (pos != args[k].size())
                throw std::invalid_argument(k + " expects an integer");
            return n;
        };
        if (args.count("--demo"))
            engine.loadDemo(args["--demo"]);
        if (args.count("--policy") || args.count("--quantum"))
            engine.policy(args.count("--policy")
                              ? args["--policy"]
                              : engine.snapshot()["config"]["policy"].get<std::string>(),
                          numeric("--quantum", 2));
        bq::json config = bq::json::object();
        for (auto key : {"workers", "slots"})
            if (args.count("--" + std::string(key)))
                config[key] = numeric("--" + std::string(key), 1);
        if (args.count("--delay"))
            config["delay_ms"] = numeric("--delay", 350);
        if (args.count("--mode"))
            config["mode"] = args["--mode"];
        if (!config.empty())
            engine.configure(config);
        if (args.count("--mem-policy"))
            engine.memoryPolicy(args["--mem-policy"]);
        if (args.count("--load"))
            engine.loadFile(args["--load"]);
        if (args.count("--serve"))
            return bq::HttpServer::serve(engine, numeric("--port", 8080), root);
        if (args.count("--interactive") || (!args.count("--demo") && !args.count("--load")))
            cli.interactive();
        else {
            engine.runUntilDone();
            if (args.count("--json"))
                std::cout << engine.snapshot().dump(2) << "\n";
            else {
                cli.banner();
                cli.report();
                cli.gantt();
            }
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "BuildQueue error: " << e.what() << "\n";
        return 1;
    }
}
