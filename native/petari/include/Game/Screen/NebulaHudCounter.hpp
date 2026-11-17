#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CountUpPaneRumbler;
class CounterLayoutAppearer;
struct NebulaHudCounterTint;

// A mod's counter on Galaxy's HUD (nebula_mod_api.h hud_counter): the save's star counter (StarCounter, its own
// layout) again under it, on one of NEBULA_HUD_COUNTER_LINES lines, its star tinted the mod's colour. The built-in
// Personal Star Counter mod (sm64coopdx's) puts its red and green counts here. CounterLayoutController shows and
// hides the lines with the save's counter, so they come in and out as it does (when the player stands still) and
// count up as it does. A line no mod sets stays hidden.
class NebulaHudCounter : public LayoutActor {
public:
    NebulaHudCounter(s32 line);

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void control();
    virtual void calcAnim();

    void disappear();
    void exeAppear();
    void exeWait();
    void exeCountUp();
    void exeDisappear();

    // hud_counter: a line's colour (0xRRGGBBAA) and number, below 0 for none. False for no such line.
    static bool setLine(s32 line, u32 rgba, s32 number);

private:
    // What the line shows now; below 0 when no mod sets it (the counter stays hidden).
    s32 getCount() const;
    // The screen's units from one counter to the next below: the layout's tallest part.
    f32 getLineStep() const;

    /* 0x20 */ s32 mLine;
    /* 0x24 */ s32 mCount;
    /* 0x28 */ u32 mColor;
    /* 0x2C */ CounterLayoutAppearer* mLayoutAppearer;
    /* 0x30 */ CountUpPaneRumbler* mPaneRumbler;
    /* 0x34 */ NebulaHudCounterTint* mTint;
};
