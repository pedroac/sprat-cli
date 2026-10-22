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
#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <limits>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <archive.h>
#include <archive_entry.h>
#include "core/layout_parser.h"
#include "core/checked_math.h"
#include "core/file_io.h"
#include "core/parallel.h"
#include "core/cli_parse.h"
#include "core/i18n.h"
#include "core/output_pattern.h"

#ifdef SPRAT_HAS_ZOPFLI
#include <zopflipng/zopflipng_lib.h>
#endif

#ifdef SPRAT_HAS_SQUISH
#include <squish.h>
#endif

#ifdef SPRAT_HAS_WEBP
#include <webp/encode.h>
#endif

#ifdef SPRAT_HAS_AVIF
#include <avif/avif.h>
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
using sprat::core::checked_mul_size_t;
using sprat::core::validate_output_pattern;
namespace fs = std::filesystem;

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

// Insert suffix before the last file extension.
// Example: apply_path_suffix("sprites/hero.png", "_n") -> "sprites/hero_n.png"
std::string apply_path_suffix(const std::string& path, const std::string& suffix) {
    if (suffix.empty()) return path;
    const fs::path p(path);
    const std::string ext = p.extension().string();
    const std::string stem = p.stem().string();
    const fs::path parent = p.parent_path();
    return (parent / (stem + suffix + ext)).string();
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


// Sprites other than sprites[self] whose rectangles reach into [x0, x1) x [y0, y1). Extrude
// and dilate write around a sprite but must never touch another sprite's pixels.
std::vector<const Sprite*> other_sprites_in(const std::vector<Sprite>& sprites, size_t self,
                                            int x0, int y0, int x1, int y1) {
    std::vector<const Sprite*> others;
    for (size_t i = 0; i < sprites.size(); ++i) {
        const Sprite& o = sprites[i];
        if (i != self && o.x < x1 && o.x + o.w > x0 && o.y < y1 && o.y + o.h > y0) {
            others.push_back(&o);
        }
    }
    return others;
}

bool inside_any(const std::vector<const Sprite*>& rects, int x, int y) {
    return std::ranges::any_of(rects, [&](const Sprite* o) {
        return x >= o->x && x < o->x + o->w && y >= o->y && y < o->y + o->h;
    });
}

// Repeats each sprite's edge pixels `extrude` times outward. Pixels inside another sprite's
// rectangle are left alone, so a layout with less padding than 2 * extrude is never corrupted;
// the extrusion is just cut short there.
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

    for (size_t si = 0; si < sprites.size(); ++si) {
        const Sprite& s = sprites[si];
        if (s.w <= 0 || s.h <= 0) {
            continue;
        }
        const long long grow = extrude;
        const std::vector<const Sprite*> others = other_sprites_in(
            sprites, si,
            static_cast<int>(std::max<long long>(0, s.x - grow)), static_cast<int>(std::max<long long>(0, s.y - grow)),
            static_cast<int>(std::min<long long>(atlas_width, static_cast<long long>(s.x) + s.w + grow)),
            static_cast<int>(std::min<long long>(atlas_height, static_cast<long long>(s.y) + s.h + grow)));

        auto set_pixel = [&](int dx, int dy, int sx, int sy) {
            if (dx < 0 || dy < 0 || dx >= atlas_width || dy >= atlas_height ||
                sx < 0 || sy < 0 || sx >= atlas_width || sy >= atlas_height || inside_any(others, dx, dy)) {
                return;
            }
            size_t dest_offset = (static_cast<size_t>(dy) * atlas_width + dx) * NUM_CHANNELS;
            size_t src_offset = (static_cast<size_t>(sy) * atlas_width + sx) * NUM_CHANNELS;
            std::memcpy(&atlas[dest_offset], &atlas[src_offset], NUM_CHANNELS);
        };

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
                set_pixel(s.x - ex, s.y - ey, s.x, s.y);
                set_pixel(s.x + s.w - 1 + ex, s.y - ey, s.x + s.w - 1, s.y);
                set_pixel(s.x - ex, s.y + s.h - 1 + ey, s.x, s.y + s.h - 1);
                set_pixel(s.x + s.w - 1 + ex, s.y + s.h - 1 + ey, s.x + s.w - 1, s.y + s.h - 1);
            }
        }
    }
}

