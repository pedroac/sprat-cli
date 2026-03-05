#include <utility>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#ifndef _O_BINARY
#define _O_BINARY 0x8000
#endif
#ifndef _fileno
#define _fileno fileno
#endif
#ifndef _setmode
#define _setmode setmode
#endif
#endif

#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <limits>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>
#include <fstream>
#include <archive.h>
#include <archive_entry.h>
#include "core/layout_parser.h"
#include "core/cli_parse.h"

#ifdef SPRAT_HAS_ZOPFLI
#include <zopflipng/zopflipng_lib.h>
#endif

namespace {

constexpr size_t NUM_CHANNELS = 4;
constexpr size_t CHANNEL_R = 0;
constexpr size_t CHANNEL_G = 1;
constexpr size_t CHANNEL_B = 2;
constexpr size_t CHANNEL_A = 3;
constexpr int MAX_CHANNEL_VALUE = 255;

using Sprite = sprat::core::Sprite;
using Layout = sprat::core::Layout;
using sprat::core::parse_int;
using sprat::core::parse_layout;
using sprat::core::to_quoted;

bool checked_mul_size_t(size_t a, size_t b, size_t& out) {
    if (a == 0 || b <= std::numeric_limits<size_t>::max() / a) {
        out = a * b;
        return true;
    }
    return false;
}

bool checked_add_size_t(size_t a, size_t b, size_t& out) {
    if (b <= std::numeric_limits<size_t>::max() - a) {
        out = a + b;
        return true;
    }
    return false;
}

bool parse_line_color(const std::string& value, std::array<unsigned char, 4>& out) {
    constexpr int MAX_CHANNELS = 4;
    std::array<int, MAX_CHANNELS> parts = {0, 0, 0, MAX_CHANNEL_VALUE};
    int part_count = 0;
    size_t start = 0;
    while (start <= value.size()) {
        size_t comma = value.find(',', start);
        size_t end = (comma == std::string::npos) ? value.size() : comma;
        if (end == start || part_count >= MAX_CHANNELS) {
            return false;
        }

        std::string token = value.substr(start, end - start);
        int channel = 0;
        if (!parse_int(token, channel) || channel < 0 || channel > MAX_CHANNEL_VALUE) {
            return false;
        }
        parts[part_count++] = channel;

        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }

    constexpr int MIN_REQUIRED_CHANNELS = 3;
    if (part_count != MIN_REQUIRED_CHANNELS && part_count != MAX_CHANNELS) {
        return false;
    }

    out[0] = static_cast<unsigned char>(parts[0]);
    out[1] = static_cast<unsigned char>(parts[1]);
    out[2] = static_cast<unsigned char>(parts[2]);
    out[3] = static_cast<unsigned char>(parts[3]);
    return true;
}

void draw_sprite_outline(
    std::vector<unsigned char>& atlas,
    int atlas_width,
    int atlas_height,
    const Sprite& s,
    int line_width,
    const std::array<unsigned char, 4>& color
) {
    if (line_width <= 0) {
        return;
    }

    auto set_pixel = [&](int px, int py) {
        if (px < 0 || py < 0 || px >= atlas_width || py >= atlas_height) {
            return;
        }
        size_t pixel_index = (static_cast<size_t>(py) * static_cast<size_t>(atlas_width)) + static_cast<size_t>(px);
        size_t offset = pixel_index * NUM_CHANNELS;
        atlas[offset + CHANNEL_R] = color[0];
        atlas[offset + CHANNEL_G] = color[1];
        atlas[offset + CHANNEL_B] = color[2];
        atlas[offset + CHANNEL_A] = color[3];
    };

    int max_t = std::min({line_width, s.w, s.h});
    for (int t = 0; t < max_t; ++t) {
        int left = s.x + t;
        int right = s.x + s.w - 1 - t;
        int top = s.y + t;
        int bottom = s.y + s.h - 1 - t;

        for (int x = left; x <= right; ++x) {
            set_pixel(x, top);
            set_pixel(x, bottom);
        }
        for (int y = top; y <= bottom; ++y) {
            set_pixel(left, y);
            set_pixel(right, y);
        }
    }
}

bool rectangles_overlap(const Sprite& a, const Sprite& b) {
    const int a_right = a.x + a.w;
    const int a_bottom = a.y + a.h;
    const int b_right = b.x + b.w;
    const int b_bottom = b.y + b.h;
    return a_right > b.x && b_right > a.x && a_bottom > b.y && b_bottom > a.y;
}

bool sprites_have_overlap(const std::vector<Sprite>& sprites) {
    if (sprites.size() < 2) {
        return false;
    }
    std::vector<size_t> order(sprites.size());
    for (size_t i = 0; i < sprites.size(); ++i) {
        order[i] = i;
    }
    std::ranges::sort(order, [&](size_t lhs, size_t rhs) {
        if (sprites[lhs].x != sprites[rhs].x) {
            return sprites[lhs].x < sprites[rhs].x;
        }
        return sprites[lhs].y < sprites[rhs].y;
    });

    for (size_t i = 0; i < order.size(); ++i) {
        const Sprite& a = sprites[order[i]];
        const int a_right = a.x + a.w;
        for (size_t j = i + 1; j < order.size(); ++j) {
            const Sprite& b = sprites[order[j]];
            if (b.x >= a_right) {
                break;
            }
            if (rectangles_overlap(a, b)) {
                return true;
            }
        }
    }
    return false;
}


void extrude_atlas(
    std::vector<unsigned char>& atlas,
    int atlas_width,
    int atlas_height,
    const std::vector<Sprite>& sprites,
    int extrude
) {
    if (extrude <= 0) {
        return;
    }

    auto set_pixel = [&](int dx, int dy, int sx, int sy) {
        if (dx < 0 || dy < 0 || dx >= atlas_width || dy >= atlas_height ||
            sx < 0 || sy < 0 || sx >= atlas_width || sy >= atlas_height) {
            return;
        }
        size_t dest_offset = (static_cast<size_t>(dy) * atlas_width + dx) * NUM_CHANNELS;
        size_t src_offset = (static_cast<size_t>(sy) * atlas_width + sx) * NUM_CHANNELS;
        std::memcpy(&atlas[dest_offset], &atlas[src_offset], NUM_CHANNELS);
    };

    for (const auto& s : sprites) {
        if (s.w <= 0 || s.h <= 0) {
            continue;
        }
        // Extrude sides
        for (int e = 1; e <= extrude; ++e) {
            // Left & Right sides
            for (int y = 0; y < s.h; ++y) {
                set_pixel(s.x - e, s.y + y, s.x, s.y + y);
                set_pixel(s.x + s.w - 1 + e, s.y + y, s.x + s.w - 1, s.y + y);
            }
            // Top & Bottom sides
            for (int x = 0; x < s.w; ++x) {
                set_pixel(s.x + x, s.y - e, s.x + x, s.y);
                set_pixel(s.x + x, s.y + s.h - 1 + e, s.x + x, s.y + s.h - 1);
            }
        }

        // Extrude corners
        for (int ey = 1; ey <= extrude; ++ey) {
            for (int ex = 1; ex <= extrude; ++ex) {
                // Top-Left
                set_pixel(s.x - ex, s.y - ey, s.x, s.y);
                // Top-Right
                set_pixel(s.x + s.w - 1 + ex, s.y - ey, s.x + s.w - 1, s.y);
                // Bottom-Left
                set_pixel(s.x - ex, s.y + s.h - 1 + ey, s.x, s.y + s.h - 1);
                // Bottom-Right
                set_pixel(s.x + s.w - 1 + ex, s.y + s.h - 1 + ey, s.x + s.w - 1, s.y + s.h - 1);
            }
        }
    }
}


} // namespace

