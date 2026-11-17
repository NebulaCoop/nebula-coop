#include "Game/Screen/NebulaHudCounter.hpp"
#include "nebula_mod_api.h"

#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/CountUpPaneRumbler.hpp"
#include "Game/Screen/CounterLayoutAppearer.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <cstring>
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Pane.hpp>
#include <Game/Ui/Picture.hpp>
#include <Game/Ui/TextBox.hpp>

namespace {
    const s32 cTintedMax = 16;
    const s32 cCountMax = 99999;  // the widest number the layout's text box holds
    const f32 cLineStepFallback = 34.0f;

    // What the mods set (hud_counter), by line from 1.
    struct Line {
        u32 mColor;
        s32 mNumber;
    };
    Line sLines[NEBULA_HUD_COUNTER_LINES] = {
        {0xFFFFFFFF, -1},
        {0xFFFFFFFF, -1},
        {0xFFFFFFFF, -1},
        {0xFFFFFFFF, -1},
    };
};  // namespace

namespace NrvNebulaHudCounter {
    NEW_NERVE(NebulaHudCounterNrvAppear, NebulaHudCounter, Appear);
    NEW_NERVE(NebulaHudCounterNrvWait, NebulaHudCounter, Wait);
    NEW_NERVE(NebulaHudCounterNrvCountUp, NebulaHudCounter, CountUp);
    NEW_NERVE(NebulaHudCounterNrvDisappear, NebulaHudCounter, Disappear);
};  // namespace NrvNebulaHudCounter

// The layout's star (its pictures and any text but the number) in the counter's colour. The layout's animations may
// set those colours again (its Flash): each one they write is tinted once more after them.
struct NebulaHudCounterTint {
    struct Entry {
        Ui::Pane* mPane;
        bool mText;
        Color8 mOriginal[4];
        Color8 mWritten[4];
    };
    Entry mEntries[cTintedMax];
    s32 mNum;
    Color8 mTint;

    Color8 tinted(Color8 color) const {
        Color8 out = color;
        out.r = static_cast< u8 >(color.r * mTint.r / 255);
        out.g = static_cast< u8 >(color.g * mTint.g / 255);
        out.b = static_cast< u8 >(color.b * mTint.b / 255);
        return out;
    }

    void collect(Ui::Pane* pPane) {
        // The number keeps its colour.
        if (pPane == nullptr || strcmp(pPane->mName, "Counter") == 0) {
            return;
        }
        const bool text = Ui::PaneCast< Ui::TextBox >(pPane) != nullptr;
        const bool picture = Ui::PaneCast< Ui::Picture >(pPane) != nullptr;
        if ((text || picture) && mNum < cTintedMax) {
            Entry& rEntry = mEntries[mNum++];
            rEntry.mPane = pPane;
            rEntry.mText = text;
            for (s32 i = 0; i < 4; i++) {
                rEntry.mOriginal[i] = text ? (i < 2 ? static_cast< Ui::TextBox* >(pPane)->GetTextColor(i) : Color8(0xFFFFFFFF))
                                           : static_cast< Ui::Picture* >(pPane)->GetVtxColor(i);
                // Never a colour the pane has: the first apply() tints them all.
                rEntry.mWritten[i] = 0x00000001;
            }
        }
        for (Ui::Pane* it : pPane->GetChildList()) {
            collect(it);
        }
    }

    // Another colour: each pane's own colours back, tinted again by the next apply().
    void retint(Color8 tint) {
        mTint = tint;
        for (s32 i = 0; i < mNum; i++) {
            Entry& rEntry = mEntries[i];
            for (u32 c = 0; c < 4; c++) {
                if (rEntry.mText) {
                    if (c < 2) {
                        static_cast< Ui::TextBox* >(rEntry.mPane)->SetTextColor(c, rEntry.mOriginal[c]);
                    }
                } else {
                    static_cast< Ui::Picture* >(rEntry.mPane)->SetVtxColor(c, rEntry.mOriginal[c]);
                }
                rEntry.mWritten[c] = 0x00000001;
            }
        }
    }

    void apply() {
        for (s32 i = 0; i < mNum; i++) {
            Entry& rEntry = mEntries[i];
            if (rEntry.mText) {
                Ui::TextBox* pText = static_cast< Ui::TextBox* >(rEntry.mPane);
                for (u32 c = 0; c < 2; c++) {
                    const Color8 now = pText->GetTextColor(c);
                    if (static_cast< u32 >(now) != static_cast< u32 >(rEntry.mWritten[c])) {
                        rEntry.mWritten[c] = tinted(now);
                        pText->SetTextColor(c, rEntry.mWritten[c]);
                    }
                }
            } else {
                Ui::Picture* pPicture = static_cast< Ui::Picture* >(rEntry.mPane);
                for (u32 c = 0; c < 4; c++) {
                    const Color8 now = pPicture->GetVtxColor(c);
                    if (static_cast< u32 >(now) != static_cast< u32 >(rEntry.mWritten[c])) {
                        rEntry.mWritten[c] = tinted(now);
                        pPicture->SetVtxColor(c, rEntry.mWritten[c]);
                    }
                }
            }
        }
    }
};

bool NebulaHudCounter::setLine(s32 line, u32 rgba, s32 number) {
    if (line < 1 || line > NEBULA_HUD_COUNTER_LINES) {
        return false;
    }
    Line& rLine = sLines[line - 1];
    rLine.mColor = rgba;
    rLine.mNumber = number < 0 ? -1 : (number > cCountMax ? cCountMax : number);
    return true;
}