// Fills the RGB of transparent pixels around and inside each sprite with the color of the
// nearest visible pixel, `radius` pixels deep, keeping alpha at 0, so GPU filtering at sprite
// edges does not pull in black. Each pass spreads from pixels colored by the previous one.
// The fill stays within the sprite's rectangle grown by `radius` and never enters another
// sprite's rectangle.
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

    for (size_t si = 0; si < sprites.size(); ++si) {
        const Sprite& s = sprites[si];
        if (s.w <= 0 || s.h <= 0) {
            continue;
        }
        const long long grow = radius;
        const int x0 = static_cast<int>(std::max<long long>(0, s.x - grow));
        const int y0 = static_cast<int>(std::max<long long>(0, s.y - grow));
        const int x1 = static_cast<int>(std::min<long long>(atlas_width, static_cast<long long>(s.x) + s.w + grow));
        const int y1 = static_cast<int>(std::min<long long>(atlas_height, static_cast<long long>(s.y) + s.h + grow));
        if (x0 >= x1 || y0 >= y1) {
            continue;
        }
        const int rw = x1 - x0;
        const int rh = y1 - y0;

        const std::vector<const Sprite*> others = other_sprites_in(sprites, si, x0, y0, x1, y1);

        // 1 = visible or already filled: a source for the next pass.
        std::vector<std::uint8_t> source(static_cast<size_t>(rw) * rh, 0);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const size_t offset = (static_cast<size_t>(y) * atlas_width + x) * NUM_CHANNELS;
                source[static_cast<size_t>(y - y0) * rw + (x - x0)] = atlas[offset + CHANNEL_A] != 0 ? 1 : 0;
            }
        }
        std::vector<std::uint8_t> next = source;
        constexpr int dx[] = {-1, 1, 0, 0};
        constexpr int dy[] = {0, 0, -1, 1};
        for (int pass = 0; pass < radius; ++pass) {
            bool changed = false;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const size_t local = static_cast<size_t>(y - y0) * rw + (x - x0);
                    if (source[local] != 0 || inside_any(others, x, y)) {
                        continue;
                    }
                    for (int dir = 0; dir < 4; ++dir) {
                        const int nx = x + dx[dir];
                        const int ny = y + dy[dir];
                        if (nx < x0 || ny < y0 || nx >= x1 || ny >= y1 ||
                            source[static_cast<size_t>(ny - y0) * rw + (nx - x0)] == 0) {
                            continue;
                        }
                        const size_t from = (static_cast<size_t>(ny) * atlas_width + nx) * NUM_CHANNELS;
                        const size_t to = (static_cast<size_t>(y) * atlas_width + x) * NUM_CHANNELS;
                        atlas[to + CHANNEL_R] = atlas[from + CHANNEL_R];
                        atlas[to + CHANNEL_G] = atlas[from + CHANNEL_G];
                        atlas[to + CHANNEL_B] = atlas[from + CHANNEL_B];
                        next[local] = 1;
                        changed = true;
                        break;  // first visible or filled neighbour wins
                    }
                }
            }
            if (!changed) {
                break;
            }
            source = next;
        }
    }
}

#ifdef SPRAT_HAS_SQUISH
// A block's two colour endpoints are fitted to all 16 of its pixels, so the (invisible) RGB of
// fully transparent pixels next to a sprite edge or in the padding would pull visible colours
// towards it; libsquish's range fit ignores alpha weighting. Give every fully transparent pixel
// the mean colour of the visible pixels of its 4x4 block instead.
void fill_invisible_block_colours(std::vector<unsigned char>& rgba, int width, int height) {
    for (int by = 0; by < height; by += 4) {
        for (int bx = 0; bx < width; bx += 4) {
            unsigned sum[3] = {0, 0, 0};
            unsigned visible = 0;
            for (int y = by; y < by + 4; ++y) {
                for (int x = bx; x < bx + 4; ++x) {
                    const unsigned char* p = &rgba[(static_cast<size_t>(y) * width + x) * NUM_CHANNELS];
                    if (p[CHANNEL_A] != 0) {
                        sum[0] += p[CHANNEL_R];
                        sum[1] += p[CHANNEL_G];
                        sum[2] += p[CHANNEL_B];
                        ++visible;
                    }
                }
            }
            if (visible == 0 || visible == 16) {
                continue;
            }
            for (int y = by; y < by + 4; ++y) {
                for (int x = bx; x < bx + 4; ++x) {
                    unsigned char* p = &rgba[(static_cast<size_t>(y) * width + x) * NUM_CHANNELS];
                    if (p[CHANNEL_A] == 0) {
                        p[CHANNEL_R] = static_cast<unsigned char>(sum[0] / visible);
                        p[CHANNEL_G] = static_cast<unsigned char>(sum[1] / visible);
                        p[CHANNEL_B] = static_cast<unsigned char>(sum[2] / visible);
                    }
                }
            }
        }
    }
}

