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

int main()
{
    // tests
    test_generate_list();

    // test the tests
    int passed = 0;
    int total = 0;
    for (auto &test : tests)
    {
        try {
            if (test.second().result) {
                ++passed;
                std::cout << std::endl << "✓ " << test.first;

            }
            else
                std::cout << std::endl << "❌ [FAILED] " << test.first << " --> " << test.second().result_string << std::endl;
            ++total;
        } catch (std::exception &e) {
            std::cout << std::endl << "❌ [FAILED] " << test.first << " --> " << "EXCEPTION" << e.what() << std::endl;
        }
    }
    std::cout << std::endl;

    std::cout << "Tests Passed : " << passed << " out of " << total << std::endl;

    return 0;
}