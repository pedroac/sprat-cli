// These tests rely on assert(); keep it active in Release builds (CI builds Release).
#undef NDEBUG
#include "../src/core/layout_parser.h"
#include "../src/commands/spratlayout/packing.h"
#include <iostream>
#include <string>
#include <cassert>
#include <sstream>
#include <vector>

void test_parse_atlas_line() {
    int w, h;
    assert(sprat::core::parse_atlas_line("atlas 512,512", w, h));
    assert(w == 512 && h == 512);
    
    assert(sprat::core::parse_atlas_line("atlas 1024 2048", w, h));
    assert(w == 1024 && h == 2048);
    
    assert(!sprat::core::parse_atlas_line("notatlas 512,512", w, h));
    assert(!sprat::core::parse_atlas_line("atlas 512", w, h));
    std::cout << "test_parse_atlas_line passed" << std::endl;
}

void test_parse_sprite_line() {
    sprat::core::Sprite s;
    std::string error;
    
    // Modern format
    assert(sprat::core::parse_sprite_line("sprite \"path/to/sprite.png\" 10,20 100,200", s, error));
    assert(s.path == "path/to/sprite.png");
    assert(s.x == 10 && s.y == 20);
    assert(s.w == 100 && s.h == 200);
    assert(!s.has_trim);
    assert(!s.rotated);

    // Modern format with trim
    assert(sprat::core::parse_sprite_line("sprite \"path/to/sprite.png\" 10,20 100,200 5,5 2,2", s, error));
    assert(s.has_trim);
    assert(s.src_x == 5 && s.src_y == 5);
    assert(s.trim_right == 2 && s.trim_bottom == 2);

    // Legacy format
    assert(sprat::core::parse_sprite_line("sprite \"legacy.png\" 0 0 32 32", s, error));
    assert(s.path == "legacy.png");
    assert(s.x == 0 && s.y == 0 && s.w == 32 && s.h == 32);

    // Rotated
    assert(sprat::core::parse_sprite_line("sprite \"rot.png\" 0,0 32,32 rotated", s, error));
    assert(s.rotated);

    std::cout << "test_parse_sprite_line passed" << std::endl;
}

void test_parse_extrude_line() {
    int extrude;
    assert(sprat::core::parse_extrude_line("extrude 2", extrude));
    assert(extrude == 2);
    
    assert(!sprat::core::parse_extrude_line("notextrude 2", extrude));
    assert(!sprat::core::parse_extrude_line("extrude", extrude));
    assert(!sprat::core::parse_extrude_line("extrude -1", extrude));
    std::cout << "test_parse_extrude_line passed" << std::endl;
}

void test_parse_layout() {
    std::string data = "atlas 512,512\n"
                       "scale 2.0\n"
                       "extrude 3\n"
                       "sprite \"a.png\" 0,0 10,10\n"
                       "sprite \"b.png\" 10,10 20,20 rotated\n";
    std::istringstream iss(data);
    sprat::core::Layout layout;
    std::string error;
    assert(sprat::core::parse_layout(iss, layout, error));
    assert(layout.atlases.size() == 1);
    assert(layout.atlases[0].width == 512 && layout.atlases[0].height == 512);
    assert(layout.has_scale && layout.scale == 2.0);
    assert(layout.has_extrude && layout.extrude == 3);
    assert(layout.sprites.size() == 2);
    assert(layout.sprites[0].path == "a.png");
    assert(layout.sprites[1].path == "b.png");
    assert(layout.sprites[1].rotated);
    std::cout << "test_parse_layout passed" << std::endl;
}

void test_parse_layout_rejects_non_positive_atlas_dimensions() {
    sprat::core::Layout layout;
    std::string error;

    // Zero dimensions
    std::istringstream z("atlas 0,0\nsprite \"a.png\" 0,0 0,0\n");
    assert(!sprat::core::parse_layout(z, layout, error));
    assert(error.find("Atlas dimensions must be positive") != std::string::npos);

    // Negative width
    error.clear();
    std::istringstream nw("atlas -1,100\nsprite \"a.png\" 0,0 1,1\n");
    assert(!sprat::core::parse_layout(nw, layout, error));
    assert(error.find("Atlas dimensions must be positive") != std::string::npos);

    // Negative height
    error.clear();
    std::istringstream nh("atlas 100,-1\nsprite \"a.png\" 0,0 1,1\n");
    assert(!sprat::core::parse_layout(nh, layout, error));
    assert(error.find("Atlas dimensions must be positive") != std::string::npos);

    std::cout << "test_parse_layout_rejects_non_positive_atlas_dimensions passed" << std::endl;
}

