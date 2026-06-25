#include <utility>
#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_image_resize2.h>

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
#include <filesystem>
#include <archive.h>
#include <archive_entry.h>
#include "core/layout_parser.h"
#include "core/cli_parse.h"
#include "core/i18n.h"
#include "core/output_pattern.h"

#ifdef SPRAT_HAS_ZOPFLI
#include <zopflipng/zopflipng_lib.h>
#endif

#ifdef SPRAT_HAS_SQUISH
#include <squish.h>
#endif

namespace {

constexpr size_t NUM_CHANNELS = 4;

enum class ScaleFilter { nearest, bilinear, bicubic, mitchell };

stbir_filter to_stbir_filter(ScaleFilter f) {
    switch (f) {
        case ScaleFilter::bilinear: return STBIR_FILTER_TRIANGLE;
        case ScaleFilter::bicubic:  return STBIR_FILTER_CATMULLROM;
        case ScaleFilter::mitchell: return STBIR_FILTER_MITCHELL;
        default:                    return STBIR_FILTER_POINT_SAMPLE;
    }
}
constexpr size_t CHANNEL_R = 0;
constexpr size_t CHANNEL_G = 1;
constexpr size_t CHANNEL_B = 2;
constexpr size_t CHANNEL_A = 3;
constexpr int MAX_CHANNEL_VALUE = 255;

using Sprite = sprat::core::Sprite;
using Layout = sprat::core::Layout;
using sprat::core::format_index_pattern;
using sprat::core::parse_int;
using sprat::core::parse_layout;
using sprat::core::to_quoted;
using sprat::core::tr;
using sprat::core::validate_output_pattern;

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

void dilate_sprite_colors(
    std::vector<unsigned char>& atlas,
    int atlas_width,
    int atlas_height,
    const std::vector<Sprite>& sprites,
    int radius
) {
    if (radius <= 0) {
        return;
    }

    // Two-buffer approach: read_buf holds the previous pass state, atlas is the write target.
    // After each pass we copy only the affected sprite region back instead of the full atlas.
    std::vector<unsigned char> read_buf = atlas;

    auto get_pixel = [&](const std::vector<unsigned char>& buf, int px, int py, size_t channel) -> unsigned char {
        if (px < 0 || py < 0 || px >= atlas_width || py >= atlas_height) {
            return 0;
        }
        size_t offset = (static_cast<size_t>(py) * atlas_width + px) * NUM_CHANNELS + channel;
        return buf[offset];
    };

    auto set_pixel_rgb = [&](int px, int py, unsigned char r, unsigned char g, unsigned char b) {
        if (px < 0 || py < 0 || px >= atlas_width || py >= atlas_height) {
            return;
        }
        size_t offset = (static_cast<size_t>(py) * atlas_width + px) * NUM_CHANNELS;
        atlas[offset + CHANNEL_R] = r;
        atlas[offset + CHANNEL_G] = g;
        atlas[offset + CHANNEL_B] = b;
    };

    for (const auto& s : sprites) {
        if (s.w <= 0 || s.h <= 0) {
            continue;
        }

        // Clamp the dilate region to atlas bounds (sprite bbox expanded by 1 pixel)
        const int region_y0 = std::max(0, s.y - 1);
        const int region_y1 = std::min(atlas_height - 1, s.y + s.h);
        const int region_x0 = std::max(0, s.x - 1);
        const int region_x1 = std::min(atlas_width - 1, s.x + s.w);
        const size_t region_row_bytes = static_cast<size_t>(region_x1 - region_x0 + 1) * NUM_CHANNELS;

        // For each pass, dilate colors from opaque pixels to transparent neighbors
        for (int pass = 0; pass < radius; ++pass) {
            // Copy only the affected region from atlas to read_buf
            for (int y = region_y0; y <= region_y1; ++y) {
                size_t row_offset = (static_cast<size_t>(y) * atlas_width + region_x0) * NUM_CHANNELS;
                std::memcpy(&read_buf[row_offset], &atlas[row_offset], region_row_bytes);
            }

            // Check pixels around (and outside) each sprite
            for (int y = region_y0; y <= region_y1; ++y) {
                for (int x = region_x0; x <= region_x1; ++x) {
                    // Only process transparent pixels
                    if (get_pixel(read_buf, x, y, CHANNEL_A) != 0) {
                        continue;
                    }

                    // Check 4 cardinal directions for opaque neighbors
                    constexpr int dx[] = {-1, 1, 0, 0};
                    constexpr int dy[] = {0, 0, -1, 1};
                    for (int dir = 0; dir < 4; ++dir) {
                        int nx = x + dx[dir];
                        int ny = y + dy[dir];
                        unsigned char alpha = get_pixel(read_buf, nx, ny, CHANNEL_A);
                        if (alpha != 0) {
                            // Found opaque neighbor, copy its RGB
                            unsigned char r = get_pixel(read_buf, nx, ny, CHANNEL_R);
                            unsigned char g = get_pixel(read_buf, nx, ny, CHANNEL_G);
                            unsigned char b = get_pixel(read_buf, nx, ny, CHANNEL_B);
                            set_pixel_rgb(x, y, r, g, b);
                            break;  // Only copy from first opaque neighbor
                        }
                    }
                }
            }
        }
    }
}

#ifdef SPRAT_HAS_SQUISH
std::vector<unsigned char> compress_to_dds(
    const std::vector<unsigned char>& rgba_data,
    int width,
    int height,
    const std::string& format
) {
    std::vector<unsigned char> dds_output;

    // Validate dimensions are multiple of 4 (DXT requirement)
    if (width % 4 != 0 || height % 4 != 0) {
        return dds_output;  // Return empty on error
    }

    // Determine compression flags
    int squish_flags = 0;
    if (format == "dxt1" || format == "DXT1") {
        squish_flags = squish::kDxt1 | squish::kColourClusterFit;
    } else {
        squish_flags = squish::kDxt5 | squish::kColourClusterFit;
    }

    // Compute compressed size (DXT1: width*height/2, DXT5: width*height)
    size_t compressed_bytes = (format == "dxt1" || format == "DXT1")
                              ? (static_cast<size_t>(width) * static_cast<size_t>(height)) / 2
                              : static_cast<size_t>(width) * static_cast<size_t>(height);

    // Build minimal DDS header (128 bytes)
    struct DdsHeader {
        uint32_t magic;               // 0x20534444 = "DDS "
        uint32_t size;                // Header size (124 bytes)
        uint32_t flags;               // Surface descriptor flags
        uint32_t height;              // Texture height
        uint32_t width;               // Texture width
        uint32_t pitch_or_linear;     // Pitch or linear size
        uint32_t depth;               // Texture depth (0 for 2D)
        uint32_t mipmap_count;        // Number of mipmaps
        uint32_t reserved[11];        // Reserved/unused
        // PixelFormat (32 bytes)
        struct {
            uint32_t size;            // PixelFormat size (32 bytes)
            uint32_t flags;           // Flags
            uint32_t fourcc;          // FourCC code
            uint32_t rgb_bit_count;   // RGB bits
            uint32_t r_mask;          // Red mask
            uint32_t g_mask;          // Green mask
            uint32_t b_mask;          // Blue mask
            uint32_t a_mask;          // Alpha mask
        } pixel_format;
        // Caps (16 bytes)
        uint32_t caps1;
        uint32_t caps2;
        uint32_t caps3;
        uint32_t caps4;
        uint32_t reserved2;
    };
    static_assert(sizeof(DdsHeader) == 128, "DDS header must be 128 bytes");

    DdsHeader header = {};
    header.magic = 0x20534444;  // "DDS "
    header.size = 124;
    header.flags = 0x0001 | 0x0002 | 0x0004 | 0x1000;  // CAPS | HEIGHT | WIDTH | LINEARSIZE
    header.height = static_cast<uint32_t>(height);
    header.width = static_cast<uint32_t>(width);
    header.pitch_or_linear = static_cast<uint32_t>(compressed_bytes);
    header.depth = 0;
    header.mipmap_count = 1;

    header.pixel_format.size = 32;
    header.pixel_format.flags = 0x0004;  // FOURCC
    header.pixel_format.fourcc = (format == "dxt1" || format == "DXT1") ? 0x31545844 : 0x35545844;  // "DXT1" or "DXT5"

    header.caps1 = 0x1000;  // TEXTURE

    // Write header
    const unsigned char* header_bytes = reinterpret_cast<const unsigned char*>(&header);
    dds_output.insert(dds_output.end(), header_bytes, header_bytes + sizeof(header));

    // Compress and append data
    std::vector<unsigned char> compressed(compressed_bytes);
    squish::CompressImage(rgba_data.data(), static_cast<int>(width), static_cast<int>(height),
                         compressed.data(), squish_flags);
    dds_output.insert(dds_output.end(), compressed.begin(), compressed.end());

    return dds_output;
}
#endif


} // namespace

