#pragma once

#include <nebula_palette.hpp>
#include <nebula_types.h>

class ModelData;
struct NebulaCoopPlayer;

// The player's colours, as sm64coopdx's palettes (player_palette.h, network_player's palette and overridePalette):
// eight parts (nebula_palette::Part), each a colour. A player wears the Options page's colours (its PLAYER page), else
// their character's own (NebulaCharacters::palette), and a mod may change any part over them (set_player_palette_color,
// as sm64coopdx's network_player_set_override_palette_color). Online, each player's travels with their character
// (NebulaCoopPlayer::palette), so the others' copies wear them.
namespace NebulaPalette {
    // The colours the player wears now: the Options page's or their character's own, with a mod's parts over them.
    nebula_palette::Palette local();
    // The ones the player picked (the Options page's, else their character's own), with no mod's part.
    nebula_palette::Palette chosen();
    // A mod's colour for a part over the player's (0xRRGGBB); a negative colour takes it off. False for no such part.
    bool setOverride(u32 part, s32 rgb);
    // Every part back to the player's own.
    void resetOverride();
    // A part a mod set (its colour), or -1 for none.
    s32 getOverride(u32 part);
    // The colours another player wears, as the session last told them: their character's own (`character`, an index,
    // the copy's) until it says.
    nebula_palette::Palette ofRemote(const NebulaCoopPlayer& rPlayer, s32 character);
};  // namespace NebulaPalette

// A player model's textures in a player's colours: copies of the textures its materials draw with that hold parts
// (nebula_palette::materialParts, by the material's name), recoloured from the model's own colours (its character's) to
// the player's. PlayerModel::drawIn draws a copy in place of its texture. Made where the actor is made (the scene's
// memory), remade in place when the colours change; one shared by a model's face, hands and body.
class NebulaPaletteSkin {
public:
    NebulaPaletteSkin();

    // `pData`'s textures that hold parts: copies are made for them. Called as the actor is made.
    void addModel(const ModelData* pData);
    // The colours: `rOwn` the model's own (its character's), `rWanted` the player's. The copies follow at once when they
    // change; nothing is done when they don't.
    void setColors(const nebula_palette::Palette& rOwn, const nebula_palette::Palette& rWanted);
    // The copy to draw in place of the image `pSource` (a texture record), or null to draw it as it is.
    const void* find(const void* pSource) const;

private:
    struct Entry {
        const void* mSource;  // the texture's record
        u32 mParts;           // the parts its materials hold
        u8* mRecord;          // its copy
        u32 mSize;
        bool mChanged;        // the copy differs: drawn in its place
    };
    enum { cMaxEntries = 40 };

    void add(const void* pSource, u32 parts);
    void build(Entry& rEntry);

    Entry mEntries[cMaxEntries];
    u32 mCount;
    u8* mScratch;
    u32 mScratchSize;
    nebula_palette::Palette mOwn;
    nebula_palette::Palette mWanted;
    bool mSet;
};
