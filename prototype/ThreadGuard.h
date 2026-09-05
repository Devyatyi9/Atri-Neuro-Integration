#pragma once

#include <exception>
#include <string>

#include "Logging.h"

// Wraps a thread body so that an exception cannot leave it.
//
// An exception that escapes a thread function is std::terminate by definition,
// and the CRT turns that into abort() and a fail-fast: the process disappears
// at once, with nothing for the engine to report. Catching here costs nothing
// and turns that into a line naming the thread that died.
template <typename Body>
void RunThreadGuarded(char const *name, Body body) {
	try {
		body();
	} catch (std::exception const &error) {
		Log(("[Thread] " + std::string(name) +
		     " stopped on exception: " + error.what() + "\n")
		        .c_str());
	} catch (...) {
		Log(("[Thread] " + std::string(name) + " stopped on an unknown exception\n")
		        .c_str());
	}
}