void test_parse_layout_rejects_negative_sprite_dimensions() {
    sprat::core::Layout layout;
    std::string error;

    // Modern format: negative width
    std::istringstream nw("atlas 512,512\nsprite \"a.png\" 0,0 -1,10\n");
    assert(!sprat::core::parse_layout(nw, layout, error));
    assert(error.find("sprite dimensions must not be negative") != std::string::npos);

    // Modern format: negative height
    error.clear();
    std::istringstream nh("atlas 512,512\nsprite \"a.png\" 0,0 10,-1\n");
    assert(!sprat::core::parse_layout(nh, layout, error));
    assert(error.find("sprite dimensions must not be negative") != std::string::npos);

    std::cout << "test_parse_layout_rejects_negative_sprite_dimensions passed" << std::endl;
}

void test_parse_layout_rejects_sprite_out_of_bounds() {
    sprat::core::Layout layout;
    std::string error;

    // Sprite extends beyond right edge
    std::istringstream r("atlas 512,512\nsprite \"a.png\" 500,0 100,100\n");
    assert(!sprat::core::parse_layout(r, layout, error));
    assert(error.find("extends beyond atlas bounds") != std::string::npos);

    // Sprite extends beyond bottom edge
    error.clear();
    std::istringstream b("atlas 512,512\nsprite \"a.png\" 0,500 100,100\n");
    assert(!sprat::core::parse_layout(b, layout, error));
    assert(error.find("extends beyond atlas bounds") != std::string::npos);

    // Sprite that fits exactly should succeed
    error.clear();
    std::istringstream ok("atlas 512,512\nsprite \"a.png\" 412,412 100,100\n");
    assert(sprat::core::parse_layout(ok, layout, error));

    std::cout << "test_parse_layout_rejects_sprite_out_of_bounds passed" << std::endl;
}

void test_parse_sprite_line_rejects_negative_position() {
    sprat::core::Sprite s;
    std::string error;

    // Modern format: negative x
    assert(!sprat::core::parse_sprite_line("sprite \"a.png\" -1,0 10,10", s, error));
    assert(error.find("sprite position must not be negative") != std::string::npos);

    // Modern format: negative y
    error.clear();
    assert(!sprat::core::parse_sprite_line("sprite \"a.png\" 0,-1 10,10", s, error));
    assert(error.find("sprite position must not be negative") != std::string::npos);

    // Legacy format: negative x
    error.clear();
    assert(!sprat::core::parse_sprite_line("sprite \"a.png\" -1 0 10 10", s, error));
    assert(error.find("sprite position must not be negative") != std::string::npos);

    // Legacy format: negative y
    error.clear();
    assert(!sprat::core::parse_sprite_line("sprite \"a.png\" 0 -1 10 10", s, error));
    assert(error.find("sprite position must not be negative") != std::string::npos);

    std::cout << "test_parse_sprite_line_rejects_negative_position passed" << std::endl;
}