void print_usage() {
    std::cout << "Usage: spratpack [OPTIONS]\n"
              << "\n"
              << "Read layout text from stdin and write one or more PNG atlases.\n"
              << "If multiple atlases are generated and writing to stdout, output is a TAR.\n"
              << "Multipack layouts also trigger TAR output by default when writing to stdout.\n"
              << "\n"
              << "Options:\n"
              << "  -o, --output PATTERN   Output filename pattern (e.g. atlas_%d.png)\n"
              << "  --atlas-index N        Pick a specific atlas index to output\n"
              << "  --extrude N            Repeat edge pixels N times (overrides layout)\n"
              << "  --frame-lines          Draw rectangle outlines for each sprite\n"
              << "  --line-width N         Outline thickness in pixels (default: 1)\n"
              << "  --line-color R,G,B[,A] Outline color channels (0-255, default: 255,0,0,255)\n"
              << "  --threads N            Number of worker threads\n"
              << "  --debug                Enable detailed error reporting and debug visualization\n"
              << "  --protect              Protect output with basic obfuscation\n"
              << "  --zopfli               Optimize output PNG using Zopfli (very slow)\n"
              << "  --help, -h             Show this help message\n";
}

int run_spratpack(int argc, char** argv) {
    bool debug = false;
    bool protect = false;
    bool use_zopfli = false;
    bool draw_frame_lines = false;
    int line_width = 1;
    constexpr unsigned char DEFAULT_COLOR_RED = 255;
    constexpr unsigned char DEFAULT_COLOR_ALPHA = 255;
    std::array<unsigned char, 4> line_color = {DEFAULT_COLOR_RED, 0, 0, DEFAULT_COLOR_ALPHA};
    unsigned int thread_limit = 0;
    std::string output_pattern;
    int requested_atlas_index = -1;
    int extrude = 0;
    bool has_extrude_override = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << "spratpack version " << SPRAT_VERSION << "\n";
            return 0;
        } else if (arg == "--debug") {
            debug = true;
        } else if (arg == "--protect") {
            protect = true;
        } else if (arg == "--zopfli") {
            use_zopfli = true;
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            output_pattern = argv[++i];
        } else if (arg == "--atlas-index" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_int(value, requested_atlas_index) || requested_atlas_index < 0) {
                std::cerr << "Invalid atlas index: " << value << "\n";
                return 1;
            }
        } else if (arg == "--extrude" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_int(value, extrude) || extrude < 0) {
                std::cerr << "Invalid extrude value: " << value << "\n";
                return 1;
            }
            has_extrude_override = true;
        } else if (arg == "--frame-lines") {
            draw_frame_lines = true;
        } else if (arg == "--line-width" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_int(value, line_width) || line_width <= 0) {
                std::cerr << "Invalid line width: " << value << "\n";
                return 1;
            }
        } else if (arg == "--line-color" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_line_color(value, line_color)) {
                std::cerr << "Invalid line color: " << value << "\n";
                return 1;
            }
        } else if (arg == "--threads" && i + 1 < argc) {
            std::string value = argv[++i];
            int parsed = 0;
            if (!parse_int(value, parsed) || parsed <= 0) {
                std::cerr << "Invalid thread count: " << value << "\n";
                return 1;
            }
            thread_limit = static_cast<unsigned int>(parsed);
        }
    }

    if (debug) {
        draw_frame_lines = true;
    }

    Layout layout;
    std::string parse_error;
    if (!parse_layout(std::cin, layout, parse_error)) {
        std::cerr << parse_error << "\n";
        return 1;
    }

    if (requested_atlas_index >= 0 && static_cast<size_t>(requested_atlas_index) >= layout.atlases.size()) {
        std::cerr << "Error: requested atlas index " << requested_atlas_index << " out of range (total: " << layout.atlases.size() << ")\n";
        return 1;
    }

    const bool output_to_stdout = output_pattern.empty();
    const bool use_tar = output_to_stdout && (layout.atlases.size() > 1 || layout.multipack) && requested_atlas_index < 0;

    struct ArchiveDeleter {
        void operator()(struct archive* a) const {
            if (a) {
                archive_write_close(a);
                archive_write_free(a);
            }
        }
    };
    std::unique_ptr<struct archive, ArchiveDeleter> a;

    if (use_tar) {
        a.reset(archive_write_new());
        archive_write_set_format_pax_restricted(a.get());
#ifdef _WIN32
        if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
            std::cerr << "Failed to set stdout to binary mode\n";
            return 1;
        }
