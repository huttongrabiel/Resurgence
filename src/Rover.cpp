#include "Constants.h"
#include "Globals.h"
#include "Util.h"
#include "log.h"
#include "navtypes.h"
#include "network/MissionControlProtocol.h"
#include "rospub.h"
#include "world_interface/world_interface.h"

#include <array>
#include <chrono>
#include <csignal>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>
#include <time.h>
#include <unistd.h>

#include <argparse/argparse.hpp>
#include <frozen/string.h>
#include <frozen/unordered_set.h>
#include <sys/time.h>

using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::steady_clock;
using namespace navtypes;
using namespace robot::types;

void closeRover(int signum) {
	robot::emergencyStop();
	rospub::shutdown();
	Globals::websocketServer.stop();
	raise(SIGTERM);
}

std::vector<URCLegGPS> parseGPSLegs(std::string filepath) {
	std::vector<URCLegGPS> urc_legs;
	std::ifstream gps_legs(filepath);

	int left_post_id, right_post_id;
	double lat, lon;
	std::string line;

	// A properly formatted file has each leg on a separate line, with each line of the form
	// left_post_id right_post_id lat lon
	// Improperly formatted lines (empty lines, comments) are ignored, and text
	// after the above data on properly formatted lines is ignored
	// An example can be found at example_gps_legs.txt
	while (getline(gps_legs, line)) {
		std::istringstream line_stream(line);
		if (line_stream >> left_post_id >> right_post_id >> lat >> lon) {
			// we assume that gps already has a fix
			gpscoords_t gps = {lat, lon};
			URCLegGPS leg = {left_post_id, right_post_id, gps};
			log(LOG_INFO, "Got urc leg at lat=%f lon=%f\n", lat, lon);
			urc_legs.push_back(leg);
		}
	}
	log(LOG_INFO, "Got %d urc legs\n", urc_legs.size());

	if (urc_legs.size() == 0) {
		log(LOG_ERROR, "could not get URC legs\n");
		std::exit(EXIT_FAILURE);
	}

	return urc_legs;
}

void parseCommandLine(int argc, char** argv) {
	argparse::ArgumentParser program("Rover", "N/A");

	program.add_argument("-p", "--peripheral")
		.help("specify the peripheral mounted on the rover")
		.default_value(std::string("none"))
		.action([](const std::string& value) {
			std::unordered_map<std::string, mountedperipheral_t> allowed{
				{"none", mountedperipheral_t::none},
				{"arm", mountedperipheral_t::arm},
				{"armServo", mountedperipheral_t::armServo},
				{"science", mountedperipheral_t::scienceStation},
				{"lidar", mountedperipheral_t::lidar}};

			if (allowed.find(value) != allowed.end()) {
				Globals::mountedPeripheral = allowed[value];
				return value;
			}

			throw std::runtime_error("Invalid peripheral " + value);
		});

	program.add_argument("-llvl", "--loglevel")
		.help("specify the log level to use (must be one of: LOG_TRACE, LOG_DEBUG, LOG_INFO, "
			  "LOG_WARN, LOG_ERROR)")
		.default_value(std::string("LOG_INFO"))
		.action([](const std::string& value) {
			std::unordered_map<std::string, int> allowed{{"LOG_TRACE", LOG_TRACE},
														 {"LOG_DEBUG", LOG_DEBUG},
														 {"LOG_INFO", LOG_INFO},
														 {"LOG_WARN", LOG_WARN},
														 {"LOG_ERROR", LOG_ERROR}};

			if (allowed.find(value) != allowed.end()) {
				setLogLevel(allowed[value]);
				return value;
			}

			throw std::runtime_error("Invalid log level " + value);
		});

	program.add_argument("-nc", "--no-colors")
		.help("disables colors in console logging")
		.action([&](const auto&) { setColorsEnabled(false); })
		.nargs(0);

	try {
		program.parse_args(argc, argv);
		log(LOG_INFO,
			"parseCommandLine got peripheral specified as: \"%s\", logLevel specified as: "
			"\"%s\"\n",
			program.get<std::string>("peripheral").c_str(),
			program.get<std::string>("loglevel").c_str());
	} catch (const std::runtime_error& err) {
		std::cerr << err.what() << std::endl;
		std::cerr << program;
		std::exit(EXIT_FAILURE);
	}
}

int main(int argc, char** argv) {
	parseCommandLine(argc, argv);
	Globals::AUTONOMOUS = false;
	Globals::websocketServer.start();
	robot::world_interface_init();
	auto mcProto = std::make_unique<net::mc::MissionControlProtocol>(Globals::websocketServer);
	Globals::websocketServer.addProtocol(std::move(mcProto));
	rospub::init();
	// Ctrl+C doesn't stop the simulation without this line
	signal(SIGINT, closeRover);

	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds(60));
	}
}
