#include <iostream>

#include "app.h"

int main() {
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