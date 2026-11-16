// Tests for nebula_palette.hpp: the player colours' parts, names and texture recolouring (sm64coopdx's palettes).
#include <nebula_palette.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

using nebula_palette::Palette;

void put(std::vector<std::uint8_t>& rgba, std::uint32_t width, std::uint32_t x, std::uint32_t y, std::uint32_t rgb) {
    std::uint8_t* texel = &rgba[(static_cast<std::size_t>(y) * width + x) * 4u];
    texel[0] = static_cast<std::uint8_t>(rgb >> 16);
    texel[1] = static_cast<std::uint8_t>(rgb >> 8);
    texel[2] = static_cast<std::uint8_t>(rgb);
    texel[3] = 0xffu;
}

std::uint32_t at(const std::vector<std::uint8_t>& rgba, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
    const std::uint8_t* texel = &rgba[(static_cast<std::size_t>(y) * width + x) * 4u];
    return static_cast<std::uint32_t>(texel[0]) << 16 | static_cast<std::uint32_t>(texel[1]) << 8 | texel[2];
}

// Whether `rgb` leans to `channel` (0 red, 1 green, 2 blue) over the other two.
bool leans(std::uint32_t rgb, int channel) {
    const int c[3] = {static_cast<int>(rgb >> 16 & 0xffu), static_cast<int>(rgb >> 8 & 0xffu), static_cast<int>(rgb & 0xffu)};
    return c[channel] > c[(channel + 1) % 3] + 40 && c[channel] > c[(channel + 2) % 3] + 40;
}

}  // namespace

int main() {
    using namespace nebula_palette;
    bool ok = true;

    ok &= expect(parseColor("ff8000") == 0xff8000 && parseColor("#A0b0C0") == 0xa0b0c0, "colours parse");
    ok &= expect(parseColor("") < 0 && parseColor("fff") < 0 && parseColor("ff80000") < 0 && parseColor("gg0000") < 0 &&
                     parseColor(nullptr) < 0,
                 "bad colours are refused");
    ok &= expect(findPart("CAP") == Cap && findPart("overalls") == Pants && findPart("emblem") == Emblem &&
                     findPart("hat") < 0,
                 "parts by name");
    ok &= expect(materialParts("mario_cap_v_x") == (bit(Cap) | bit(Emblem)) &&
                     materialParts("MarioBody") == (bit(Pants) | bit(Shirt) | bit(Shoes)) &&
                     materialParts("mario_eyeLid") == bit(Skin) && materialParts("mario_eye") == 0u &&
                     materialParts("MarioHand") == bit(Gloves) && materialParts("MarioHair") == (bit(Hair) | bit(Skin)),
                 "a material's parts by its name");

    Palette palette = cMario;
    setColor(palette, Cap, 0x00ff00);
    ok &= expect(color(palette, Cap) == 0x00ff00u && color(cMario, Shirt) == 0xdc0000u && color(palette, PartCount) == 0u,
                 "a part's colour");
    ok &= expect(changes(bit(Cap), cMario, palette) && !changes(bit(Shirt) | bit(Pants), cMario, palette) && !same(cMario, palette),
                 "what a change touches");

    // A cap: its red all over, a white disc in the middle holding the emblem's red, a black outline in a corner.
    constexpr std::uint32_t size = 32u;
    std::vector<std::uint8_t> cap(size * size * 4u);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const int dx = static_cast<int>(x) - 16;
            const int dy = static_cast<int>(y) - 16;
            const int r2 = dx * dx + dy * dy;
            put(cap, size, x, y, r2 <= 9 ? 0xc40000u : r2 <= 36 ? 0xffffffu : 0xb40000u);
        }
    }
    put(cap, size, 0, 0, 0x000000u);
    std::vector<std::uint8_t> scratch(scratchBytes(size, size));

    std::vector<std::uint8_t> same_cap = cap;
    recolor(same_cap.data(), size, size, bit(Cap) | bit(Emblem), cMario, cMario, scratch.data());
    ok &= expect(same_cap == cap, "a texture in its own colours stays as it is");

    Palette wanted = cMario;
    setColor(wanted, Cap, 0x00b400);
    setColor(wanted, Emblem, 0x0000c4);
    std::vector<std::uint8_t> recoloured = cap;
    recolor(recoloured.data(), size, size, bit(Cap) | bit(Emblem), cMario, wanted, scratch.data());
    ok &= expect(leans(at(recoloured, size, 4, 20), 1) && leans(at(recoloured, size, 30, 2), 1), "the cap takes its colour");
    ok &= expect(leans(at(recoloured, size, 16, 16), 2), "the emblem inside its disc takes its own");
    ok &= expect(at(recoloured, size, 16, 11) == 0xffffffu && at(recoloured, size, 0, 0) == 0u,
                 "the disc's white and the outline stay");

    std::vector<std::uint8_t> cap_only = cap;
    Palette cap_wanted = cMario;
    setColor(cap_wanted, Cap, 0x00b400);
    recolor(cap_only.data(), size, size, bit(Cap) | bit(Emblem), cMario, cap_wanted, scratch.data());
    ok &= expect(leans(at(cap_only, size, 4, 20), 1) && at(cap_only, size, 16, 16) == 0xc40000u,
                 "an emblem kept at its own colour stays as it is under a new cap");

    // The shirt's red in a body texture isn't the cap's: only the parts a material holds change.
    std::vector<std::uint8_t> body(size * size * 4u);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) put(body, size, x, y, y < 16u ? 0xdc0000u : 0x083084u);
    }
    std::vector<std::uint8_t> body_cap = body;
    recolor(body_cap.data(), size, size, bit(Pants) | bit(Shirt) | bit(Shoes), cMario, cap_wanted, scratch.data());
    ok &= expect(body_cap == body, "a body texture ignores the cap's colour");
    Palette blue_shirt = cMario;
    setColor(blue_shirt, Shirt, 0x0000dc);
    setColor(blue_shirt, Pants, 0x840808);
    recolor(body.data(), size, size, bit(Pants) | bit(Shirt) | bit(Shoes), cMario, blue_shirt, scratch.data());
    ok &= expect(leans(at(body, size, 8, 4), 2) && leans(at(body, size, 8, 24), 0), "shirt and overalls take theirs");

    if (!ok) return EXIT_FAILURE;
    std::cout << "Palette tests passed\n";
    return EXIT_SUCCESS;
}
