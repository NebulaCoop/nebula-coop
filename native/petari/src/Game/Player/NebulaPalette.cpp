#include "Game/Player/NebulaPalette.hpp"
#include "Game/Player/NebulaCharacter.hpp"
#include "Game/Player/NebulaCharacters.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaHost.hpp"
#include <nebula_cooked_texture.h>
#include <nebula_game_api.h>
#include "nebula_module.h"
#include <cstring>

namespace {
    // A mod's colours over the player's (set_player_palette_color), part by part; -1 for none.
    s32 sOverride[nebula_palette::PartCount] = {-1, -1, -1, -1, -1, -1, -1, -1};

    // `pName` without its last ".n" ("mario_eyeLid.2" -> "mario_eyeLid"), the length of that.
    u32 stemLength(const char* pName) {
        const char* pDot = std::strrchr(pName, '.');
        return pDot != nullptr ? static_cast< u32 >(pDot - pName) : static_cast< u32 >(std::strlen(pName));
    }
};  // namespace

namespace NebulaPalette {
    nebula_palette::Palette chosen() {
        nebula_palette::Palette palette;
        if (NebulaHost::coopPaletteWanted(&palette.rgb[0][0])) {
            return palette;
        }
        return NebulaCharacters::palette(NebulaCharacter::index());
    }

    nebula_palette::Palette local() {
        nebula_palette::Palette palette = chosen();
        for (u32 part = 0; part < nebula_palette::PartCount; part++) {
            if (sOverride[part] >= 0) {
                nebula_palette::setColor(palette, part, static_cast< u32 >(sOverride[part]));
            }
        }
        return palette;
    }

    bool setOverride(u32 part, s32 rgb) {
        if (part >= nebula_palette::PartCount) {
            return false;
        }
        sOverride[part] = rgb < 0 ? -1 : (rgb & 0xFFFFFF);
        return true;
    }

    void resetOverride() {
        for (u32 part = 0; part < nebula_palette::PartCount; part++) {
            sOverride[part] = -1;
        }
    }

    s32 getOverride(u32 part) {
        return part < nebula_palette::PartCount ? sOverride[part] : -1;
    }

    nebula_palette::Palette ofRemote(const NebulaCoopPlayer& rPlayer, s32 character) {
        if (rPlayer.has_palette == 0) {
            return NebulaCharacters::palette(character);
        }
        nebula_palette::Palette palette;
        memcpy(&palette, rPlayer.palette, sizeof(palette));
        return palette;
    }
};  // namespace NebulaPalette

NebulaPaletteSkin::NebulaPaletteSkin() : mCount(0), mScratch(nullptr), mScratchSize(0), mSet(false) {
    mOwn = nebula_palette::cMario;
    mWanted = nebula_palette::cMario;
}

void NebulaPaletteSkin::addModel(const ModelData* pData) {
    if (pData == nullptr) {
        return;
    }
    const u32 textures = pData->getTextureCount();
    for (u32 i = 0; i < pData->getMaterialCount(); i++) {
        const char* pName = pData->getMaterialName(i);
        const u32 parts = nebula_palette::materialParts(pName);
        const u32 texture = parts != 0 ? pData->getMaterialAt(i)->getTextureIndex(0) : NEBULA_NONE32;
        if (texture >= textures) {
            continue;
        }
        add(pData->getTexture(texture)->image(), parts);
        // The eyelids blink through their texture's frames (MarioActor::swapTexture): every frame, all skin.
        if (nebula_palette::detail::contains(pName, "eyelid")) {
            const char* pTexture = pData->getTextureName(texture);
            const u32 length = stemLength(pTexture);
            for (u32 j = 0; j < textures; j++) {
                const char* pOther = pData->getTextureName(j);
                if (j != texture && stemLength(pOther) == length && std::strncmp(pOther, pTexture, length) == 0) {
                    add(pData->getTexture(j)->image(), parts);
                }
            }
        }
    }
}

void NebulaPaletteSkin::add(const void* pSource, u32 parts) {
    const NebulaTexture* pRecord = nebula_texture_record(pSource);
    if (pRecord == nullptr || pRecord->width == 0 || pRecord->height == 0) {
        return;
    }
    for (u32 i = 0; i < mCount; i++) {
        if (mEntries[i].mSource == pSource) {
            mEntries[i].mParts |= parts;
            return;
        }
    }
    if (mCount >= cMaxEntries) {
        return;
    }
    // The skin's memory (its actor's arena, the scene's): the renderer lets the copies go with it.
    NebulaArena* pArena = NebulaArena::findFromRoot(this);
    const u32 size = static_cast< u32 >(nebula_palette::recordBytes(*pRecord));
    u8* pCopy = static_cast< u8* >(NebulaArena::alloc(size, 32, pArena));
    if (pCopy == nullptr) {
        return;
    }
    const u32 scratch = static_cast< u32 >(nebula_palette::scratchBytes(pRecord->width, pRecord->height));
    if (scratch > mScratchSize) {
        u8* pScratch = static_cast< u8* >(NebulaArena::alloc(scratch, 4, pArena));
        if (pScratch == nullptr) {
            NebulaArena::free(pCopy, pArena);
            return;
        }
        if (mScratch != nullptr) {
            NebulaArena::free(mScratch, pArena);
        }
        mScratch = pScratch;
        mScratchSize = scratch;
    }
    Entry& rEntry = mEntries[mCount++];
    rEntry.mSource = pSource;
    rEntry.mParts = parts;
    rEntry.mRecord = pCopy;
    rEntry.mSize = size;
    rEntry.mChanged = false;
    if (mSet) {
        build(rEntry);
    }
}

void NebulaPaletteSkin::build(Entry& rEntry) {
    rEntry.mChanged = false;
    if (!nebula_palette::changes(rEntry.mParts, mOwn, mWanted)) {
        return;
    }
    const NebulaTexture* pRecord = nebula_texture_record(rEntry.mSource);
    if (pRecord == nullptr || !nebula_palette::recolorRecord(*pRecord, rEntry.mRecord, rEntry.mParts, mOwn, mWanted, mScratch)) {
        return;
    }
    RenderTexture::rewritten(rEntry.mRecord, rEntry.mSize);
    rEntry.mChanged = true;
}

void NebulaPaletteSkin::setColors(const nebula_palette::Palette& rOwn, const nebula_palette::Palette& rWanted) {
    if (mSet && nebula_palette::same(rOwn, mOwn) && nebula_palette::same(rWanted, mWanted)) {
        return;
    }
    mOwn = rOwn;
    mWanted = rWanted;
    mSet = true;
    for (u32 i = 0; i < mCount; i++) {
        build(mEntries[i]);
    }
}

const void* NebulaPaletteSkin::find(const void* pSource) const {
    if (pSource == nullptr) {
        return nullptr;
    }
    for (u32 i = 0; i < mCount; i++) {
        if (mEntries[i].mSource == pSource) {
            return mEntries[i].mChanged ? mEntries[i].mRecord : nullptr;
        }
    }
    return nullptr;
}
