#include <iostream>

#include "viscera/Engine.hpp"

int main()
{
    std::cout << "Engine version: " << viscera::Engine::version() << '\n';
    return 0;
}
