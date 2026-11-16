// SPDX-License-Identifier: GPL-3.0-only
// Player colours, as sm64coopdx's palettes (player_palette.h): eight parts of a player, each a colour. The game's
// player models carry their colours in their textures (the body's overalls, shirt and shoes in one, the cap and its
// emblem in another), so a palette is drawn by recoloured copies of those textures: each texel is read as one of the
// parts its material may hold (by the material's name), by how near its hue is to that part's colour in the model's own
// palette (its character's), and takes the wanted colour at the same lightness. Texels near no part (the buttons, the
// eyes, outlines, the white of the emblem) stay as they are.
//
// The game (NebulaPaletteSkin) and the host's Options page share this; nothing here allocates.
#pragma once

#include <nebula_cooked_texture.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace nebula_palette {

// sm64coopdx's PlayerPart, in its order (a palette's bytes, a co-op message's, NEBULA_PALETTE_* in the APIs).
enum Part : std::uint32_t { Pants, Shirt, Gloves, Shoes, Hair, Skin, Cap, Emblem, PartCount };

constexpr std::uint32_t bit(Part part) {
    return 1u << part;
}

// Each part's colour, red, green and blue.
struct Palette {
    std::uint8_t rgb[PartCount][3];
};
static_assert(sizeof(Palette) == 24, "a palette is 24 bytes, as the APIs and co-op messages carry it");

inline bool same(const Palette& a, const Palette& b) {
    return std::memcmp(&a, &b, sizeof(Palette)) == 0;
}

inline std::uint32_t color(const Palette& palette, std::uint32_t part) {
    if (part >= PartCount) return 0u;
    return static_cast<std::uint32_t>(palette.rgb[part][0]) << 16 | static_cast<std::uint32_t>(palette.rgb[part][1]) << 8 |
           palette.rgb[part][2];
}

inline void setColor(Palette& palette, std::uint32_t part, std::uint32_t rgb) {
    if (part >= PartCount) return;
    palette.rgb[part][0] = static_cast<std::uint8_t>(rgb >> 16);
    palette.rgb[part][1] = static_cast<std::uint8_t>(rgb >> 8);
    palette.rgb[part][2] = static_cast<std::uint8_t>(rgb);
}

// The game's two as their textures have them (the most common colour of each part in Mario.arc's and Luigi.arc's).
inline constexpr Palette cMario = {{{0x08, 0x30, 0x84}, {0xdc, 0x00, 0x00}, {0xff, 0xff, 0xff}, {0x6e, 0x32, 0x14},
                                    {0x4a, 0x14, 0x00}, {0xff, 0xaa, 0x84}, {0xb4, 0x00, 0x00}, {0xc4, 0x00, 0x00}}};
inline constexpr Palette cLuigi = {{{0x30, 0x38, 0x60}, {0x28, 0x94, 0x18}, {0xff, 0xff, 0xff}, {0x6e, 0x32, 0x14},
                                    {0x42, 0x13, 0x00}, {0xff, 0xaa, 0x84}, {0x20, 0x78, 0x10}, {0x21, 0x96, 0x39}}};

// A part's name as luma.json, runtime_settings.ini and the docs write it.
inline const char* partKey(std::uint32_t part) {
    static const char* const kKeys[PartCount] = {"pants", "shirt", "gloves", "shoes", "hair", "skin", "cap", "emblem"};
    return part < PartCount ? kKeys[part] : "";
}

