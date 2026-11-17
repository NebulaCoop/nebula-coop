#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include "nebula_mod_api.h"

class CoinCounter;
class MarioMeter;
class NebulaHudCounter;
class PlayerLeft;
class StarCounter;
class StarPieceCounter;

class CounterLayoutController : public LayoutActor {
public:
    /// @brief Creates a new `CounterLayoutController`.
    CounterLayoutController();

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

    void forceAppear();
    void setHPMeter(MarioMeter*);
    void requestedTicoEat(bool);
    static bool isInvalidSystemStateShowCounter();
    static bool isWaitToDisappearCounter(const LayoutActor*);
    void showAllLayout();
    void hideAllLayout();
    bool isHiddenOrWaitAllLayout() const;
    bool isPlayerMoving() const;
    void appearAllCounterWithoutStar();
    void killAllCoounter();
    bool tryEndTicoEatStarPiece();
    void exePlayerMoving();
    void exePlayerNotMoving();

    s32 mPlayerNotMovingFrame;
    bool mIsTicoEatRequested;
    CoinCounter* mCoinCounter;
    StarPieceCounter* mStarPieceCounter;
    PlayerLeft* mPlayerLeft;
    StarCounter* mStarCounter;
    MarioMeter* mHPMeter;
    // The mods' counters under the save's star counter (hud_counter; the Personal Star Counter's): shown and hidden
    // with it.
    NebulaHudCounter* mHudCounters[NEBULA_HUD_COUNTER_LINES];
};