// libsquish 1.15 computes storage sizes and pixel/block offsets in int, so it writes past its
// buffers once the padded RGBA image exceeds INT_MAX bytes (about 23170x23170; Direct3D
// itself stops at 16384x16384). Such atlases are refused before anything is allocated.
bool dds_size_supported(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const long long padded_w = (static_cast<long long>(width) + 3) / 4 * 4;
    const long long padded_h = (static_cast<long long>(height) + 3) / 4 * 4;
    return padded_w * padded_h * NUM_CHANNELS <= std::numeric_limits<int>::max();
}

// Encodes the atlas as a single-level DXT1/DXT5 DDS texture. The image is padded with
// transparent pixels to a multiple of 4 (Direct3D 10+ require it for block-compressed
// textures; sprite coordinates are unchanged because the padding is at the right and bottom).
// Rows of 4x4 blocks are independent, so strips of them are compressed on `threads` workers.
// `best` selects libsquish's cluster fit, about 30x slower than the default range fit.
std::vector<unsigned char> compress_to_dds(
    const std::vector<unsigned char>& rgba_data,
    int width,
    int height,
    bool dxt1,
    bool best,
    unsigned int threads
) {
    if (!dds_size_supported(width, height)) {
        return {};
    }
    const int padded_w = (width + 3) / 4 * 4;
    const int padded_h = (height + 3) / 4 * 4;
    std::vector<unsigned char> padded(static_cast<size_t>(padded_w) * padded_h * NUM_CHANNELS, 0);
    for (int y = 0; y < height; ++y) {
        std::memcpy(&padded[static_cast<size_t>(y) * padded_w * NUM_CHANNELS],
                    &rgba_data[static_cast<size_t>(y) * width * NUM_CHANNELS],
                    static_cast<size_t>(width) * NUM_CHANNELS);
    }
    fill_invisible_block_colours(padded, padded_w, padded_h);
    const unsigned char* pixels = padded.data();

    // Weighting colour by alpha keeps transparent pixels (sprite margins, padding) from
    // dragging the colours of visible pixels that share a 4x4 block towards their RGB.
    const int squish_flags = (dxt1 ? squish::kDxt1 : squish::kDxt5) |
                             (best ? squish::kColourClusterFit : squish::kColourRangeFit) |
                             squish::kWeightColourByAlpha;
    const size_t block_bytes = dxt1 ? 8 : 16;
    const int blocks_x = padded_w / 4;
    const int block_rows = padded_h / 4;
    // Not squish::GetStorageRequirements, whose int result could wrap.
    const size_t data_bytes = static_cast<size_t>(blocks_x) * static_cast<size_t>(block_rows) * block_bytes;
    std::vector<unsigned char> dds(128 + data_bytes, 0);

    // DDS_HEADER, little-endian (Microsoft's DDS reference).
    size_t at = 0;
    const auto put = [&](uint32_t v) {
        for (int b = 0; b < 4; ++b) {
            dds[at++] = static_cast<unsigned char>((v >> (8 * b)) & 0xFFU);
        }
    };
    constexpr uint32_t k_caps = 0x1, k_height = 0x2, k_width = 0x4, k_pixelformat = 0x1000;
    constexpr uint32_t k_linearsize = 0x80000, k_fourcc = 0x4, k_texture = 0x1000;
    put(0x20534444);  // "DDS "
    put(124);         // header size
    put(k_caps | k_height | k_width | k_pixelformat | k_linearsize);
    put(static_cast<uint32_t>(padded_h));
    put(static_cast<uint32_t>(padded_w));
    put(static_cast<uint32_t>(data_bytes));  // linear size of the top level
    put(0);                                  // depth
    put(1);                                  // mipmap count
    at += 11 * 4;                            // reserved
    put(32);                                 // pixel format size
    put(k_fourcc);
    put(dxt1 ? 0x31545844 : 0x35545844);     // "DXT1" / "DXT5"
    at += 5 * 4;                             // RGB bit count and masks (unused with FOURCC)
    put(k_texture);                          // caps; caps2-4 and reserved stay 0

    unsigned char* blocks = dds.data() + 128;
    const int workers = static_cast<int>(std::clamp<unsigned int>(threads, 1, static_cast<unsigned int>(block_rows)));
    const int rows_per_worker = (block_rows + workers - 1) / workers;
    const auto compress_rows = [&](int first_row, int row_count) {
        squish::CompressImage(pixels + static_cast<size_t>(first_row) * 4 * padded_w * NUM_CHANNELS,
                              padded_w, row_count * 4,
                              blocks + static_cast<size_t>(first_row) * blocks_x * block_bytes,
                              squish_flags);
    };
    const int strips = (block_rows + rows_per_worker - 1) / rows_per_worker;
    sprat::core::run_workers(static_cast<unsigned int>(strips), [&](unsigned int strip) {
        const int first = static_cast<int>(strip) * rows_per_worker;
        compress_rows(first, std::min(rows_per_worker, block_rows - first));
    });
    return dds;
}
#endif

