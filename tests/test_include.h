//
// Created by Bradley Pearce on 19/01/2026.
//

#ifndef MYLO_TEST_INCLUDE_H
#define MYLO_TEST_INCLUDE_H
#include <vector>
#include <iostream>
#include <string>

struct TestOutput {
    bool result;
    std::string result_string;
};

std::vector<std::pair<std::string, TestOutput(*)()>> tests = {};

#define ADD_TEST(NAME, FUNC) tests.emplace_back("Test " + std::to_string(tests.size()) + " [" + NAME + "]", FUNC)
#endif //MYLO_TEST_INCLUDE_H