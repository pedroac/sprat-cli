#include "../src/core/layout_parser.h"
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

void test_parse_layout() {
    std::string data = "atlas 512,512\n"
                       "scale 2.0\n"
                       "sprite \"a.png\" 0,0 10,10\n"
                       "sprite \"b.png\" 10,10 20,20 rotated\n";
    std::istringstream iss(data);
    sprat::core::Layout layout;
    std::string error;
    assert(sprat::core::parse_layout(iss, layout, error));
    assert(layout.atlases.size() == 1);
    assert(layout.atlases[0].width == 512 && layout.atlases[0].height == 512);
    assert(layout.has_scale && layout.scale == 2.0);
    assert(layout.sprites.size() == 2);
    assert(layout.sprites[0].path == "a.png");
    assert(layout.sprites[1].path == "b.png");
    assert(layout.sprites[1].rotated);
    std::cout << "test_parse_layout passed" << std::endl;
}

int main() {
    test_parse_atlas_line();
    test_parse_sprite_line();
    test_parse_layout();
    std::cout << "All layout tests passed!" << std::endl;
    return 0;
}
