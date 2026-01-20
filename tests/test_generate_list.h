//
// Created by Bradley Pearce on 19/01/2026.
//

#ifndef MYLO_TEST_GENERATE_LIST_H
#define MYLO_TEST_GENERATE_LIST_H


#include <cstring>
#include "test_include.h"

// Toggle this define to print the source code for each test
//#define PRINT_TEST_CODE

extern "C" {
    #include "../src/vm.h"
    // declarations from compiler.c
    void parse(char* src);
}


// Tests the test harness
inline TestOutput test_test() {
    return {true};
}

inline TestOutput test_simple_math() {
    //printf("Running test_simple_math...\n");
    mylo_reset(); // Ensure clean state

    std::string src = "var x = 10 + 5\n";
    parse(const_cast<char *>(src.c_str()));
    run_vm(false);

    // Inspect VM state directly
    // Global 'x' should be at index 0 and equal 15
    TestOutput output;
    output.result = vm.globals[0] == 15.0;
    output.result_string = output.result ? "" : "Expected 15.0 and got " + std::to_string(vm.globals[0]);
    return output;
}

inline TestOutput test_strings_init() {
    mylo_reset(); // Clear previous globals/bytecode

    std::string src = "var s = \"hello\"\n";
    parse(const_cast<char *>(src.c_str()));
    run_vm(false);

    TestOutput output;
    // Check string pool
    output.result = (strcmp(vm.string_pool[(int)vm.globals[0]], "hello") == 0);
    output.result_string = output.result ? "" : "Expected 'hello' and got '" + std::string(vm.string_pool[(int)vm.globals[0]]) + "'";
    return output;

}

inline TestOutput test_hello_world() {
    MyloConfig.print_to_memory = true;
    mylo_reset();

    // Note: escape the quote properly for C string "print(\"hello\")"
    std::string src = "print(\"hello\")";
    parse(const_cast<char *>(src.c_str()));
    run_vm(false);

    // Reset config so other tests don't break
    MyloConfig.print_to_memory = false;

    // Check if the output buffer actually contains the output
    // Note: OP_PRN adds a newline "\n"
    TestOutput output;
    // Check string pool
    output.result = (strcmp(vm.string_pool[(int)vm.globals[0]], "hello") == 0);
    output.result_string = output.result ? "" : "Expected 'hello' and got '" + std::string(vm.string_pool[(int)vm.globals[0]]) + "'";
    return output;
}

inline TestOutput run_source_test(const std::string& src, const std::string& expected) {
    MyloConfig.print_to_memory = true;
    mylo_reset();
#ifdef PRINT_TEST_CODE
    std::cout << std::endl;
    std::cout << src << std::endl;
#endif
    parse(const_cast<char *>(src.c_str()));
    run_vm(false);

    // Reset config so other tests don't break
    MyloConfig.print_to_memory = false;

    // Check if the output buffer actually contains the output
    // Note: OP_PRN adds a newline "\n"
    TestOutput output;
    output.result = (strcmp(vm.output_char_buffer, expected.c_str()) == 0);
    output.result_string = output.result ? "" : "Expected '" + expected + "'" + "and got '" + std::string(vm.output_char_buffer) + "'";
    return output;
}

inline TestOutput test_change_types() {

    // Note: escape the quote properly for C string "print(\"hello\")"
    std::string src = """"
    "var x = \"hi\""
    "print(x)"
    "x = 1"
    "print(x)"

    "x = f\"hi {x}\""
    "print(f\"hi {x}\")";

    std::string expected = """"
    "hi\n"
    "1\n"
    "hi hi 1\n";

    return run_source_test(src, expected);
}

inline TestOutput test_ifs() {

    // Note: escape the quote properly for C string "print(\"hello\")"
    std::string src = """"
    "var x = 5\n"
    "if x <= 5 {\n"
        "print(\"it is less than or equals 5\")\n"
    "} else {\n"
        "print(\"its greater than 5\")"
    "}";

    std::string expected = """"
    "it is less than or equals 5\n";

    return run_source_test(src, expected);
}

inline TestOutput test_fib() {

    // Note: escape the quote properly for C string "print(\"hello\")"
    std::string src = """"
    "fn fib(n) {\n"
        "if (n < 2) {\n"
            "ret n\n"
        "}\n"
        // Recursively call fib
        "ret fib(n - 1) + fib(n - 2)\n"
    "}\n"
    "print(\"Calculating Fib(10)...\")\n"
    "var result = fib(10)\n"
    "print(result)";

    std::string expected = """"
    "Calculating Fib(10)...\n"
    "55\n";

    return run_source_test(src, expected);
}

inline TestOutput test_ternary() {

    // Note: escape the quote properly for C string "print(\"hello\")"
    std::string src = """"
    "var x = 3\n"
    "print(f\"hello { x > 3 ? \"little one\" else \"not little one}\")";


    std::string expected = """"
    "hello not little one\n";

    return run_source_test(src, expected);
}