#ifdef SPRAT_HAS_WEBP
// Lossless (quality 100) keeps every pixel exactly. libwebp's simple lossless API rewrites the
// colour of fully transparent pixels, which would undo --dilate and change the atlas, so the
// advanced API is used with `exact` set.
std::vector<unsigned char> encode_webp(
    const unsigned char* rgba_data,
    int width,
    int height,
    int quality
) {
    std::vector<unsigned char> result;
    WebPConfig config;
    WebPPicture picture;
    if (!WebPConfigInit(&config) || !WebPPictureInit(&picture)) {
        return result;
    }
    const bool lossless = quality >= 100 || quality < 0;
    config.lossless = lossless ? 1 : 0;
    config.quality = lossless ? 75.0f : static_cast<float>(quality);  // lossless: effort, not fidelity
    config.exact = lossless ? 1 : 0;
    if (!WebPValidateConfig(&config)) {
        return result;
    }
    picture.use_argb = 1;
    picture.width = width;
    picture.height = height;
    if (!WebPPictureImportRGBA(&picture, rgba_data, width * 4)) {
        WebPPictureFree(&picture);
        return result;
    }
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;
    if (WebPEncode(&config, &picture) && writer.size > 0) {
        result.assign(writer.mem, writer.mem + writer.size);
    }
    WebPMemoryWriterClear(&writer);
    WebPPictureFree(&picture);
    return result;
}
#endif

#ifdef SPRAT_HAS_AVIF
std::vector<unsigned char> encode_avif(
    const unsigned char* rgba_data,
    int width,
    int height,
    int quality
) {
    std::vector<unsigned char> result;

    avifImage* image = avifImageCreate(width, height, 8, AVIF_PIXEL_FORMAT_YUV444);
    if (!image) return result;

    image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
    image->yuvRange = AVIF_RANGE_FULL;

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format = AVIF_RGB_FORMAT_RGBA;
    rgb.depth = 8;
    rgb.pixels = const_cast<uint8_t*>(rgba_data);
    rgb.rowBytes = static_cast<uint32_t>(width) * 4;

    if (avifImageRGBToYUV(image, &rgb) != AVIF_RESULT_OK) {
        avifImageDestroy(image);
        return result;
    }

    avifEncoder* encoder = avifEncoderCreate();
    if (!encoder) {
        avifImageDestroy(image);
        return result;
    }

    if (quality >= 100 || quality < 0) {
        encoder->quality = AVIF_QUALITY_LOSSLESS;
        encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
    } else {
        encoder->quality = quality;
        encoder->qualityAlpha = quality;
    }
    encoder->speed = AVIF_SPEED_DEFAULT;

    avifRWData output = AVIF_DATA_EMPTY;
    avifResult addResult = avifEncoderAddImage(encoder, image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
    if (addResult == AVIF_RESULT_OK) {
        avifResult finishResult = avifEncoderFinish(encoder, &output);
        if (finishResult == AVIF_RESULT_OK && output.size > 0) {
            result.assign(output.data, output.data + output.size);
        }
    }

    avifRWDataFree(&output);
    avifEncoderDestroy(encoder);
    avifImageDestroy(image);
    return result;
}
#endif

} // namespace

