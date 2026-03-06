#include "../src/core/cli_parse.h"
#include "../src/core/output_pattern.h"
#include <iostream>
#include <string>
#include <cassert>
#include <vector>

void test_parse_positive_int() {
    int out = 0;
    assert(sprat::core::parse_positive_int("10", out));
    assert(out == 10);
    assert(!sprat::core::parse_positive_int("0", out));
    assert(!sprat::core::parse_positive_int("-1", out));
    assert(!sprat::core::parse_positive_int("abc", out));
    assert(!sprat::core::parse_positive_int("10a", out));
    std::cout << "test_parse_positive_int passed" << std::endl;
}

void test_parse_non_negative_int() {
    int out = 0;
    assert(sprat::core::parse_non_negative_int("10", out));
    assert(out == 10);
    assert(sprat::core::parse_non_negative_int("0", out));
    assert(out == 0);
    assert(!sprat::core::parse_non_negative_int("-1", out));
    std::cout << "test_parse_non_negative_int passed" << std::endl;
}

void test_parse_int() {
    int out = 0;
    assert(sprat::core::parse_int("10", out));
    assert(out == 10);
    assert(sprat::core::parse_int("-10", out));
    assert(out == -10);
    assert(sprat::core::parse_int("0", out));
    assert(out == 0);
    assert(!sprat::core::parse_int("10.5", out));
    assert(!sprat::core::parse_int("abc", out));
    std::cout << "test_parse_int passed" << std::endl;
}

void test_parse_double() {
    double out = 0.0;
    assert(sprat::core::parse_double("10.5", out));
    assert(out == 10.5);
    assert(sprat::core::parse_double("-10.5", out));
    assert(out == -10.5);
    assert(sprat::core::parse_double("10", out));
    assert(out == 10.0);
    assert(!sprat::core::parse_double("abc", out));
    std::cout << "test_parse_double passed" << std::endl;
}

void test_parse_pair() {
    int a = 0, b = 0;
    assert(sprat::core::parse_pair("10,20", a, b));
    assert(a == 10 && b == 20);
    assert(sprat::core::parse_pair("-1,-2", a, b));
    assert(a == -1 && b == -2);
    assert(!sprat::core::parse_pair("1020", a, b));
    assert(!sprat::core::parse_pair("10,", a, b));
    assert(!sprat::core::parse_pair(",20", a, b));
    assert(!sprat::core::parse_pair("10,20,30", a, b));
    std::cout << "test_parse_pair passed" << std::endl;
}

void test_parse_quoted() {
    std::string out;
    std::string error;
    size_t pos = 0;
    
    std::string input = "\"hello world\"";
    assert(sprat::core::parse_quoted(input, pos, out, error));
    assert(out == "hello world");
    assert(pos == input.size());

    input = "\"hello \\\"world\\\"\"";
    pos = 0;
    assert(sprat::core::parse_quoted(input, pos, out, error));
    assert(out == "hello \"world\"");

    input = "\"backslash \\\\ \"";
    pos = 0;
    assert(sprat::core::parse_quoted(input, pos, out, error));
    assert(out == "backslash \\ ");

    input = "no quotes";
    pos = 0;
    assert(!sprat::core::parse_quoted(input, pos, out, error));

    input = "\"unterminated";
    pos = 0;
    assert(!sprat::core::parse_quoted(input, pos, out, error));
    std::cout << "test_parse_quoted passed" << std::endl;
}

void test_to_quoted() {
    assert(sprat::core::to_quoted("hello") == "\"hello\"");
    assert(sprat::core::to_quoted("hello \"world\"") == "\"hello \\\"world\\\"\"");
    assert(sprat::core::to_quoted("a\\b") == "\"a\\\\b\"");
    std::cout << "test_to_quoted passed" << std::endl;
}

void test_format_index_pattern() {
    std::string out;
    std::string error;
    size_t placeholders = 0;

    assert(sprat::core::format_index_pattern("atlas_%d.png", 12, out, error, &placeholders));
    assert(out == "atlas_12.png");
    assert(placeholders == 1);

    assert(sprat::core::format_index_pattern("atlas_%%_%d.png", 3, out, error, &placeholders));
    assert(out == "atlas_%_3.png");
    assert(placeholders == 1);

    assert(!sprat::core::format_index_pattern("atlas_%s.png", 0, out, error, &placeholders));
    assert(error.find("unsupported placeholder") != std::string::npos);

    assert(!sprat::core::format_index_pattern("atlas_%", 0, out, error, &placeholders));
    assert(error.find("trailing '%'") != std::string::npos);
    std::cout << "test_format_index_pattern passed" << std::endl;
}

void test_validate_output_pattern() {
    std::string error;
    size_t placeholders = 0;

    assert(sprat::core::validate_output_pattern("atlas_%d.png", 2, true, error, &placeholders));
    assert(placeholders == 1);

    assert(!sprat::core::validate_output_pattern("atlas.png", 2, true, error, &placeholders));
    assert(error.find("must include %d") != std::string::npos);

    assert(sprat::core::validate_output_pattern("atlas.png", 1, true, error, &placeholders));
    assert(placeholders == 0);

    assert(sprat::core::validate_output_pattern("atlas.png", 2, false, error, &placeholders));
    assert(placeholders == 0);
    std::cout << "test_validate_output_pattern passed" << std::endl;
}

int main() {
    test_parse_positive_int();
    test_parse_non_negative_int();
    test_parse_int();
    test_parse_double();
    test_parse_pair();
    test_parse_quoted();
    test_to_quoted();
    test_format_index_pattern();
    test_validate_output_pattern();
    std::cout << "All core tests passed!" << std::endl;
    return 0;
}