inline TestOutput test_literal_loop() {
    std::string src = """"
    "for (var i in 0...5) {\n"
    "print(i)"
    "}";

    std::string expected = """"
    "0\n1\n2\n3\n4\n";

    return run_source_test(src, expected);
}

inline TestOutput test_literal_reverse() {
    std::string src = """"
    "for (var i in 5...0) {\n"
    "print(i)"
    "}";

    std::string expected = """"
    "5\n4\n3\n2\n1\n";

    return run_source_test(src, expected);
}

inline TestOutput test_variable_loop() {
    std::string src = """"
    "var x = 3\n"
    "for (var i in x...5) {\n"
    "print(i)"
    "}";

    std::string expected = """"
    "3\n4\n";

    return run_source_test(src, expected);
}

inline TestOutput test_struct() {

    std::string src = """"
    "struct my_struct {\n"
        "var age\n"
        "var name\n"
    "}\n"

    "var person : my_struct = { age=30, name=\"Andy\" }\n"
    "var age = person.age\n"

    "print(person)\n"
    "print(age)\n"
    "print(person.name)\n";

    std::string expected = """"
    "[Ref: 0]\n"
    "30\n"
    "Andy\n";

    return run_source_test(src, expected);
}

inline TestOutput test_string_interp() {

    std::string src = """"
    "var name = \"Ben\"\n"
    "var age = 37.5\n"
    "var str = f\"I am {name}, and I am {age + 2.5}\"\n"
    "print(str)";

    std::string expected = """"
    "I am Ben, and I am 40\n";

    return run_source_test(src, expected);
}

inline TestOutput test_scope() {

    std::string src = """"
    "var x = 5\n"

    "fn foo() {\n"
        "var x = 10\n"
        "ret x\n"
    "}\n"

    "var y = foo()\n"
    "print(x)"
    "print(y)";

    std::string expected = """"
    "5\n10\n";

    return run_source_test(src, expected);
}

inline TestOutput test_namespace() {

    std::string src = """"
    "var foo = 5\n"
    "mod MySpace {\n"
        "fn foo() {\n"
            "ret 10\n"
        "}\n"
    "}\n"
    "print(foo)\n"
    "print(MySpace::foo())\n";

    std::string expected = """"
    "5\n10\n";

    return run_source_test(src, expected);
}

inline TestOutput test_while_for() {

    std::string src = """"
    "var i = 0\n"
    "for (i < 2) {\n"
        "print(i)\n"
        "i = i + 1\n"
    "}\n";

    std::string expected = """"
    "0\n1\n";

    return run_source_test(src, expected);
}

inline TestOutput test_var_access() {

    std::string src = """"
    "struct my_struct {\n"
        "var name\n"
    "}\n"
    "var person : my_struct = {name=\"Andy\"}\n"

    "fn hi(n : my_struct) {\n"
        "print(f\"Hi {n.name}\")\n"
        "ret\n"
    "}\n"
    "fn change_name(x : my_struct) {\n"
        "x.name = \"Darren\""
        "ret x\n"
    "}\n"
    "hi(change_name(person))\n";

    std::string expected = """"
    "Hi Darren\n";

    return run_source_test(src, expected);
}

inline TestOutput test_arrays() {

    std::string src = """"
    "var my_list = [\"m\", \"o\", \"o\", \"n\"]\n"

    // prints 'm'
    "print(my_list[0])\n"

    "for (x in my_list) {\n"
        "print(x)\n"
    "}\n"

    "var next_list = [1, 2, 33]\n"
    "for (x in next_list) {\n"
        "print(x)\n"
    "}\n";

    std::string expected = """"
    "m\nm\no\no\nn\n1\n2\n33\n";

    return run_source_test(src, expected);
}

inline TestOutput test_list_array() {

    std::string src = """"
    "struct Color {\n"
        "var rgba\n"
    "}\n"
    "var my_list: Color = [{rgba= 255}, {rgba= 444}]\n"

    "for (x: Color in my_list) {\n"
        "print(x.rgba)\n"
    "}\n";

    std::string expected = """"
    "255\n444\n";

    return run_source_test(src, expected);
}

inline TestOutput test_list_concat() {

    std::string src = """"
    "struct Color {\n"
        "var rgba\n"
    "}\n"
    "var my_list: Color = []\n"
    "my_list = my_list + [{rgba=500}, {rgba=800}, {rgba=22}]\n"
    "print(my_list[1].rgba)\n";

    std::string expected = """"
    "800\n";

    return run_source_test(src, expected);
}

inline TestOutput test_list_slices() {

    std::string src = """"
    "struct Color {\n"
    "var rgba\n"
    "}\n"
    "var my_list: Color = []\n"
    "my_list = my_list + [{rgba=500}, {rgba=400}, {rgba=300}, {rgba=700}, {rgba=900}]\n"
    "my_list = my_list[1:3]\n"
    // prints 'm'
    "for (x: Color in my_list) {\n"
        "print(x.rgba)\n"
    "}\n";

    std::string expected = """"
    "400\n300\n700\n";

    return run_source_test(src, expected);
}