void test_parse_sprite_line_rejects_negative_trim() {
    sprat::core::Sprite s;
    std::string error;

    assert(sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 10,10 1,2 3,4", s, error));
    assert(s.has_trim && s.src_x == 1 && s.src_y == 2 && s.trim_right == 3 && s.trim_bottom == 4);

    // Modern format: any negative trim offset
    for (const char* line : {"sprite \"a.png\" 0,0 10,10 -1,0 0,0",
                             "sprite \"a.png\" 0,0 10,10 0,-1 0,0",
                             "sprite \"a.png\" 0,0 10,10 0,0 -1,0",
                             "sprite \"a.png\" 0,0 10,10 0,0 0,-2147483648"}) {
        error.clear();
        assert(!sprat::core::parse_sprite_line(line, s, error));
        assert(error.find("trim offsets must not be negative") != std::string::npos);
    }

    // Legacy format: negative crop offset
    error.clear();
    assert(!sprat::core::parse_sprite_line("sprite \"a.png\" 0 0 10 10 -1 0", s, error));
    assert(error.find("trim offsets must not be negative") != std::string::npos);

    std::cout << "test_parse_sprite_line_rejects_negative_trim passed" << std::endl;
}

void test_parse_layout_rotation_direction() {
    sprat::core::Layout layout;
    std::string error;

    std::istringstream cw_default("atlas 10,10\nsprite \"a.png\" 0,0 10,10 rotated\n");
    assert(sprat::core::parse_layout(cw_default, layout, error));
    assert(!layout.rotate_ccw && !layout.has_rotation);

    std::istringstream ccw("rotation ccw\natlas 10,10\nsprite \"a.png\" 0,0 10,10 rotated\n");
    assert(sprat::core::parse_layout(ccw, layout, error));
    assert(layout.rotate_ccw && layout.has_rotation);

    std::istringstream cw("rotation cw\natlas 10,10\n");
    assert(sprat::core::parse_layout(cw, layout, error));
    assert(!layout.rotate_ccw && layout.has_rotation);

    std::istringstream invalid("rotation left\natlas 10,10\n");
    assert(!sprat::core::parse_layout(invalid, layout, error));
    assert(error.find("Invalid rotation line") != std::string::npos);

    std::istringstream duplicate("rotation ccw\nrotation cw\natlas 10,10\n");
    assert(!sprat::core::parse_layout(duplicate, layout, error));
    assert(error.find("Duplicate rotation line") != std::string::npos);

    std::cout << "test_parse_layout_rotation_direction passed" << std::endl;
}

void test_parse_sprite_line_slice() {
    sprat::core::Sprite s;
    std::string error;

    // Basic slice
    assert(sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=10,10,10,10", s, error));
    assert(s.has_slice);
    assert(s.slice_left == 10 && s.slice_top == 10 && s.slice_right == 10 && s.slice_bottom == 10);

    // Zero insets
    assert(sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=0,0,0,0", s, error));
    assert(s.has_slice);
    assert(s.slice_left == 0 && s.slice_top == 0 && s.slice_right == 0 && s.slice_bottom == 0);

    // Asymmetric insets
    assert(sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=10,20,30,40", s, error));
    assert(s.has_slice);
    assert(s.slice_left == 10 && s.slice_top == 20 && s.slice_right == 30 && s.slice_bottom == 40);

    // Combined with rotated
    assert(sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=10,10,10,10 rotated", s, error));
    assert(s.has_slice);
    assert(s.rotated);
    assert(s.slice_left == 10 && s.slice_top == 10);

    // Invalid: non-numeric
    assert(!sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=abc", s, error));
    assert(error.find("invalid slice value") != std::string::npos);

    // Invalid: negative value
    error.clear();
    assert(!sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=-1,0,0,0", s, error));
    assert(error.find("invalid slice value") != std::string::npos);

    // Invalid: too few values
    error.clear();
    assert(!sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=10,10,10", s, error));
    assert(error.find("invalid slice value") != std::string::npos);

    std::cout << "test_parse_sprite_line_slice passed" << std::endl;
}

void test_parse_sprite_line_slice_fill_modes() {
    sprat::core::Sprite s;
    std::string error;

    // Explicit per-axis modes: repeat,stretch
    assert(sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=8,8,8,8,repeat,stretch", s, error));
    assert(s.has_slice);
    assert(s.slice_left == 8 && s.slice_top == 8 && s.slice_right == 8 && s.slice_bottom == 8);
    assert(s.slice_h == "repeat");
    assert(s.slice_v == "stretch");

    // Both mirror
    assert(sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=8,8,8,8,mirror,mirror", s, error));
    assert(s.has_slice);
    assert(s.slice_h == "mirror");
    assert(s.slice_v == "mirror");

    // 4 values only — modes default to stretch
    assert(sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=8,8,8,8", s, error));
    assert(s.has_slice);
    assert(s.slice_h == "stretch");
    assert(s.slice_v == "stretch");

    // Invalid mode name
    error.clear();
    assert(!sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=8,8,8,8,invalid,stretch", s, error));
    assert(error.find("invalid slice value") != std::string::npos);

    // Fixed mode on both axes
    assert(sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=8,8,8,8,fixed,fixed", s, error));
    assert(s.has_slice);
    assert(s.slice_h == "fixed");
    assert(s.slice_v == "fixed");

    // Fixed on one axis, stretch on the other
    assert(sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=8,8,8,8,fixed,stretch", s, error));
    assert(s.slice_h == "fixed");
    assert(s.slice_v == "stretch");

    assert(sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=8,8,8,8,repeat,fixed", s, error));
    assert(s.slice_h == "repeat");
    assert(s.slice_v == "fixed");

    // Only one mode provided (5 values) — should fail
    error.clear();
    assert(!sprat::core::parse_sprite_line("sprite \"a.png\" 0,0 64,64 slice=8,8,8,8,repeat", s, error));
    assert(error.find("invalid slice value") != std::string::npos);

    std::cout << "test_parse_sprite_line_slice_fill_modes passed" << std::endl;
}

void test_parse_alias_line() {
    sprat::core::Alias alias;
    std::string error;

    assert(sprat::core::parse_alias_line("alias \"b.png\" \"a.png\"", alias, error));
    assert(alias.path == "b.png" && alias.canonical_path == "a.png");
    assert(!alias.has_trim);

    assert(sprat::core::parse_alias_line("alias \"b.png\" \"a.png\" 1,2 3,4", alias, error));
    assert(alias.has_trim);
    assert(alias.trim_left == 1 && alias.trim_top == 2);
    assert(alias.trim_right == 3 && alias.trim_bottom == 4);

    assert(!sprat::core::parse_alias_line("alias \"b.png\" \"a.png\" 1,2", alias, error));
    assert(!sprat::core::parse_alias_line("alias \"b.png\" \"a.png\" 1,2 x,4", alias, error));
    assert(!sprat::core::parse_alias_line("alias \"b.png\" \"a.png\" -1,2 3,4", alias, error));
    assert(!sprat::core::parse_alias_line("alias \"b.png\" \"a.png\" 1,2 3,4 5,6", alias, error));
    std::cout << "test_parse_alias_line passed" << std::endl;
}

void test_expand_layout_aliases() {
    std::istringstream iss("atlas 16,16\n"
                           "atlas 16,16\n"
                           "sprite \"a.png\" 2,3 4,5 1,6 2,0 rotated\n"
                           "alias \"b.png\" \"a.png\"\n"
                           "alias \"c.png\" \"a.png\" 1,0 2,6\n");
    sprat::core::Layout layout;
    std::string error;
    assert(sprat::core::parse_layout(iss, layout, error));
    assert(sprat::core::expand_layout_aliases(layout, error));
    assert(layout.aliases.empty());
    assert(layout.sprites.size() == 3);

    // Without its own trim, an alias is a copy of the canonical sprite.
    const sprat::core::Sprite& b = layout.sprites[1];
    assert(b.path == "b.png" && b.alias_of == "a.png");
    assert(b.x == 2 && b.y == 3 && b.w == 4 && b.h == 5 && b.rotated && b.atlas_index == 1);
    assert(b.src_x == 1 && b.src_y == 6 && b.trim_right == 2 && b.trim_bottom == 0);

    // With its own trim, only the placement within the source image changes.
    const sprat::core::Sprite& c = layout.sprites[2];
    assert(c.path == "c.png" && c.alias_of == "a.png");
    assert(c.x == 2 && c.y == 3 && c.w == 4 && c.h == 5 && c.rotated);
    assert(c.src_x == 1 && c.src_y == 0 && c.trim_right == 2 && c.trim_bottom == 6);

    std::istringstream missing("atlas 4,4\nsprite \"a.png\" 0,0 4,4\nalias \"b.png\" \"x.png\"\n");
    assert(sprat::core::parse_layout(missing, layout, error));
    assert(!sprat::core::expand_layout_aliases(layout, error));
    assert(error.find("x.png") != std::string::npos);
    std::cout << "test_expand_layout_aliases passed" << std::endl;
}

void test_pot_limits() {
    using sprat::layout::pot_limit;
    using sprat::layout::pot_search_bound;
    assert(pot_limit(0) == 0);       // no limit
    assert(pot_limit(1000) == 512);  // largest power of two within the limit
    assert(pot_limit(1024) == 1024);
    assert(pot_search_bound(300, 0) == 512);      // sum rounded up when there is no limit
    assert(pot_search_bound(300, 1000) == 512);
    assert(pot_search_bound(3000, 1000) == 512);  // capped by the limit's power of two
    assert(pot_search_bound(64, 96) == 64);
    std::cout << "test_pot_limits passed" << std::endl;
}

int main() {
    test_parse_atlas_line();
    test_parse_sprite_line();
    test_parse_extrude_line();
    test_parse_layout();
    test_parse_layout_rejects_non_positive_atlas_dimensions();
    test_parse_layout_rejects_negative_sprite_dimensions();
    test_parse_layout_rejects_sprite_out_of_bounds();
    test_parse_sprite_line_rejects_negative_position();
    test_parse_sprite_line_rejects_negative_trim();
    test_parse_layout_rotation_direction();
    test_parse_sprite_line_slice();
    test_parse_sprite_line_slice_fill_modes();
    test_parse_alias_line();
    test_expand_layout_aliases();
    test_pot_limits();
    std::cout << "All layout tests passed!" << std::endl;
    return 0;
}
