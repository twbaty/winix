#include <iostream>
#include <string>
#include <vector>
#include <sstream>

int main() {
    std::string line;
    std::cout << "Winix Shell v0.1\n";

    while (true) {
        std::cout << "winix> ";
        if (!std::getline(std::cin, line) || line == "exit")
            break;

        std::cout << "You entered: " << line << std::endl;
    }

    std::cout << "Goodbye.\n";
    return 0;
}