#endif
        if (archive_write_open_FILE(a.get(), stdout) != ARCHIVE_OK) {
            std::cerr << "Failed to open TAR stream on stdout: " << archive_error_string(a.get()) << "\n";
            return 1;
        }
    }

    for (size_t atlas_idx = 0; atlas_idx < layout.atlases.size(); ++atlas_idx) {
        if (requested_atlas_index >= 0 && static_cast<size_t>(requested_atlas_index) != atlas_idx) {
            continue;
        }

        const int atlas_width = layout.atlases[atlas_idx].width;
        const int atlas_height = layout.atlases[atlas_idx].height;
        std::vector<Sprite> atlas_sprites;
        for (const auto& s : layout.sprites) {
            if (s.atlas_index == static_cast<int>(atlas_idx)) {
                atlas_sprites.push_back(s);
            }
        }

        size_t pixel_count = 0;
        size_t byte_count = 0;
        if (!checked_mul_size_t(static_cast<size_t>(atlas_width), static_cast<size_t>(atlas_height), pixel_count)
            || !checked_mul_size_t(pixel_count, NUM_CHANNELS, byte_count)) {
            std::cerr << "Error: Atlas " << atlas_idx << " dimensions are too large for memory allocation\n";
            return 1;
        }

        std::vector<unsigned char> atlas_data(byte_count, 0);

        auto blit_sprite = [&](const Sprite& s, std::string& error_out) -> bool {
            int w = 0, h = 0, channels = 0;
            unsigned char* data = stbi_load(s.path.c_str(), &w, &h, &channels, static_cast<int>(NUM_CHANNELS));
            if (!data) {
                // stbi_failure_reason() is global, but since we are stopping on first error, it's acceptable here.
                error_out = "Failed to load image: " + to_quoted(s.path) + " (Reason: " + stbi_failure_reason() + ")";
                return false;
            }
            // Use RAII for image data
            struct StbImageDeleter { void operator()(unsigned char* p) const { stbi_image_free(p); } };
            std::unique_ptr<unsigned char, StbImageDeleter> image_ptr(data);

            if (s.colors > 0) {
                auto quantize = [](unsigned char v, int levels, int x, int y, bool dither) -> unsigned char {
                    if (levels <= 1) return 0;
                    if (levels >= 256) return v;
                    
                    float val = v / 255.0f;
                    if (dither) {
                        static const float bayer[4][4] = {
                            { 0.0f/16, 8.0f/16, 2.0f/16, 10.0f/16 },
                            { 12.0f/16, 4.0f/16, 14.0f/16, 6.0f/16 },
                            { 3.0f/16, 11.0f/16, 1.0f/16, 9.0f/16 },
                            { 15.0f/16, 7.0f/16, 13.0f/16, 5.0f/16 }
                        };
                        float threshold = bayer[y % 4][x % 4];
                        val += (threshold - 0.5f) / (levels - 1);
                        if (val < 0.0f) val = 0.0f;
                        if (val > 1.0f) val = 1.0f;
                    }
                    
                    int level = static_cast<int>(val * (levels - 1) + 0.5f);
                    return static_cast<unsigned char>((level * 255) / (levels - 1));
                };

                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        for (int c = 0; c < 3; ++c) { // R, G, B
                            size_t off = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * NUM_CHANNELS + static_cast<size_t>(c);
                            image_ptr.get()[off] = quantize(image_ptr.get()[off], s.colors, x, y, s.dither);
                        }
                    }
                }
            }

            const int source_x = s.has_trim ? s.src_x : 0;
            const int source_y = s.has_trim ? s.src_y : 0;
            const int source_w = s.has_trim ? (w - s.src_x - s.trim_right) : w;
            const int source_h = s.has_trim ? (h - s.src_y - s.trim_bottom) : h;

            if (source_x < 0 || source_y < 0 || source_w <= 0 || source_h <= 0 ||
                source_x > w - source_w || source_y > h - source_h) {
                error_out = "Error: Crop/Trim out of bounds for " + to_quoted(s.path);
                return false;
            }

            const bool copy_rows_direct = !s.rotated && (source_w == s.w && source_h == s.h);
            if (copy_rows_direct) {
                const size_t row_bytes = static_cast<size_t>(s.w) * NUM_CHANNELS;
                for (int row = 0; row < s.h; ++row) {
                    const size_t dest_pixels = static_cast<size_t>(s.y + row) * atlas_width + s.x;
                    const size_t dest_offset = dest_pixels * NUM_CHANNELS;
                    const size_t src_pixels = static_cast<size_t>(source_y + row) * w + source_x;
                    const size_t src_offset = src_pixels * NUM_CHANNELS;
                    std::memcpy(atlas_data.data() + dest_offset, image_ptr.get() + src_offset, row_bytes);
                }
            } else {
                for (int row = 0; row < s.h; ++row) {
                    for (int col = 0; col < s.w; ++col) {
                        int sample_x, sample_y;
                        if (!s.rotated) {
                            sample_x = source_x + ((col * source_w) / s.w);
                            sample_y = source_y + ((row * source_h) / s.h);
                        } else {
                            sample_x = source_x + ((row * source_w) / s.h);
                            sample_y = source_y + (source_h - 1 - ((col * source_h) / s.w));
                        }
                        const size_t dest_pixels = static_cast<size_t>(s.y + row) * atlas_width + (s.x + col);
                        const size_t dest_offset = dest_pixels * NUM_CHANNELS;
                        const size_t src_pixels = static_cast<size_t>(sample_y) * w + sample_x;
                        const size_t src_offset = src_pixels * NUM_CHANNELS;
                        std::memcpy(atlas_data.data() + dest_offset, image_ptr.get() + src_offset, NUM_CHANNELS);
                    }
                }
            }
            return true;
        };

        unsigned int atlas_worker_count = thread_limit > 0 ? thread_limit : std::thread::hardware_concurrency();
        if (atlas_worker_count == 0) atlas_worker_count = 1;
        atlas_worker_count = std::min<unsigned int>(atlas_worker_count, static_cast<unsigned int>(std::max<size_t>(1, atlas_sprites.size())));

        const bool can_parallel = (atlas_worker_count > 1) && !sprites_have_overlap(atlas_sprites);
        if (!can_parallel) {
            for (const auto& s : atlas_sprites) {
                std::string error;
                if (!blit_sprite(s, error)) { std::cerr << error << "\n"; return 1; }
            }
        } else {
            std::atomic<size_t> next_idx{0};
            std::atomic<bool> failed{false};
            std::mutex err_mtx;
            std::string first_err;
            std::vector<std::thread> workers;
            for (unsigned int i = 0; i < atlas_worker_count; ++i) {
                workers.emplace_back([&]() {
                    while (!failed.load(std::memory_order_relaxed)) {
                        size_t idx = next_idx.fetch_add(1, std::memory_order_relaxed);
                        if (idx >= atlas_sprites.size()) break;
                        std::string error;
                        if (!blit_sprite(atlas_sprites[idx], error)) {
                            std::scoped_lock lock(err_mtx);
                            if (first_err.empty()) first_err = std::move(error);
                            failed.store(true, std::memory_order_relaxed);
                            break;
                        }
                    }
                });
            }
            for (auto& w : workers) w.join();
            if (failed.load(std::memory_order_relaxed)) { std::cerr << first_err << "\n"; return 1; }
        }

        const int active_extrude = has_extrude_override ? extrude : layout.extrude;
        if (active_extrude > 0) {
            extrude_atlas(atlas_data, atlas_width, atlas_height, atlas_sprites, active_extrude);
        }

        if (draw_frame_lines) {
            for (const auto& s : atlas_sprites) {
                draw_sprite_outline(atlas_data, atlas_width, atlas_height, s, line_width, line_color);
            }
        }

        if (std::getenv("SPRAT_PACK_DEBUG") && atlas_width <= 16) {
            for (int y = 0; y < atlas_height; ++y) {
                for (int x = 0; x < atlas_width; ++x) {
                    size_t off = (static_cast<size_t>(y) * atlas_width + x) * NUM_CHANNELS;
                    if (atlas_data[off+3] != 0) {
                        std::cerr << "[pixel-debug] x=" << x << " y=" << y << " RGBA=" 
                                  << (int)atlas_data[off] << "," << (int)atlas_data[off+1] << "," 
                                  << (int)atlas_data[off+2] << "," << (int)atlas_data[off+3] << "\n";
                    }
                }
            }
        }

        std::vector<unsigned char> png_data;
        auto write_to_vec = [](void* context, void* data, int size) {
            auto* vec = static_cast<std::vector<unsigned char>*>(context);
            const auto* bytes = static_cast<const unsigned char*>(data);
            vec->insert(vec->end(), bytes, bytes + size);
        };

        if (stbi_write_png_to_func(write_to_vec, &png_data, atlas_width, atlas_height, 4, atlas_data.data(), atlas_width * 4) == 0) {
            std::cerr << "Error: Failed to encode PNG for atlas " << atlas_idx << "\n";
            return 1;
        }