inline TestOutput test_func_iterator_passing() {

    std::string src = """"
    "struct foo {\n"
        "var name\n"
    "}\n"

    "fn list_names(n : foo[])\n"
    "{\n"
        "for (x : foo in n) {\n"
            "print(x.name)\n"
        "}\n"
        "ret\n"
    "}\n"
    "var myvar0: foo = [{name=\"Harry\"}, {name=\"Barry\"}, {name=\"John\"}]\n"

    // This prints 'Harry', 'Barry', 'John'
    "list_names(myvar0)\n";
    std::string expected = """"
    "Harry\nBarry\nJohn\n";

    return run_source_test(src, expected);
}

inline TestOutput test_for_break() {

    std::string src = """"
    "for (var x in 0...5) {\n"
        "print(x)\n"
        "if (x > 2) {\n"
            "break\n"
        "}\n"
    "}\n";

    std::string expected = """"
    "0\n1\n2\n3\n";

    return run_source_test(src, expected);
}

inline TestOutput test_for_continue() {

    std::string src = """"
    "for (var x in 0...5) {\n"
        "if (x < 2) {\n"
            "continue\n"
        "}\n"
        "print(x)\n"
    "}\n";

    std::string expected = """"
    "2\n3\n4\n";

    return run_source_test(src, expected);
}

inline TestOutput test_file_str_io() {

    std::string src = """"
    "write_file(\"test.txt\", \"Line1\n\", \"w\")\n"
    // "a" = append mode (replaces append_file)
    "write_file(\"test.txt\", \"Line2\n\", \"a\")\n"
    "var lines = read_lines(\"test.txt\")\n"
    "for (line in lines) {\n"
        "print(line)\n"
    "}\n";

    std::string expected = """"
    "Line1\nLine2\n";

    return run_source_test(src, expected);
}

inline TestOutput test_len() {

    std::string src = """"
    "var x = [1, 2, 3, 4]\n"
    // "a" = append mode (replaces append_file)
    "print(len(x))\n";
    std::string expected = """"
    "4\n";

    return run_source_test(src, expected);
}

inline TestOutput test_file_bin_io() {

    std::string src = """"
    "var bytes = [65, 66, 67, 10]\n"
    "write_bytes(\"data.bin\", bytes)\n"

    "var read_data = read_bytes(\"data.bin\", 1)\n"
    "for (b in read_data) {\n"
        "print(b)\n"
    "}\n";

    std::string expected = """"
    "65\n66\n67\n10\n";

    return run_source_test(src, expected);
}

inline TestOutput test_contains() {

    std::string src = """"
    "var list = [\"apple\", \"banana\", \"cherry\"]\n"
    "if (contains(list, \"banana\")) {\n"
        "print(\"A\")\n"
    "}\n"

    "if (contains(\"Teamwork\", \"work\")) {\n"
        "print(\"B\")\n"
    "}\n"
    "if (contains(\"Teamwork\", \"fnarf\")) {\n"
    "}\n"
    "else {"
        "print(\"C\")\n"
    "}";

    std::string expected = """"
    "A\nB\nC\n";

    return run_source_test(src, expected);
}


inline void test_generate_list() {
    ADD_TEST("Test Test", test_test);
    ADD_TEST("Test Math", test_simple_math);
    ADD_TEST("Test Strings Init", test_strings_init);
    ADD_TEST("Test Print", test_hello_world);
    ADD_TEST("Test Change Types", test_change_types);
    ADD_TEST("Test Fib(10)", test_fib);
    ADD_TEST("Test If Statements", test_ifs);
    ADD_TEST("Test Ternary Operation", test_ternary);
    ADD_TEST("Test Literal Loop", test_literal_loop);
    ADD_TEST("Test Variable Loop", test_variable_loop);
    ADD_TEST("Test While For Loop", test_while_for);
    ADD_TEST("Test Struct", test_struct);
    ADD_TEST("Test String Interpolation", test_string_interp);
    ADD_TEST("Test Scope", test_scope);
    ADD_TEST("Test Variable Access", test_var_access);
    ADD_TEST("Test Reverse Loop", test_literal_reverse);
    ADD_TEST("Test Arrays", test_arrays);
    ADD_TEST("Test Namespace", test_namespace);
    ADD_TEST("Test List Array", test_list_array);
    ADD_TEST("Test List Concatenation", test_list_concat);
    ADD_TEST("Test List Slicing", test_list_slices);
    ADD_TEST("Test Func. Iter. Passing", test_func_iterator_passing);
    ADD_TEST("Test For Loop Break", test_for_break);
    ADD_TEST("Test For Loop Continue", test_for_continue);
    ADD_TEST("Test For File IO (str)", test_file_str_io);
    ADD_TEST("Test For File IO (bin)", test_file_bin_io);
    ADD_TEST("Test for len(array)", test_len);
    ADD_TEST("Test for contains(array)", test_contains);

}

#endif //MYLO_TEST_GENERATE_LIST_H