// These tests rely on assert(); keep it active in Release builds (CI builds Release).
#undef NDEBUG
#include "../src/core/checked_math.h"
#include "../src/core/cli_parse.h"
#include "../src/core/i18n.h"
#include "../src/core/image_limits.h"
#include "../src/core/output_pattern.h"
#include "../src/core/parallel.h"
#include "../src/core/perceptual_dedup.h"
#include <iostream>
#include <string>
#include <cassert>
#include <cstdlib>
#include <atomic>
#include <limits>
#include <stdexcept>
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

void test_compare_natural() {
    // Basic string comparison
    assert(sprat::core::compare_natural("a", "b") < 0);
    assert(sprat::core::compare_natural("b", "a") > 0);
    assert(sprat::core::compare_natural("a", "a") == 0);

    // Natural number sorting (key feature)
    assert(sprat::core::compare_natural("sprite_2.png", "sprite_10.png") < 0);
    assert(sprat::core::compare_natural("sprite_10.png", "sprite_2.png") > 0);
    assert(sprat::core::compare_natural("sprite_01.png", "sprite_02.png") < 0);

    // Mixed text and numbers
    assert(sprat::core::compare_natural("file1name", "file10name") < 0);
    assert(sprat::core::compare_natural("file10name", "file1name") > 0);

    // Empty and single character
    assert(sprat::core::compare_natural("", "a") < 0);
    assert(sprat::core::compare_natural("a", "") > 0);
    assert(sprat::core::compare_natural("", "") == 0);

    // Case sensitivity
    assert(sprat::core::compare_natural("Sprite", "sprite") != 0);

    std::cout << "test_compare_natural passed" << std::endl;
}

void test_trim_copy() {
    assert(sprat::core::trim_copy("") == "");
    assert(sprat::core::trim_copy("   ") == "");
    assert(sprat::core::trim_copy("abc") == "abc");
    assert(sprat::core::trim_copy(" \t a b \r\n") == "a b");
    std::cout << "test_trim_copy passed" << std::endl;
}

void test_checked_mul_size_t() {
    constexpr size_t max = std::numeric_limits<size_t>::max();
    size_t out = 7;
    assert(sprat::core::checked_mul_size_t(0, max, out) && out == 0);
    assert(sprat::core::checked_mul_size_t(6, 7, out) && out == 42);
    assert(sprat::core::checked_mul_size_t(max, 1, out) && out == max);
    out = 7;
    assert(!sprat::core::checked_mul_size_t(max, 2, out) && out == 7);
    assert(!sprat::core::checked_mul_size_t(max / 2 + 1, 2, out) && out == 7);
    std::cout << "test_checked_mul_size_t passed" << std::endl;
}

void test_rgba_within_tolerance() {
    using sprat::core::rgba_within_tolerance;
    const unsigned char a[] = {10, 20, 30, 255,   0, 0, 0, 0};
    const unsigned char b[] = {18, 12, 30, 255,   99, 99, 99, 0};
    assert(rgba_within_tolerance(a, b, 2, 8));   // pixel 0 within 8; pixel 1 both transparent
    assert(!rgba_within_tolerance(a, b, 2, 7));
    const unsigned char c[] = {10, 20, 30, 255,   0, 0, 0, 1};
    assert(!rgba_within_tolerance(b, c, 2, 8)); // only one side transparent: RGB counts
    std::cout << "test_rgba_within_tolerance passed" << std::endl;
}

void test_group_near_duplicates() {
    using sprat::core::DedupCandidate;
    using sprat::core::group_near_duplicates;
    // Values 0, 6, 12 with tolerance 8: neighbours match, the ends don't.
    const int values[] = {0, 6, 12};
    const auto within8 = [&](size_t x, size_t y) { return std::abs(values[x] - values[y]) <= 8; };
    std::vector<DedupCandidate> items(3, DedupCandidate{4, 4, 0, true});
    std::vector<size_t> groups = group_near_duplicates(items, 5, within8);
    assert((groups == std::vector<size_t>{0, 0, 2})); // no chaining 2 -> 1 -> 0

    // Size mismatch, hash distance and missing hashes all block a merge.
    items = {{4, 4, 0x0, true}, {4, 5, 0x0, true}, {4, 4, 0xFF, true}, {4, 4, 0x0, false}};
    const auto always = [](size_t, size_t) { return true; };
    groups = group_near_duplicates(items, 5, always);
    assert((groups == std::vector<size_t>{0, 1, 2, 3}));
    groups = group_near_duplicates(items, 8, always);
    assert((groups == std::vector<size_t>{0, 1, 0, 3}));
    std::cout << "test_group_near_duplicates passed" << std::endl;
}

void test_run_workers() {
    // Every index runs exactly once.
    std::vector<std::atomic<int>> runs(8);
    sprat::core::run_workers(8, [&](unsigned int i) { runs[i].fetch_add(1); });
    for (const auto& r : runs) {
        assert(r.load() == 1);
    }
    // A worker's exception reaches the caller after the other workers finished, instead of
    // terminating the process.
    std::atomic<int> finished{0};
    bool caught = false;
    try {
        sprat::core::run_workers(4, [&](unsigned int i) {
            if (i == 2) {
                throw std::runtime_error("worker failed");
            }
            finished.fetch_add(1);
        });
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()) == "worker failed";
    }
    assert(caught);
    assert(finished.load() == 3);
    // An exception on the calling thread's own worker (index 0) is reported the same way.
    caught = false;
    try {
        sprat::core::run_workers(3, [&](unsigned int i) {
            if (i == 0) {
                throw std::runtime_error("caller failed");
            }
        });
    } catch (const std::runtime_error&) {
        caught = true;
    }
    assert(caught);
    sprat::core::run_workers(0, [](unsigned int) { assert(false); });
    std::cout << "test_run_workers passed" << std::endl;
}

void test_image_size_within_limits() {
    assert(sprat::core::image_size_within_limits(1, 1));
    assert(sprat::core::image_size_within_limits(10000, 10000));
    assert(!sprat::core::image_size_within_limits(0, 10));
    assert(!sprat::core::image_size_within_limits(100000, 1));
    assert(!sprat::core::image_size_within_limits(20000, 20000));  // 400 megapixels
    std::cout << "test_image_size_within_limits passed" << std::endl;
}

void test_tr_format() {
    using sprat::core::tr_format;
    assert(tr_format("Animation {0} has fps {1}", {"run", "120"}) == "Animation run has fps 120");
    // A translation may reorder or repeat the values.
    assert(tr_format("{1} fps in {0} ({1})", {"run", "120"}) == "120 fps in run (120)");
    // Placeholders without a matching argument, and other braces, stay as they are.
    assert(tr_format("{2} {x} {} {", {"a"}) == "{2} {x} {} {");
    assert(tr_format("no values", {}) == "no values");
    std::cout << "test_tr_format passed" << std::endl;
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
    test_compare_natural();
    test_trim_copy();
    test_checked_mul_size_t();
    test_rgba_within_tolerance();
    test_group_near_duplicates();
    test_run_workers();
    test_image_size_within_limits();
    test_tr_format();
    std::cout << "All core tests passed!" << std::endl;
    return 0;
}