void print_usage() {
    std::cout << tr("Usage: spratpack [OPTIONS]\n")
              << tr("\n")
              << tr("Read layout text from stdin and write one or more PNG atlases.\n")
              << tr("Writes PNG to stdout for single-atlas input; TAR to stdout for multipack input.\n")
              << tr("\n")
              << tr("Options:\n")
              << tr("  -a, --atlas PATTERN    Output filename pattern (e.g. atlas_%d.png)\n")
              << tr("  --atlas-index N        Pick a specific atlas index to output\n")
              << tr("  --extrude N            Repeat edge pixels N times (overrides layout)\n")
              << tr("  --dilate N             Bleed opaque pixels into transparent neighbors (N passes)\n")
              << tr("  --gpu-compress FORMAT  Compress to DCS: dxt1 or dxt5 (requires libsquish)\n")
              << tr("  --frame-lines          Draw rectangle outlines for each sprite\n")
              << tr("  --line-width N         Outline thickness in pixels (default: 1)\n")
              << tr("  --line-color R,G,B[,A] Outline color channels (0-255, default: 255,0,0,255)\n")
              << tr("  --scale-filter FILTER  Resampling filter when source and target sizes differ:\n")
              << tr("                           nearest (default), bilinear, bicubic, mitchell\n")
              << tr("  --threads N            Number of worker threads\n")
              << tr("  --debug                Enable detailed error reporting and debug visualization\n")
              << tr("  --protect              Protect output with basic obfuscation\n")
              << tr("  --zopfli               Optimize output PNG using Zopfli (very slow)\n")
              << tr("  --help, -h             Show this help message\n")
              << tr("  --version, -v          Show version\n");
}

