//
// Created by Bradley Pearce on 19/01/2026.
//

#ifndef MYLO_TEST_INCLUDE_H
#define MYLO_TEST_INCLUDE_H
#include <vector>
#include <iostream>
#include <string>

namespace TestTerminalColours {
    // ANSI Color Codes
    enum class Color {
        Red = 31,
        Green = 32,
        Yellow = 33,
        Blue = 34,
        Magenta = 35,
        Cyan = 36,
        White = 37,
        Default = 39
    };

    enum class BgColor {
        Red = 41,
        Green = 42,
        Yellow = 43,
        Blue = 44,
        Magenta = 45,
        Cyan = 46,
        White = 47,
        Default = 49
    };

    // Function to set colors
    void setTerminalColor(Color fg, BgColor bg = BgColor::Default) {
        std::cout << "\033[" << static_cast<int>(fg) << ";" << static_cast<int>(bg) << "m";
    }

    // Function to reset to default
    void resetTerminal() {
        std::cout << "\033[0m";
    }
}

struct TestOutput {
    bool result;
    std::string result_string;
};

std::vector<std::pair<std::string, TestOutput(*)()>> tests = {};

#define ADD_TEST(NAME, FUNC) tests.emplace_back("Test " + std::to_string(tests.size()) + " [" + NAME + "]", FUNC)
#endif //MYLO_TEST_INCLUDE_H