namespace detail {
    inline char lower(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    inline bool equal(const char* a, const char* b) {
        for (; *a != '\0' && *b != '\0'; ++a, ++b) {
            if (lower(*a) != lower(*b)) return false;
        }
        return *a == *b;
    }

    // `word` (lower case) is in `text`, in any case.
    inline bool contains(const char* text, const char* word) {
        if (text == nullptr) return false;
        const std::size_t length = std::strlen(word);
        for (const char* at = text; *at != '\0'; ++at) {
            std::size_t i = 0;
            while (i < length && at[i] != '\0' && lower(at[i]) == word[i]) ++i;
            if (i == length) return true;
        }
        return false;
    }
}  // namespace detail

// A part by its name (any case), "overalls" for the pants too; -1 for none.
inline int findPart(const char* name) {
    if (name == nullptr) return -1;
    for (std::uint32_t part = 0; part < PartCount; ++part) {
        if (detail::equal(name, partKey(part))) return static_cast<int>(part);
    }
    return detail::equal(name, "overalls") ? static_cast<int>(Pants) : -1;
}

// The parts a material's texture may hold, by the material's name (the game's player models, and a mod character's
// model named as them): "Cap" the cap and its emblem, "Body" the overalls, shirt and shoes, "Hair" the hair (and the
// ear it shares a texture with), "Face" and "EyeLid" the skin, "Hand" the gloves. Eyes take none.
inline std::uint32_t materialParts(const char* material) {
    using detail::contains;
    if (contains(material, "eyelid")) return bit(Skin);
    if (contains(material, "eye")) return 0u;
    if (contains(material, "cap") || contains(material, "hat")) return bit(Cap) | bit(Emblem);
    if (contains(material, "body")) return bit(Pants) | bit(Shirt) | bit(Shoes);
    if (contains(material, "hair")) return bit(Hair) | bit(Skin);
    if (contains(material, "face") || contains(material, "skin")) return bit(Skin);
    if (contains(material, "hand") || contains(material, "glove")) return bit(Gloves);
    return 0u;
}

// Whether drawing `wanted` over a model whose own colours are `reference` changes any of `parts`.
inline bool changes(std::uint32_t parts, const Palette& reference, const Palette& wanted) {
    for (std::uint32_t part = 0; part < PartCount; ++part) {
        if ((parts & (1u << part)) != 0u && std::memcmp(reference.rgb[part], wanted.rgb[part], 3) != 0) return true;
    }
    return false;
}

// The scratch recolor takes: bytes for a width x height image.
inline std::size_t scratchBytes(std::uint32_t width, std::uint32_t height) {
    return static_cast<std::size_t>(width) * height * 11u;
}

namespace detail {
    constexpr std::uint8_t kNone = 0xffu;
    // How far a texel's hue may be from a part's (squared, its channels over its brightest).
    constexpr float kNear = 0.12f;
    // Darker texels (outlines, the black around the pieces) and near-clear ones are nobody's.
    constexpr std::uint8_t kDarkest = 24u;
    constexpr std::uint8_t kClearest = 8u;
    // A light texel round an emblem: its disc.
    constexpr std::uint8_t kLight = 140u;

    inline std::uint8_t brightest(const std::uint8_t* c) {
        const std::uint8_t a = c[0] > c[1] ? c[0] : c[1];
        return a > c[2] ? a : c[2];
    }

    struct Hue {
        float c[3];
        std::uint8_t value;
    };

    inline Hue hueOf(const std::uint8_t* c) {
        Hue hue{};
        hue.value = brightest(c);
        const float v = hue.value != 0 ? static_cast<float>(hue.value) : 1.0f;
        for (int k = 0; k < 3; ++k) hue.c[k] = static_cast<float>(c[k]) / v;
        return hue;
    }

    inline float distance(const Hue& a, const Hue& b) {
        float d = 0.0f;
        for (int k = 0; k < 3; ++k) d += (a.c[k] - b.c[k]) * (a.c[k] - b.c[k]);
        return d;
    }
}  // namespace detail

// Recolours `rgba` (width x height texels, 4 bytes each, in rows) in place from `reference`'s colours to `wanted`'s,
// for the parts in `parts` (materialParts). `scratch` holds scratchBytes(width, height) bytes.
//
// Each texel near a part's hue (and not too dark) is that part's; in a cap's texture, a patch of the cap's or emblem's
// hue ringed by light texels (the emblem's disc) and touching no edge is the emblem, the rest the cap. A texel next to
// one of a part's that is that part's hue mixed with white (an edge's blend) is recoloured as such a mix. A part's
// texels take its wanted colour at their own lightness against the reference's (brighter ones towards white), and a
// part whose wanted colour is its reference's keeps its texels as they are.
inline void recolor(std::uint8_t* rgba, std::uint32_t width, std::uint32_t height, std::uint32_t parts, const Palette& reference,
                    const Palette& wanted, std::uint8_t* scratch) {
    using namespace detail;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    if (count == 0 || parts == 0u || !changes(parts, reference, wanted)) return;
    std::uint8_t* const kind = scratch;              // each texel's part (kNone: none)
    std::uint8_t* const fringe = scratch + count;    // an unclaimed texel's part as an edge's blend
    std::uint8_t* const mix = scratch + count * 2;   // that blend's white, of 255
    std::uint32_t* const stack = reinterpret_cast<std::uint32_t*>(scratch + count * 3);
    std::uint32_t* const order = stack + count;
    Hue hues[PartCount];
    for (std::uint32_t part = 0; part < PartCount; ++part) hues[part] = hueOf(reference.rgb[part]);

    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* c = rgba + i * 4;
        kind[i] = kNone;
        fringe[i] = kNone;
        mix[i] = 0;
        const Hue hue = hueOf(c);
        if (hue.value < kDarkest || c[3] < kClearest) continue;
        float best = kNear;
        for (std::uint32_t part = 0; part < PartCount; ++part) {
            if ((parts & (1u << part)) == 0u) continue;
            const float d = distance(hue, hues[part]);
            if (d < best) {
                best = d;
                kind[i] = static_cast<std::uint8_t>(part);
            }
        }
    }

