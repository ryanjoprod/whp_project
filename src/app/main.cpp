#include <iostream>

#include "app.h"
#include "logger.h"

int main() {
	Logger::init("whp_program.log");

	try {
		App app;
		app.run();
	}
	catch (const std::exception& e) {
		std::cerr << "Fatal error: " << e.what() << "\n";
		return 1;
	}

	return 0;
}