#ifdef SPRAT_HAS_ZOPFLI
        if (use_zopfli) {
            ZopfliPNGOptions options;
            std::vector<unsigned char> optimized;
            if (ZopfliPNGCompress(png_data, options, false, &optimized) == 0) {
                png_data = std::move(optimized);
            } else {
                std::cerr << "Warning: Zopfli optimization failed for atlas " << atlas_idx << "\n";
            }
        }
#endif

        if (protect) {
            const std::string key = "sprat";
            std::vector<unsigned char> protected_data = {'S', 'P', 'R', 'A', 'T', '!'};
            protected_data.reserve(png_data.size() + protected_data.size());
            for (size_t i = 0; i < png_data.size(); ++i) {
                protected_data.push_back(png_data[i] ^ static_cast<unsigned char>(key[i % key.size()]));
            }
            png_data = std::move(protected_data);
        }

        if (use_tar) {
            std::string filename = "atlas_" + std::to_string(atlas_idx) + ".png";
            struct archive_entry* entry = archive_entry_new();
            if (!entry) {
                std::cerr << "Error: Failed to create TAR entry\n";
                return 1;
            }
            archive_entry_set_pathname(entry, filename.c_str());
            archive_entry_set_size(entry, static_cast<la_int64_t>(png_data.size()));
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            
            if (archive_write_header(a.get(), entry) != ARCHIVE_OK) {
                std::cerr << "Error: Failed to write TAR header: " << archive_error_string(a.get()) << "\n";
                archive_entry_free(entry);
                return 1;
            }
            if (archive_write_data(a.get(), png_data.data(), png_data.size()) < 0) {
                std::cerr << "Error: Failed to write TAR data: " << archive_error_string(a.get()) << "\n";
                archive_entry_free(entry);
                return 1;
            }
            archive_entry_free(entry);
        } else if (output_pattern.empty()) {
#ifdef _WIN32
            if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
                std::cerr << "Failed to set stdout to binary mode\n";
                return 1;
            }
#endif
            std::cout.write(reinterpret_cast<const char*>(png_data.data()), static_cast<std::streamsize>(png_data.size()));
        } else {
            char filename_buf[1024];
            int written = 0;
#ifdef _WIN32
            written = _snprintf(filename_buf, sizeof(filename_buf), output_pattern.c_str(), static_cast<int>(atlas_idx));
#else
            written = snprintf(filename_buf, sizeof(filename_buf), output_pattern.c_str(), static_cast<int>(atlas_idx));
#endif
            if (written < 0 || static_cast<size_t>(written) >= sizeof(filename_buf)) {
                std::cerr << "Error: Output filename pattern resulted in a path too long or invalid\n";
                return 1;
            }
            std::ofstream out_file(filename_buf, std::ios::binary);
            if (!out_file) {
                std::cerr << "Error: Failed to open output file: " << filename_buf << "\n";
                return 1;
            }
            out_file.write(reinterpret_cast<const char*>(png_data.data()), static_cast<std::streamsize>(png_data.size()));
        }
    }

    return 0;
}