    // The emblem: each patch of the cap's and emblem's hues apart from the cap's main one, inside a light ring.
    if ((parts & bit(Cap)) != 0u) {
        const auto group = [&](std::size_t i) { return kind[i] == Cap || kind[i] == Emblem; };
        std::size_t total = 0;
        for (std::size_t i = 0; i < count; ++i) total += group(i) ? 1u : 0u;
        std::uint8_t* const seen = mix;  // 0 until the patches are found (cleared after)
        std::size_t placed = 0;
        for (std::size_t start = 0; start < count; ++start) {
            if (!group(start) || seen[start] != 0u) continue;
            std::size_t top = 0;
            const std::size_t first = placed;
            stack[top++] = static_cast<std::uint32_t>(start);
            seen[start] = 1u;
            bool edge = false;
            std::size_t border = 0, light = 0;
            while (top > 0) {
                const std::uint32_t i = stack[--top];
                order[placed++] = i;
                const std::uint32_t x = i % width, y = i / width;
                const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& step : steps) {
                    const std::int64_t nx = static_cast<std::int64_t>(x) + step[0];
                    const std::int64_t ny = static_cast<std::int64_t>(y) + step[1];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                        edge = true;
                        continue;
                    }
                    const std::uint32_t j = static_cast<std::uint32_t>(ny) * width + static_cast<std::uint32_t>(nx);
                    if (group(j)) {
                        if (seen[j] == 0u) {
                            seen[j] = 1u;
                            stack[top++] = j;
                        }
                    } else {
                        ++border;
                        light += brightest(rgba + static_cast<std::size_t>(j) * 4) >= kLight ? 1u : 0u;
                    }
                }
            }
            const std::size_t size = placed - first;
            const bool emblem = !edge && size * 4 < total && border > 0 && light * 2 >= border;
            for (std::size_t k = first; k < placed; ++k) kind[order[k]] = static_cast<std::uint8_t>(emblem ? Emblem : Cap);
        }
        std::memset(seen, 0, count);
    }

    // Edges: an unclaimed texel next to a part's that is its hue mixed with white.
    for (std::size_t i = 0; i < count; ++i) {
        if (kind[i] != kNone) continue;
        const std::uint8_t* c = rgba + i * 4;
        const Hue hue = hueOf(c);
        if (hue.value < kDarkest || c[3] < kClearest) continue;
        const std::uint32_t x = static_cast<std::uint32_t>(i % width), y = static_cast<std::uint32_t>(i / width);
        float best = kNear;
        const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& step : steps) {
            const std::int64_t nx = static_cast<std::int64_t>(x) + step[0];
            const std::int64_t ny = static_cast<std::int64_t>(y) + step[1];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            const std::uint8_t part = kind[static_cast<std::size_t>(ny) * width + static_cast<std::size_t>(nx)];
            if (part == kNone) continue;
            const Hue& ref = hues[part];
            float towards[3], along = 0.0f, length = 0.0f;
            for (int k = 0; k < 3; ++k) {
                towards[k] = 1.0f - ref.c[k];
                along += (hue.c[k] - ref.c[k]) * towards[k];
                length += towards[k] * towards[k];
            }
            float t = length > 1e-6f ? along / length : 0.0f;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            if (t >= 0.9f) continue;
            float d = 0.0f;
            for (int k = 0; k < 3; ++k) {
                const float off = hue.c[k] - ref.c[k] - t * towards[k];
                d += off * off;
            }
            if (d < best) {
                best = d;
                fringe[i] = part;
                mix[i] = static_cast<std::uint8_t>(t * 255.0f + 0.5f);
            }
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t part = kind[i] != kNone ? kind[i] : fringe[i];
        if (part == kNone || std::memcmp(reference.rgb[part], wanted.rgb[part], 3) == 0) continue;
        std::uint8_t* c = rgba + i * 4;
        const float value = static_cast<float>(brightest(c));
        const float white = kind[i] != kNone ? 0.0f : static_cast<float>(mix[i]) / 255.0f;
        const float own = static_cast<float>(brightest(reference.rgb[part]));
        const float scale = value * (1.0f - white) / (own > 0.0f ? own : 1.0f);
        for (int k = 0; k < 3; ++k) {
            const float target = static_cast<float>(wanted.rgb[part][k]);
            float out = scale <= 1.0f ? target * scale : target + (255.0f - target) * (scale - 1.0f > 1.0f ? 1.0f : scale - 1.0f);
            out += value * white;
            c[k] = static_cast<std::uint8_t>(out >= 255.0f ? 255.0f : out + 0.5f);
        }
    }
}

