#include "Engine.h"

int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[])
{
    Logger::setVerbosity(Logger::Verbosity::TRIVIAL);

    Engine().run();

    return 0;
}