NebulaHudCounter::NebulaHudCounter(s32 line)
    : LayoutActor("NebulaHudCounter", true), mLine(line), mCount(0), mColor(0xFFFFFFFF), mLayoutAppearer(nullptr),
      mPaneRumbler(nullptr), mTint(nullptr) {
}

void NebulaHudCounter::init(const TableIter& rIter) {
    // The save's own counter, with room in its number for a long count.
    initLayoutManagerWithTextBoxBufferLength("StarCounter", 2, 8);
    MR::createAndAddPaneCtrl(this, "Counter", 1);

    mLayoutAppearer = new CounterLayoutAppearer(this, Vec2f(-50.0f, 0.0f));
    mPaneRumbler = new CountUpPaneRumbler(this, "Counter");

    mColor = sLines[mLine - 1].mColor;
    mTint = new NebulaHudCounterTint();
    mTint->mNum = 0;
    mTint->mTint = mColor;
    mTint->collect(getLayoutManager()->mLayout->mpRootPane);

    MR::connectToSceneLayout(this);
    initNerve(GET_NERVE(NebulaHudCounter, NebulaHudCounterNrvAppear));
}

s32 NebulaHudCounter::getCount() const {
    return sLines[mLine - 1].mNumber;
}

f32 NebulaHudCounter::getLineStep() const {
    // The tallest picture or text of the layout (the star, or the number), from layout units to the screen's.
    f32 tallest = 0.0f;
    for (s32 i = 0; i < mTint->mNum; i++) {
        const Ui::Pane* pPane = mTint->mEntries[i].mPane;
        const f32 height = pPane->mSize.height * pPane->mScale.y;
        tallest = height > tallest ? height : tallest;
    }
    const Ui::Pane* pCounter = getLayoutManager()->mLayout->mpRootPane->FindPaneByName("Counter", true);
    if (pCounter != nullptr && pCounter->mSize.height * pCounter->mScale.y > tallest) {
        tallest = pCounter->mSize.height * pCounter->mScale.y;
    }
    if (tallest <= 0.0f) {
        return cLineStepFallback;
    }
    Vec2f top, bottom;
    MR::convertLayoutPosToScreenPos(&top, Vec2f(0.0f, 0.0f));
    MR::convertLayoutPosToScreenPos(&bottom, Vec2f(0.0f, -tallest));
    const f32 step = bottom.y - top.y;
    return step > 1.0f ? step : cLineStepFallback;
}

void NebulaHudCounter::appear() {
    mCount = getCount();
    if (mCount < 0) {
        // No mod sets this line.
        kill();
        return;
    }
    if (mColor != sLines[mLine - 1].mColor) {
        mColor = sLines[mLine - 1].mColor;
        mTint->retint(mColor);
    }
    mLayoutAppearer->appear(Vec2f(0.0f, getLineStep() * mLine));
    mPaneRumbler->reset();
    MR::startAnim(this, "Wait", 1);

    MR::setTextBoxNumberRecursive(this, "Counter", mCount);
    setNerve(GET_NERVE(NebulaHudCounter, NebulaHudCounterNrvAppear));
    LayoutActor::appear();
    MR::requestMovementOn(this);
}

void NebulaHudCounter::disappear() {
    if (MR::isDead(this)) {
        return;
    }
    MR::requestMovementOn(this);
    setNerve(GET_NERVE(NebulaHudCounter, NebulaHudCounterNrvDisappear));
}

void NebulaHudCounter::control() {
    mLayoutAppearer->update();
    mPaneRumbler->update();
}

void NebulaHudCounter::calcAnim() {
    LayoutActor::calcAnim();
    mTint->apply();
}

void NebulaHudCounter::exeAppear() {
    if (mLayoutAppearer->isAppeared()) {
        setNerve(GET_NERVE(NebulaHudCounter, NebulaHudCounterNrvWait));
    }
}

void NebulaHudCounter::exeWait() {
    const s32 count = getCount();
    if (count < 0) {
        // Removed while shown: out at once rather than with the save's counter.
        disappear();
        return;
    }
    if (mColor != sLines[mLine - 1].mColor) {
        // Another count on this line now: shown as it is.
        mColor = sLines[mLine - 1].mColor;
        mTint->retint(mColor);
        mCount = count;
        MR::setTextBoxNumberRecursive(this, "Counter", mCount);
    } else if (count < mCount) {
        mCount = count;
        MR::setTextBoxNumberRecursive(this, "Counter", mCount);
    } else if (count > mCount) {
        setNerve(GET_NERVE(NebulaHudCounter, NebulaHudCounterNrvCountUp));
    }
}

void NebulaHudCounter::exeCountUp() {
    if (MR::isFirstStep(this)) {
        mCount++;

        MR::setTextBoxNumberRecursive(this, "Counter", mCount);
        MR::startAnim(this, "Flash", 0);
        mPaneRumbler->start();
    }

    if (MR::isAnimStopped(this, 0) && !mPaneRumbler->isRumbling()) {
        setNerve(GET_NERVE(NebulaHudCounter, NebulaHudCounterNrvWait));
    }
}

void NebulaHudCounter::exeDisappear() {
    if (MR::isFirstStep(this)) {
        mLayoutAppearer->disappear();
    }

    if (mLayoutAppearer->isDisappeared()) {
        kill();
    }
}