int run_spratpack(int argc, char** argv) {
    bool debug = false;
    bool protect = false;
    bool use_zopfli = false;
    bool draw_frame_lines = false;
    ScaleFilter scale_filter = ScaleFilter::nearest;
    int line_width = 1;
    constexpr unsigned char DEFAULT_COLOR_RED = 255;
    constexpr unsigned char DEFAULT_COLOR_ALPHA = 255;
    std::array<unsigned char, 4> line_color = {DEFAULT_COLOR_RED, 0, 0, DEFAULT_COLOR_ALPHA};
    unsigned int thread_limit = 0;
    std::string output_pattern;
    int requested_atlas_index = -1;
    int extrude = 0;
    bool has_extrude_override = false;
    int dilate = 0;
    bool has_dilate_override = false;
    std::string gpu_compress_format;
    bool has_gpu_compress = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << tr("spratpack version ") << SPRAT_VERSION << "\n";
            return 0;
        } else if (arg == "--debug") {
            debug = true;
        } else if (arg == "--protect") {
            protect = true;
        } else if (arg == "--zopfli") {
            use_zopfli = true;
        } else if ((arg == "--atlas" || arg == "-a" || arg == "--output" || arg == "-o") && i + 1 < argc) {
            output_pattern = argv[++i];
        } else if (arg == "--atlas-index" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_int(value, requested_atlas_index) || requested_atlas_index < 0) {
                std::cerr << tr("Invalid atlas index: ") << value << "\n";
                return 1;
            }
        } else if (arg == "--extrude" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_int(value, extrude) || extrude < 0) {
                std::cerr << tr("Invalid extrude value: ") << value << "\n";
                return 1;
            }
            has_extrude_override = true;
        } else if (arg == "--dilate" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_int(value, dilate) || dilate < 0) {
                std::cerr << tr("Invalid dilate value: ") << value << "\n";
                return 1;
            }
            has_dilate_override = true;
        } else if (arg == "--gpu-compress" && i + 1 < argc) {
            gpu_compress_format = argv[++i];
            std::string lower_format = gpu_compress_format;
            std::transform(lower_format.begin(), lower_format.end(), lower_format.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            if (lower_format != "dxt1" && lower_format != "dxt5") {
                std::cerr << tr("Invalid gpu-compress format: ") << gpu_compress_format << tr(" (must be dxt1 or dxt5)\n");
                return 1;
            }
#ifndef SPRAT_HAS_SQUISH
            std::cerr << tr("Error: --gpu-compress requires libsquish support (not compiled in)\n");
            return 1;
#endif
            has_gpu_compress = true;
        } else if (arg == "--frame-lines") {
            draw_frame_lines = true;
        } else if (arg == "--line-width" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_int(value, line_width) || line_width <= 0) {
                std::cerr << tr("Invalid line width: ") << value << "\n";
                return 1;
            }
        } else if (arg == "--line-color" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_line_color(value, line_color)) {
                std::cerr << tr("Invalid line color: ") << value << "\n";
                return 1;
            }
        } else if (arg == "--scale-filter" && i + 1 < argc) {
            std::string value = argv[++i];
            if (value == "nearest") {
                scale_filter = ScaleFilter::nearest;
            } else if (value == "bilinear") {
                scale_filter = ScaleFilter::bilinear;
            } else if (value == "bicubic") {
                scale_filter = ScaleFilter::bicubic;
            } else if (value == "mitchell") {
                scale_filter = ScaleFilter::mitchell;
            } else {
                std::cerr << tr("Invalid scale filter: ") << value << tr(" (must be nearest, bilinear, bicubic, or mitchell)\n");
                return 1;
            }
        } else if (arg == "--threads" && i + 1 < argc) {
            std::string value = argv[++i];
            int parsed = 0;
            if (!parse_int(value, parsed) || parsed <= 0) {
                std::cerr << tr("Invalid thread count: ") << value << "\n";
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

    // Resolve relative sprite paths using the root directory from the layout.
    if (layout.has_root && !layout.root.empty()) {
        std::filesystem::path root_path(layout.root);
        for (auto& sprite : layout.sprites) {
            std::filesystem::path sp(sprite.path);
            if (sp.is_relative()) {
                sprite.path = (root_path / sp).string();
            }
        }
        for (auto& alias : layout.aliases) {
            std::filesystem::path ap(alias.first);
            if (ap.is_relative()) {
                alias.first = (root_path / ap).string();
            }
            std::filesystem::path cp(alias.second);
            if (cp.is_relative()) {
                alias.second = (root_path / cp).string();
            }
        }
    }

    if (requested_atlas_index >= 0 && static_cast<size_t>(requested_atlas_index) >= layout.atlases.size()) {
        std::cerr << tr("Error: requested atlas index ") << requested_atlas_index
                  << tr(" out of range (total: ") << layout.atlases.size() << ")\n";
        return 1;
    }
    if (!output_pattern.empty()) {
        std::string pattern_error;
        if (!validate_output_pattern(output_pattern,
                                     layout.atlases.size(),
                                     requested_atlas_index < 0,
                                     pattern_error)) {
            std::cerr << tr("Invalid output pattern: ") << pattern_error << "\n";
            return 1;
        }
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
        // Non-fatal: in embedded mode stdout may not be a real file handle.
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        // Use a write callback instead of archive_write_open_FILE so that
        // output flows through std::cout.  In embedded mode std::cout is
        // redirected to a stringstream; writing to the C-level stdout FILE*
        // would bypass that redirection and lose all data.
        auto tar_write_cb = [](struct archive* /*unused*/, void* /*client_data*/,
                               const void* buffer, size_t length) -> la_ssize_t {
            std::cout.write(static_cast<const char*>(buffer),
                            static_cast<std::streamsize>(length));
            if (std::cout.fail()) return -1;
            return static_cast<la_ssize_t>(length);
        };
        if (archive_write_open(a.get(), nullptr, nullptr, tar_write_cb, nullptr) != ARCHIVE_OK) {
            std::cerr << tr("Failed to open TAR stream on stdout: ") << archive_error_string(a.get()) << "\n";
            return 1;
        }
    }

    // Pre-group sprites by atlas index
    std::vector<std::vector<Sprite>> sprites_by_atlas(layout.atlases.size());
    for (const auto& s : layout.sprites) {
        if (s.atlas_index >= 0 && static_cast<size_t>(s.atlas_index) < layout.atlases.size()) {
            sprites_by_atlas[static_cast<size_t>(s.atlas_index)].push_back(s);
        }
    }

    for (size_t atlas_idx = 0; atlas_idx < layout.atlases.size(); ++atlas_idx) {
        if (requested_atlas_index >= 0 && static_cast<size_t>(requested_atlas_index) != atlas_idx) {
            continue;
        }

        const int atlas_width = layout.atlases[atlas_idx].width;
        const int atlas_height = layout.atlases[atlas_idx].height;
        const std::vector<Sprite>& atlas_sprites = sprites_by_atlas[atlas_idx];

        size_t pixel_count = 0;
        size_t byte_count = 0;
        if (!checked_mul_size_t(static_cast<size_t>(atlas_width), static_cast<size_t>(atlas_height), pixel_count)
            || !checked_mul_size_t(pixel_count, NUM_CHANNELS, byte_count)) {
            std::cerr << tr("Error: Atlas ") << atlas_idx << tr(" dimensions are too large for memory allocation\n");
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

            // Unrotated destination dimensions (rotation swaps w/h in the atlas slot)
            const int dest_w = s.rotated ? s.h : s.w;
            const int dest_h = s.rotated ? s.w : s.h;
            const bool needs_scale = (source_w != dest_w || source_h != dest_h);

            // Fast path: no rotation, no scaling
            if (!s.rotated && !needs_scale) {
                const size_t row_bytes = static_cast<size_t>(s.w) * NUM_CHANNELS;
                for (int row = 0; row < s.h; ++row) {
                    const size_t dest_pixels = static_cast<size_t>(s.y + row) * atlas_width + s.x;
                    const size_t dest_offset = dest_pixels * NUM_CHANNELS;
                    const size_t src_pixels = static_cast<size_t>(source_y + row) * w + source_x;
                    const size_t src_offset = src_pixels * NUM_CHANNELS;
                    std::memcpy(atlas_data.data() + dest_offset, image_ptr.get() + src_offset, row_bytes);
                }
                return true;
            }

            // Source pointer and stride within the full loaded image
            const unsigned char* src_ptr = image_ptr.get() +
                (static_cast<size_t>(source_y) * w + source_x) * NUM_CHANNELS;
            const int src_stride_bytes = w * static_cast<int>(NUM_CHANNELS);

            // Scale if source and destination sizes differ
            std::vector<unsigned char> scaled_buf;
            const unsigned char* blit_ptr = src_ptr;
            int blit_stride_bytes = src_stride_bytes;

            if (needs_scale) {
                scaled_buf.resize(static_cast<size_t>(dest_w) * dest_h * NUM_CHANNELS);
                STBIR_RESIZE resize;
                stbir_resize_init(&resize,
                    src_ptr, source_w, source_h, src_stride_bytes,
                    scaled_buf.data(), dest_w, dest_h, 0,
                    STBIR_RGBA, STBIR_TYPE_UINT8_SRGB);
                stbir_set_filters(&resize,
                    to_stbir_filter(scale_filter), to_stbir_filter(scale_filter));
                if (!stbir_resize_extended(&resize)) {
                    error_out = "Error: Failed to resize image " + to_quoted(s.path);
                    return false;
                }
                blit_ptr = scaled_buf.data();
                blit_stride_bytes = dest_w * static_cast<int>(NUM_CHANNELS);
            }

            // Blit to atlas, handling 90° CW rotation if needed
            if (!s.rotated) {
                const size_t row_bytes = static_cast<size_t>(dest_w) * NUM_CHANNELS;
                for (int row = 0; row < dest_h; ++row) {
                    const size_t dest_off =
                        (static_cast<size_t>(s.y + row) * atlas_width + s.x) * NUM_CHANNELS;
                    const size_t src_off =
                        static_cast<size_t>(row) * static_cast<size_t>(blit_stride_bytes);
                    std::memcpy(atlas_data.data() + dest_off, blit_ptr + src_off, row_bytes);
                }
            } else {
                // atlas(s.x+col, s.y+row) <- source(px=row, py=dest_h-1-col)
                for (int row = 0; row < s.h; ++row) {
                    for (int col = 0; col < s.w; ++col) {
                        const int px = row;
                        const int py = dest_h - 1 - col;
                        const size_t dest_off =
                            (static_cast<size_t>(s.y + row) * atlas_width + (s.x + col)) * NUM_CHANNELS;
                        const size_t src_off =
                            static_cast<size_t>(py) * static_cast<size_t>(blit_stride_bytes) +
                            static_cast<size_t>(px) * NUM_CHANNELS;
                        std::memcpy(atlas_data.data() + dest_off, blit_ptr + src_off, NUM_CHANNELS);
                    }
                }
            }
            return true;
        };

        unsigned int atlas_worker_count = thread_limit > 0 ? thread_limit : std::thread::hardware_concurrency();
        if (atlas_worker_count == 0) atlas_worker_count = 1;
        atlas_worker_count = std::min<unsigned int>(atlas_worker_count, static_cast<unsigned int>(std::max<size_t>(1, atlas_sprites.size())));
#ifdef __EMSCRIPTEN__
        atlas_worker_count = 1;
#endif

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

        const int active_dilate = has_dilate_override ? dilate : 0;
        if (active_dilate > 0) {
            dilate_sprite_colors(atlas_data, atlas_width, atlas_height, atlas_sprites, active_dilate);
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

        std::vector<unsigned char> output_data;
        std::string output_extension = ".png";

#ifdef SPRAT_HAS_SQUISH
        if (has_gpu_compress) {
            output_data = compress_to_dds(atlas_data, atlas_width, atlas_height, gpu_compress_format);
            if (output_data.empty()) {
                std::cerr << tr("Error: Failed to compress to DDS for atlas ") << atlas_idx << tr(" (atlas dimensions must be multiple of 4)\n");
                return 1;
            }
            output_extension = ".dds";
        } else
#endif
        {
            auto write_to_vec = [](void* context, void* data, int size) {
                auto* vec = static_cast<std::vector<unsigned char>*>(context);
                const auto* bytes = static_cast<const unsigned char*>(data);
                vec->insert(vec->end(), bytes, bytes + size);
            };

            if (stbi_write_png_to_func(write_to_vec, &output_data, atlas_width, atlas_height, 4, atlas_data.data(), atlas_width * 4) == 0) {
                std::cerr << tr("Error: Failed to encode PNG for atlas ") << atlas_idx << "\n";
                return 1;
            }

#ifdef SPRAT_HAS_ZOPFLI
            if (use_zopfli) {
                ZopfliPNGOptions options;
                std::vector<unsigned char> optimized;
                if (ZopfliPNGCompress(output_data, options, false, &optimized) == 0) {
                    output_data = std::move(optimized);
                } else {
                    std::cerr << tr("Warning: Zopfli optimization failed for atlas ") << atlas_idx << "\n";
                }
            }
#endif
        }

        if (protect && !has_gpu_compress) {
            const std::string key = "sprat";
            std::vector<unsigned char> protected_data = {'S', 'P', 'R', 'A', 'T', '!'};
            protected_data.reserve(output_data.size() + protected_data.size());
            for (size_t i = 0; i < output_data.size(); ++i) {
                protected_data.push_back(output_data[i] ^ static_cast<unsigned char>(key[i % key.size()]));
            }
            output_data = std::move(protected_data);
        }

        if (use_tar) {
            std::string filename = "atlas_" + std::to_string(atlas_idx) + output_extension;
            struct archive_entry* entry = archive_entry_new();
            if (!entry) {
                std::cerr << tr("Error: Failed to create TAR entry\n");
                return 1;
            }
            archive_entry_set_pathname(entry, filename.c_str());
            archive_entry_set_size(entry, static_cast<la_int64_t>(output_data.size()));
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);

            if (archive_write_header(a.get(), entry) != ARCHIVE_OK) {
                std::cerr << tr("Error: Failed to write TAR header: ") << archive_error_string(a.get()) << "\n";
                archive_entry_free(entry);
                return 1;
            }
            if (archive_write_data(a.get(), output_data.data(), output_data.size()) < 0) {
                std::cerr << tr("Error: Failed to write TAR data: ") << archive_error_string(a.get()) << "\n";
                archive_entry_free(entry);
                return 1;
            }
            archive_entry_free(entry);
        } else if (output_pattern.empty()) {
#ifdef _WIN32
            // Non-fatal: in embedded mode stdout may not be a real file handle.
            _setmode(_fileno(stdout), _O_BINARY);
#endif
            std::cout.write(reinterpret_cast<const char*>(output_data.data()), static_cast<std::streamsize>(output_data.size()));
        } else {
            std::string filename;
            std::string pattern_error;
            if (!format_index_pattern(output_pattern, static_cast<int>(atlas_idx), filename, pattern_error)) {
                std::cerr << tr("Invalid output pattern: ") << pattern_error << "\n";
                return 1;
            }
            std::ofstream out_file(filename, std::ios::binary);
            if (!out_file) {
                std::cerr << tr("Error: Failed to open output file: ") << filename << "\n";
                return 1;
            }
            out_file.write(reinterpret_cast<const char*>(output_data.data()), static_cast<std::streamsize>(output_data.size()));
        }
    }

    return 0;
}
