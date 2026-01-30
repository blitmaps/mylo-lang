//
// Created by Bradley Pearce on 19/01/2026.
//
extern "C" {
    #include "../src/vm.h"
}
#include <iostream>
#include <string>
#include "test_include.h"
#include "test_generate_list.h"

using namespace TestTerminalColours;
int main()
{
    // tests
    test_generate_list();

    // test the tests
    int passed = 0;
    int total = 0;
    for (auto &test : tests)
    {
        //setTerminalColor(Color::Blue, BgColor::Default);
        //std::cout << "========================================================" << std::endl;
        //setTerminalColor(Color::Default, BgColor::Default);
        std::cout << "Running..." << test.first << std::endl;
        try {
            if (test.second().result) {
                ++passed;
                setTerminalColor(Color::Green, BgColor::Default);
                std::cout << "[PASS] ";
                setTerminalColor(Color::Default, BgColor::Default);
                std::cout << test.first << std::endl;
            }
            else {
                setTerminalColor(Color::Red, BgColor::Default);
                std::cout << "[FAILED] ";
                setTerminalColor(Color::Default, BgColor::Default);
                std::cout << test.first << std::endl;
                setTerminalColor(Color::Magenta, BgColor::Default);
                std::cout << " --> " << test.second().result_string << std::endl;
                setTerminalColor(Color::Default, BgColor::Default);
            }
            ++total;
        } catch (std::exception &e) {
            setTerminalColor(Color::Red, BgColor::Default);
            std::cout << "[FAILED -- EXCEPTION] ";
            std::cout << std::endl << test.first << " --> " << "EXCEPTION" << e.what() << std::endl;
            setTerminalColor(Color::Default, BgColor::Default);
            ++total;
        }
        setTerminalColor(Color::Blue, BgColor::Default);
        std::cout << "========================================================" << std::endl;
        setTerminalColor(Color::Default, BgColor::Default);

    }
    std::cout << std::endl;

    std::cout << "Tests Passed : " << passed << " out of " << total << std::endl;

    return 0;
}