// A texture record's first level as RGBA8 texels into `rgba` (width x height x 4 bytes): false for a layout it can't
// read.
inline bool decodeFirstLevel(const NebulaTexture& record, std::uint8_t* rgba) {
    const std::uint8_t* in = reinterpret_cast<const std::uint8_t*>(&record) + NEBULA_TEXTURE_HEADER;
    const std::uint32_t width = record.width, height = record.height;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    switch (record.layout) {
    case NEBULA_TEXTURE_RGBA8:
        std::memcpy(rgba, in, count * 4);
        return true;
    case NEBULA_TEXTURE_L8:
        for (std::size_t i = 0; i < count; ++i) std::memset(rgba + i * 4, in[i], 4);
        return true;
    case NEBULA_TEXTURE_LA8:
        for (std::size_t i = 0; i < count; ++i) {
            std::memset(rgba + i * 4, in[i * 2], 3);
            rgba[i * 4 + 3] = in[i * 2 + 1];
        }
        return true;
    case NEBULA_TEXTURE_BLOCK4: {
        const auto widen = [](std::uint16_t v, std::uint8_t* out) {
            const std::uint32_t r = (v >> 11) & 31u, g = (v >> 5) & 63u, b = v & 31u;
            out[0] = static_cast<std::uint8_t>(r << 3 | r >> 2);
            out[1] = static_cast<std::uint8_t>(g << 2 | g >> 4);
            out[2] = static_cast<std::uint8_t>(b << 3 | b >> 2);
            out[3] = 255;
        };
        const std::uint32_t across = (width + 3) / 4, down = (height + 3) / 4;
        for (std::uint32_t by = 0; by < down; ++by) {
            for (std::uint32_t bx = 0; bx < across; ++bx) {
                const std::uint8_t* block = in + (static_cast<std::size_t>(by) * across + bx) * 8;
                const std::uint16_t c0 = static_cast<std::uint16_t>(block[0] | block[1] << 8);
                const std::uint16_t c1 = static_cast<std::uint16_t>(block[2] | block[3] << 8);
                const std::uint32_t indices = static_cast<std::uint32_t>(block[4]) | static_cast<std::uint32_t>(block[5]) << 8 |
                                              static_cast<std::uint32_t>(block[6]) << 16 | static_cast<std::uint32_t>(block[7]) << 24;
                std::uint8_t colors[4][4];
                widen(c0, colors[0]);
                widen(c1, colors[1]);
                for (int k = 0; k < 3; ++k) {
                    if (c0 > c1) {
                        colors[2][k] = static_cast<std::uint8_t>((5 * colors[0][k] + 3 * colors[1][k]) / 8);
                        colors[3][k] = static_cast<std::uint8_t>((3 * colors[0][k] + 5 * colors[1][k]) / 8);
                    } else {
                        colors[2][k] = static_cast<std::uint8_t>((colors[0][k] + colors[1][k]) / 2);
                        colors[3][k] = colors[2][k];
                    }
                }
                colors[2][3] = 255;
                colors[3][3] = c0 > c1 ? 255 : 0;
                for (std::uint32_t y = 0; y < 4; ++y) {
                    for (std::uint32_t x = 0; x < 4; ++x) {
                        const std::uint32_t px = bx * 4 + x, py = by * 4 + y;
                        if (px >= width || py >= height) continue;
                        std::memcpy(rgba + (static_cast<std::size_t>(py) * width + px) * 4, colors[(indices >> (2 * (4 * y + x))) & 3u], 4);
                    }
                }
            }
        }
        return true;
    }
    default:
        return false;
    }
}

