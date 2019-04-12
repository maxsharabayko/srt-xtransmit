#include "CLI/CLI.hpp"



int main(int argc, char **argv) {

	CLI::App app("SRT xtransmit tool.");
	app.set_help_all_flag("--help-all", "Expand all help");

	CLI::App *sc_test    = app.add_subcommand("test",    "Receive/send a test content generated");
	CLI::App *sc_file    = app.add_subcommand("file",    "Receive/send a file");
	CLI::App *sc_folder  = app.add_subcommand("folder",  "Receive/send a folder");
	CLI::App *sc_live    = app.add_subcommand("live",    "Receive/send a live source");
	CLI::App *sc_forward = app.add_subcommand("forward", "Bidirectional data forwarding");
	app.require_subcommand(); // 1 or more

	//std::string file;
	//start->add_option("-f,--file", file, "File name");

	//CLI::Option *s = stop->add_flag("-c,--count", "Counter");

	CLI11_PARSE(app, argc, argv);

	//std::cout << "Working on --file from start: " << file << std::endl;
	//std::cout << "Working on --count from stop: " << s->count() << ", direct count: " << stop->count("--count")
	//	<< std::endl;
	//std::cout << "Count of --random flag: " << app.count("--random") << std::endl;
	//for (auto subcom : app.get_subcommands())
	//	std::cout << "Subcommand: " << subcom->get_name() << std::endl;

	return 0;
}