void print_usage() {
    std::cout << tr("Usage: spratpack [OPTIONS]\n")
              << tr("\n")
              << tr("Read layout text from stdin and write one or more image atlases.\n")
              << tr("Writes PNG to stdout for single-atlas input; TAR to stdout for multipack input.\n")
              << tr("\n")
              << tr("Options:\n")
              << tr("  -a, --atlas PATTERN    Output filename pattern (e.g. atlas_%d.png); alias: -o, --output\n")
              << tr("  --atlas-index N        Pick a specific atlas index to output\n")
              << tr("  --extrude N            Repeat edge pixels N times (overrides layout)\n")
              << tr("  --dilate N             Bleed opaque pixels into transparent neighbors (N passes)\n")
              << tr("  --gpu-compress FORMAT  Compress to DDS: dxt1 or dxt5 (requires libsquish)\n")
              << tr("  --gpu-quality fast|best  DDS compression quality (default: fast; best is much slower)\n")
              << tr("  --frame-lines          Draw rectangle outlines for each sprite\n")
              << tr("  --line-width N         Outline thickness in pixels (default: 1)\n")
              << tr("  --line-color R,G,B[,A] Outline color channels (0-255, default: 255,0,0,255)\n")
              << tr("  --scale-filter FILTER  Resampling filter when source and target sizes differ:\n")
              << tr("                           nearest (default), bilinear, bicubic, mitchell\n")
              << tr("  --threads N            Number of worker threads\n")
              << tr("  --debug                Enable detailed error reporting and debug visualization\n")
              << tr("  --protect              Protect output with basic obfuscation\n")
              << tr("  --format FORMAT        Output format: png (default), webp, or avif\n")
              << tr("  --quality N            Encoding quality 0-100 (100 = lossless, default: lossless)\n")
              << tr("  --path-suffix SUFFIX   Insert suffix before extension when loading images (e.g. _n loads hero_n.png)\n")
              << tr("  --output-scale FACTOR  Scale output atlas dimensions by FACTOR (e.g. 0.5 for half size)\n")
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
    std::string gpu_compress_format;  // lowercase: dxt1 or dxt5
    bool gpu_quality_best = false;
    bool has_gpu_compress = false;
    std::string output_format = "png";
    int quality = -1;  // -1 = default (lossless for webp/avif)
    std::string path_suffix;
    double output_scale = 1.0;
    static const std::unordered_set<std::string> k_value_options = {
        "--atlas", "-a", "--output", "-o", "--atlas-index", "--extrude", "--dilate",
        "--gpu-compress", "--gpu-quality", "--line-width", "--line-color", "--scale-filter", "--threads",
        "--format", "--quality", "--path-suffix", "--output-scale"
    };
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (k_value_options.contains(arg) && i + 1 >= argc) {
            std::cerr << tr("Error: Missing value for ") << arg << "\n";
            return 1;
        }
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
#ifndef SPRAT_HAS_ZOPFLI
            std::cerr << tr("Error: --zopfli requires zopflipng support (not compiled in)\n");
            return 1;
#endif
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
            const std::string value = argv[++i];
            gpu_compress_format = value;
            std::transform(gpu_compress_format.begin(), gpu_compress_format.end(), gpu_compress_format.begin(),
                          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (gpu_compress_format != "dxt1" && gpu_compress_format != "dxt5") {
                std::cerr << tr("Invalid gpu-compress format: ") << value << tr(" (must be dxt1 or dxt5)\n");
                return 1;
            }
#ifndef SPRAT_HAS_SQUISH
            std::cerr << tr("Error: --gpu-compress requires libsquish support (not compiled in)\n");
            return 1;
#endif
            has_gpu_compress = true;
        } else if (arg == "--gpu-quality" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value != "fast" && value != "best") {
                std::cerr << tr("Invalid gpu-quality: ") << value << tr(" (must be fast or best)\n");
                return 1;
            }
            gpu_quality_best = value == "best";
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
        } else if (arg == "--format" && i + 1 < argc) {
            output_format = argv[++i];
            std::transform(output_format.begin(), output_format.end(), output_format.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            if (output_format != "png" && output_format != "webp" && output_format != "avif") {
                std::cerr << tr("Invalid format: ") << output_format << tr(" (must be png, webp, or avif)\n");
                return 1;
            }
        } else if (arg == "--quality" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_int(value, quality) || quality < 0 || quality > 100) {
                std::cerr << tr("Invalid quality value: ") << value << tr(" (must be 0-100)\n");
                return 1;
            }
        } else if (arg == "--path-suffix" && i + 1 < argc) {
            path_suffix = argv[++i];
        } else if (arg == "--output-scale" && i + 1 < argc) {
            std::string value = argv[++i];
            try {
                size_t pos = 0;
                double parsed = std::stod(value, &pos);
                if (pos != value.size() || !std::isfinite(parsed) || parsed <= 0.0) {
                    std::cerr << tr("Invalid output scale: ") << value << tr(" (must be a positive number)\n");
                    return 1;
                }
                output_scale = parsed;
            } catch (...) {
                std::cerr << tr("Invalid output scale: ") << value << tr(" (must be a positive number)\n");
                return 1;
            }
        } else {
            std::cerr << tr("Error: Unknown option: ") << arg << "\n";
            std::cerr << tr("Run 'spratpack --help' for usage.\n");
            return 1;
        }
    }

    if (use_zopfli && output_format != "png") {
        std::cerr << tr("Error: --zopfli only applies to PNG output\n");
        return 1;
    }

    if (debug) {
        draw_frame_lines = true;
    }

    if (output_format == "webp") {
#ifndef SPRAT_HAS_WEBP
        std::cerr << tr("Error: --format webp requires libwebp support (not compiled in)\n");
        return 1;
#endif
    } else if (output_format == "avif") {
#ifndef SPRAT_HAS_AVIF
        std::cerr << tr("Error: --format avif requires libavif support (not compiled in)\n");
        return 1;
#endif
    }

    // Default quality: lossless (100) for webp/avif when not specified
    if (quality < 0 && (output_format == "webp" || output_format == "avif")) {
        quality = 100;
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
            std::filesystem::path ap(alias.path);
            if (ap.is_relative()) {
                alias.path = (root_path / ap).string();
            }
            std::filesystem::path cp(alias.canonical_path);
            if (cp.is_relative()) {
                alias.canonical_path = (root_path / cp).string();
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
        auto tar_write_cb = [](struct archive* a, void* /*client_data*/,
                               const void* buffer, size_t length) -> la_ssize_t {
            std::cout.write(static_cast<const char*>(buffer),
                            static_cast<std::streamsize>(length));
            if (std::cout.fail()) {
                archive_set_error(a, EIO, "std::cout write error");
                return -1;
            }
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

        // Range-check in double before converting: a large atlas times the scale can
        // exceed INT_MAX, and an out-of-range cast would yield a bogus (tiny) atlas.
        const double scaled_width  = std::round(atlas_width  * output_scale);
        const double scaled_height = std::round(atlas_height * output_scale);
        constexpr double max_output_dim = static_cast<double>(std::numeric_limits<int>::max());
        if (!(scaled_width <= max_output_dim) || !(scaled_height <= max_output_dim)) {
            std::cerr << tr("Error: Scaled output atlas ") << atlas_idx << tr(" dimensions are too large\n");
            return 1;
        }
        const int output_width  = std::max(1, static_cast<int>(scaled_width));
        const int output_height = std::max(1, static_cast<int>(scaled_height));
        size_t output_pixel_count = 0;
        size_t output_byte_count  = 0;
        if (!checked_mul_size_t(static_cast<size_t>(output_width), static_cast<size_t>(output_height), output_pixel_count)
            || !checked_mul_size_t(output_pixel_count, NUM_CHANNELS, output_byte_count)) {
            std::cerr << tr("Error: Scaled output atlas ") << atlas_idx << tr(" dimensions are too large for memory allocation\n");
            return 1;
        }
#ifdef SPRAT_HAS_SQUISH
        if (has_gpu_compress && !dds_size_supported(output_width, output_height)) {
            std::cerr << tr("Error: Atlas ") << atlas_idx << " (" << output_width << "x" << output_height
                      << tr(") is too large for --gpu-compress (at most about 23170x23170; ")
                      << tr("Direct3D supports up to 16384x16384)\n");
            return 1;
        }
#endif
        std::vector<unsigned char> atlas_data(output_byte_count, 0);

        // Map a layout-space span [start, start+len) to output space. Both edges are
        // rounded (rather than start and length separately) so adjacent and edge-flush
        // slots stay disjoint and inside the scaled atlas.
        // Edges are clamped to [0, limit] in double before converting, so the int
        // results can't overflow.
        auto scale_span = [&](int start, int len, int limit, int& out_start, int& out_len) {
            const double max_edge = static_cast<double>(limit);
            double lo = std::clamp(std::round(start * output_scale), 0.0, max_edge);
            double hi = std::clamp(std::round((static_cast<double>(start) + len) * output_scale), 0.0, max_edge);
            if (hi <= lo) {
                if (lo < max_edge) {
                    hi = lo + 1.0;
                } else {
                    lo = max_edge - 1.0;
                    hi = max_edge;
                }
            }
            out_start = static_cast<int>(lo);
            out_len = static_cast<int>(hi - lo);
        };

        auto blit_sprite = [&](const Sprite& s, std::string& error_out) -> bool {
            const std::string load_path = path_suffix.empty() ? s.path : apply_path_suffix(s.path, path_suffix);
            int w = 0, h = 0, channels = 0;
            unsigned char* data = stbi_load(load_path.c_str(), &w, &h, &channels, static_cast<int>(NUM_CHANNELS));
            if (!data) {
                // stbi_failure_reason() is global, but since we are stopping on first error, it's acceptable here.
                error_out = std::string(tr("Failed to load image: ")) + to_quoted(load_path) + tr(" (Reason: ") + stbi_failure_reason() + ")";
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

            // 64-bit so out-of-range trim offsets are rejected instead of overflowing.
            const long long crop_w = s.has_trim
                ? static_cast<long long>(w) - s.src_x - s.trim_right : static_cast<long long>(w);
            const long long crop_h = s.has_trim
                ? static_cast<long long>(h) - s.src_y - s.trim_bottom : static_cast<long long>(h);
            const int source_x = s.has_trim ? s.src_x : 0;
            const int source_y = s.has_trim ? s.src_y : 0;

            if (source_x < 0 || source_y < 0 || crop_w <= 0 || crop_h <= 0 ||
                source_x > w - crop_w || source_y > h - crop_h) {
                error_out = tr("Error: Crop/Trim out of bounds for ") + to_quoted(load_path);
                return false;
            }
            const int source_w = static_cast<int>(crop_w);
            const int source_h = static_cast<int>(crop_h);

            // Scaled destination position and slot in the output atlas
            int sdest_x = 0, sdest_y = 0, sdest_slot_w = 0, sdest_slot_h = 0;
            scale_span(s.x, s.w, output_width,  sdest_x, sdest_slot_w);
            scale_span(s.y, s.h, output_height, sdest_y, sdest_slot_h);

            if (sdest_x < 0 || sdest_y < 0 || sdest_slot_w <= 0 || sdest_slot_h <= 0 ||
                sdest_x > output_width || sdest_y > output_height ||
                sdest_slot_w > output_width - sdest_x || sdest_slot_h > output_height - sdest_y) {
                error_out = std::string(tr("Error: Sprite ")) + to_quoted(load_path) + tr(" placement out of atlas bounds");
                return false;
            }

            // Unrotated destination dimensions (rotation swaps w/h in atlas slot)
            const int dest_w = s.rotated ? sdest_slot_h : sdest_slot_w;
            const int dest_h = s.rotated ? sdest_slot_w : sdest_slot_h;
            const bool needs_scale = (source_w != dest_w || source_h != dest_h);

            // Fast path: no rotation, no scaling
            if (!s.rotated && !needs_scale) {
                const size_t row_bytes = static_cast<size_t>(sdest_slot_w) * NUM_CHANNELS;
                for (int row = 0; row < sdest_slot_h; ++row) {
                    const size_t dest_pixels = static_cast<size_t>(sdest_y + row) * output_width + sdest_x;
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
                    error_out = tr("Error: Failed to resize image ") + to_quoted(load_path);
                    return false;
                }
                blit_ptr = scaled_buf.data();
                blit_stride_bytes = dest_w * static_cast<int>(NUM_CHANNELS);
            }

            // Blit to atlas, handling 90° rotation (CW by default, CCW for "rotation ccw") if needed
            if (!s.rotated) {
                const size_t row_bytes = static_cast<size_t>(dest_w) * NUM_CHANNELS;
                for (int row = 0; row < dest_h; ++row) {
                    const size_t dest_off =
                        (static_cast<size_t>(sdest_y + row) * output_width + sdest_x) * NUM_CHANNELS;
                    const size_t src_off =
                        static_cast<size_t>(row) * static_cast<size_t>(blit_stride_bytes);
                    std::memcpy(atlas_data.data() + dest_off, blit_ptr + src_off, row_bytes);
                }
            } else {
                // CW:  atlas(sdest_x+col, sdest_y+row) <- source(px=row, py=dest_h-1-col)
                // CCW: atlas(sdest_x+col, sdest_y+row) <- source(px=dest_w-1-row, py=col)
                for (int row = 0; row < sdest_slot_h; ++row) {
                    for (int col = 0; col < sdest_slot_w; ++col) {
                        const int px = layout.rotate_ccw ? dest_w - 1 - row : row;
                        const int py = layout.rotate_ccw ? col : dest_h - 1 - col;
                        const size_t dest_off =
                            (static_cast<size_t>(sdest_y + row) * output_width + (sdest_x + col)) * NUM_CHANNELS;
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
            sprat::core::run_workers(atlas_worker_count, [&](unsigned int) {
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
            if (failed.load(std::memory_order_relaxed)) { std::cerr << first_err << "\n"; return 1; }
        }

        // Scaled sprite list for post-processing (extrude, dilate, frame_lines)
        std::vector<Sprite> scaled_atlas_sprites;
        const std::vector<Sprite>* post_sprites = &atlas_sprites;
        if (output_scale != 1.0) {
            scaled_atlas_sprites.reserve(atlas_sprites.size());
            for (const auto& sp : atlas_sprites) {
                Sprite sc = sp;
                scale_span(sp.x, sp.w, output_width,  sc.x, sc.w);
                scale_span(sp.y, sp.h, output_height, sc.y, sc.h);
                scaled_atlas_sprites.push_back(sc);
            }
            post_sprites = &scaled_atlas_sprites;
        }

        // The layout's extrude reserves gaps in layout space; scale it with the atlas so it
        // doesn't overrun neighbouring sprites. An explicit --extrude is taken as output pixels.
        const int active_extrude = has_extrude_override
            ? extrude
            : static_cast<int>(std::min(std::round(layout.extrude * output_scale),
                                        static_cast<double>(std::numeric_limits<int>::max())));
        if (active_extrude > 0) {
            extrude_atlas(atlas_data, output_width, output_height, *post_sprites, active_extrude);
        }

        const int active_dilate = has_dilate_override ? dilate : 0;
        if (active_dilate > 0) {
            dilate_sprite_colors(atlas_data, output_width, output_height, *post_sprites, active_dilate);
        }

        if (draw_frame_lines) {
            for (const auto& s : *post_sprites) {
                draw_sprite_outline(atlas_data, output_width, output_height, s, line_width, line_color);
            }
        }

        if (std::getenv("SPRAT_PACK_DEBUG") && output_width <= 16) {
            for (int y = 0; y < output_height; ++y) {
                for (int x = 0; x < output_width; ++x) {
                    size_t off = (static_cast<size_t>(y) * output_width + x) * NUM_CHANNELS;
                    if (atlas_data[off+3] != 0) {
                        std::cerr << "[pixel-debug] x=" << x << " y=" << y << " RGBA=" 
                                  << (int)atlas_data[off] << "," << (int)atlas_data[off+1] << "," 
                                  << (int)atlas_data[off+2] << "," << (int)atlas_data[off+3] << "\n";
                    }
                }
            }
        }

        std::vector<unsigned char> output_data;
        std::string output_extension = "." + output_format;

#ifdef SPRAT_HAS_SQUISH
        if (has_gpu_compress) {
            unsigned int dds_threads = thread_limit > 0 ? thread_limit : std::thread::hardware_concurrency();
#ifdef __EMSCRIPTEN__
            dds_threads = 1;
#endif
            output_data = compress_to_dds(atlas_data, output_width, output_height,
                                          gpu_compress_format == "dxt1", gpu_quality_best, dds_threads);
            if (output_data.empty()) {
                std::cerr << tr("Error: Failed to encode DDS for atlas ") << atlas_idx << "\n";
                return 1;
            }
            output_extension = ".dds";
        } else
#endif
#ifdef SPRAT_HAS_WEBP
        if (output_format == "webp") {
            output_data = encode_webp(atlas_data.data(), output_width, output_height, quality);
            if (output_data.empty()) {
                std::cerr << tr("Error: Failed to encode WEBP for atlas ") << atlas_idx << "\n";
                return 1;
            }
        } else
#endif
#ifdef SPRAT_HAS_AVIF
        if (output_format == "avif") {
            output_data = encode_avif(atlas_data.data(), output_width, output_height, quality);
            if (output_data.empty()) {
                std::cerr << tr("Error: Failed to encode AVIF for atlas ") << atlas_idx << "\n";
                return 1;
            }
        } else
#endif
        {
            auto write_to_vec = [](void* context, void* data, int size) {
                auto* vec = static_cast<std::vector<unsigned char>*>(context);
                const auto* bytes = static_cast<const unsigned char*>(data);
                vec->insert(vec->end(), bytes, bytes + size);
            };

            if (stbi_write_png_to_func(write_to_vec, &output_data, output_width, output_height, 4, atlas_data.data(), output_width * 4) == 0) {
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
            std::string write_error;
            if (!sprat::core::write_file(filename,
                    std::string_view(reinterpret_cast<const char*>(output_data.data()), output_data.size()),
                    write_error)) {
                std::cerr << tr("Error: Failed to write output file ") << to_quoted(filename)
                          << ": " << write_error << "\n";
                return 1;
            }
        }
    }

    if (a) {
        // Closing writes the TAR trailer, which must reach stdout too.
        if (archive_write_close(a.get()) != ARCHIVE_OK) {
            std::cerr << tr("Error: Failed to write TAR data: ") << archive_error_string(a.get()) << "\n";
            return 1;
        }
    }
    if (output_to_stdout && !sprat::core::finish_stdout()) {
        return 1;
    }
    return 0;
}