// The bytes of a recoloured copy of `source` (recolorRecord's record, header included).
inline std::size_t recordBytes(const NebulaTexture& source) {
    const std::uint32_t levels = source.levels != 0 ? source.levels : 1u;
    return NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(source.width, source.height, levels, NEBULA_TEXTURE_RGBA8);
}

// Writes into `out` (recordBytes(source) bytes) a record of `source` recoloured (recolor), RGBA8, with as many levels
// as it has: the first recoloured, each next one its last halved (2 x 2 texels averaged). `scratch` holds
// scratchBytes(source.width, source.height). False when `source`'s texels can't be read (nothing written).
inline bool recolorRecord(const NebulaTexture& source, std::uint8_t* out, std::uint32_t parts, const Palette& reference,
                          const Palette& wanted, std::uint8_t* scratch) {
    const std::uint32_t levels = source.levels != 0 ? source.levels : 1u;
    std::uint8_t* texels = out + NEBULA_TEXTURE_HEADER;
    if (!decodeFirstLevel(source, texels)) return false;
    nebula_texture_init(out, source.width, source.height, levels, NEBULA_TEXTURE_RGBA8,
                        static_cast<std::uint8_t>(source.flags & NEBULA_TEXTURE_GENERATED_LEVELS));
    recolor(texels, source.width, source.height, parts, reference, wanted, scratch);
    std::uint32_t width = source.width, height = source.height;
    for (std::uint32_t level = 1; level < levels; ++level) {
        const std::uint32_t nextWidth = width > 1 ? width / 2 : 1, nextHeight = height > 1 ? height / 2 : 1;
        std::uint8_t* next = texels + static_cast<std::size_t>(width) * height * 4;
        for (std::uint32_t y = 0; y < nextHeight; ++y) {
            for (std::uint32_t x = 0; x < nextWidth; ++x) {
                const std::uint32_t x0 = x * 2 < width ? x * 2 : width - 1, x1 = x * 2 + 1 < width ? x * 2 + 1 : x0;
                const std::uint32_t y0 = y * 2 < height ? y * 2 : height - 1, y1 = y * 2 + 1 < height ? y * 2 + 1 : y0;
                for (std::size_t k = 0; k < 4; ++k) {
                    const std::uint32_t sum = texels[(static_cast<std::size_t>(y0) * width + x0) * 4 + k] +
                                              texels[(static_cast<std::size_t>(y0) * width + x1) * 4 + k] +
                                              texels[(static_cast<std::size_t>(y1) * width + x0) * 4 + k] +
                                              texels[(static_cast<std::size_t>(y1) * width + x1) * 4 + k];
                    next[(static_cast<std::size_t>(y) * nextWidth + x) * 4 + k] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
        texels = next;
        width = nextWidth;
        height = nextHeight;
    }
    return true;
}

// "rrggbb" (any case, an optional '#') as a colour, or -1.
inline std::int32_t parseColor(const char* text) {
    if (text == nullptr) return -1;
    if (*text == '#') ++text;
    std::int32_t value = 0;
    int digits = 0;
    for (; *text != '\0'; ++text, ++digits) {
        const char c = detail::lower(*text);
        const int digit = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
        if (digit < 0 || digits >= 6) return -1;
        value = value * 16 + digit;
    }
    return digits == 6 ? value : -1;
}

}  // namespace nebula_palette
