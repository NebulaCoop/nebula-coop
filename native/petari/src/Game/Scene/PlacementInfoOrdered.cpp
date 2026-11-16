#include <nebula_game_api.h>
extern "C" const NebulaHostServices* nebula_host(void);
#include "Game/System/NebulaLoadTrace.hpp"
#include "Game/System/NebulaMods.hpp"
<<<<<<< HEAD
#include "Game/System/NebulaCoopWorld.hpp"
#include "Game/System/LumaClass.hpp"
=======
#include "Game/System/LumaClasses.hpp"
#include "Game/System/LumaRefs.hpp"
>>>>>>> upstream/main
#include "Game/Scene/PlacementInfoOrdered.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/Map/PlanetMapCreator.hpp"
#include "Game/NameObj/ModelChangableObjFactory.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/System/NebulaCoopWorld.hpp"

namespace {
    CreationFuncPtr getCreator(const PlacementInfoOrdered::Identifier& rIdentifier) {
        if (rIdentifier.mShapeId != -1) {
            return MR::getModelChangableObjCreator(rIdentifier.mName);
        }

        if (const CreationFuncPtr creator = NameObjFactory::getCreator(rIdentifier.mName)) {
            return creator;
        }
        // A class a mod places that no mod made (references.md 3, after start): the placeholder object stands there.
        return luma::refs::placeholderFor(rIdentifier.mName);
    }

    PlacementInfoOrdered::Identifier readIdentifier(const TableIter& rIter) {
        const char* pName = "";
        MR::getObjectName(&pName, rIter);
        s32 shapeId = -1;
        MR::getPlacementShapeIdWithInit(rIter, &shapeId);
        return PlacementInfoOrdered::Identifier(pName, shapeId);
    }

    bool isAfterScenario(const PlacementInfoOrdered::Identifier& rIdentifier) {
        return rIdentifier.mShapeId == -1 && PlanetMapCreatorFunction::isLoadArchiveAfterScenarioSelected(rIdentifier.mName);
    }
};  // namespace

s32 PlacementInfoOrdered::countAfterScenario(const Table* pInfo) {
    s32 count = 0;
    for (s32 i = 0; i < pInfo->getNumEntries(); i++) {
        if (::isAfterScenario(::readIdentifier(TableIter(pInfo, i)))) {
            count++;
        }
    }

    return count;
}

PlacementInfoOrdered::PlacementInfoOrdered(int count) : mIndexArray(), mIndexCount(), mSetArray(), mOrderedSetArray(), mCount(count) {
    mIndexArray = new Index[count];
    mSetArray = new SameIdSet[count];
    mOrderedSetArray = new SameIdSet*[count];
}

void PlacementInfoOrdered::sort() {
    const s32 count = mSetNum;
    for (s32 i = 0; i < count; i++) {
        // Player archive loaders first, then every other set: the order of a restart, when all of a stage's files are already loaded.
        SameIdSet* pSet = mOrderedSetArray[i];
        pSet->mPriority = NameObjFactory::isPlayerArchiveLoaderObj(pSet->mName) ? 0 : 1;
    }

    s32 gap = 13;
    while (gap < count) {
        gap = gap * 3 + 1;
    }

    for (gap /= 9; gap > 0; gap /= 3) {
        for (s32 i = gap; i < count; i++) {
            SameIdSet* pSet = mOrderedSetArray[i];
            s32 j = i - gap;
            while (j >= 0 && *pSet < *mOrderedSetArray[j]) {
                mOrderedSetArray[j + gap] = mOrderedSetArray[j];
                j -= gap;
            }

            mOrderedSetArray[j + gap] = pSet;
        }
    }

    for (s32 i = 0; i < count; i++) {
        NebulaLoadTrace::sorted(mOrderedSetArray[i]->mName, mOrderedSetArray[i]->mPriority, mOrderedSetArray[i]->mList.getNum());
    }
}

void PlacementInfoOrdered::initPlacement() {
    while (!initPlacementStep(UINT64_MAX)) {}
}

bool PlacementInfoOrdered::initPlacementStep(uint64_t deadline) {
    if (!mPlacementStarted) {
        MR::startInitLiveActorSystemInfo();
        mPlacementStarted = true;
    }
    const s32 count = mSetNum;
    while (mPlacementSet < count) {
        SameIdSet* set = mOrderedSetArray[mPlacementSet];
        const CreationFuncPtr creator = ::getCreator(*set);
        if (mPlacementLink == nullptr) mPlacementLink = set->mList.getFirst();
        if (creator == nullptr || mPlacementLink == nullptr) {
            ++mPlacementSet;
            mPlacementLink = nullptr;
            continue;
        }
        const TableIter& iter = mPlacementLink->getObject()->getInfoIter();
        if (!NebulaMods::isMissingFiles(set->mName)) {
            NebulaLoadTrace::Scope trace(set->mName);
            MR::setCurrentPlacementZoneId(MR::getPlacedZoneId(iter));
            if (mInitializing == nullptr) {
                NebulaLoadTrace::placed(set->mName, MR::getPlacedZoneId(iter));
<<<<<<< HEAD
                // Online co-op: the actors it makes as it is placed are its parts.
                NebulaCoopWorld::beginPlacement();
                // A mod's object (placed by its name, or what a change turns the placed one into) is made by its own
                // name; the game's by their Japanese names, as the game's creators take them.
                const char* const pModObject = set->mShapeId == -1 ? NebulaMods::modObjectName(set->mName) : nullptr;
                mInitializing = creator(pModObject != nullptr ? pModObject : MR::getJapaneseObjectName(set->mName));
                if (mInitializing == nullptr) {
                    // Nothing made (a mod's object it could not make): this placement is left out.
                    NebulaCoopWorld::notePlaced(set->mName, nullptr, iter);
=======
                // The game's creators take the Japanese names.
                mInitializing = creator(MR::getJapaneseObjectName(set->mName));
                mPlacementLink->getObject()->mMade = mInitializing;
                if (mInitializing == nullptr) {
                    // Nothing made (a mod's change removed it): this placement is left out.
>>>>>>> upstream/main
                    MR::clearCurrentPlacementZoneId();
                    mPlacementLink = mPlacementLink->getNext();
                    if (mPlacementLink == nullptr) ++mPlacementSet;
                    continue;
                }
            }
            MR::initLiveActorSystemInfo(iter);
            bool ready;
            {
                // A name a mod aliased (classes.md, "alias"): its arguments while the object initialises.
                const luma::classes::AliasPlacing alias(set->mName, iter.mInfo, iter.mIndex);
                ready = mInitializing->initStep(iter, deadline);
            }
            MR::initLiveActorSystemInfo(iter);
            MR::clearCurrentPlacementZoneId();
            if (!ready) return false;
<<<<<<< HEAD
            // Online co-op: the placed things the instance's record follows.
            NebulaCoopWorld::notePlaced(set->mName, mInitializing, iter);
            // The mods' classes on what this placement made (luma.json "object_classes" with a script, plugins' hooks).
            if (lumacore::sClassRowCount != 0) [[unlikely]] {
                lumacore::attachClass(mInitializing, set->mName);
            }
=======
>>>>>>> upstream/main
            mInitializing = nullptr;
        }
        mPlacementLink = mPlacementLink->getNext();
        if (mPlacementLink == nullptr) ++mPlacementSet;
        if (nebula_host()->clock_ns(nebula_host()->user) >= deadline) {
            if (mPlacementSet >= count) NebulaLoadTrace::placementDone();
            return mPlacementSet >= count;
        }
    }
    NebulaLoadTrace::placementDone();
    return true;
}

void PlacementInfoOrdered::insert(const Identifier& rIdentifier, const TableIter& rIter) {
    SameIdSet* pSet = find(rIdentifier);
    if (pSet == nullptr) {
        pSet = createSameIdSet(rIdentifier);
    }

    pSet->mList.append(createIndex(rIter));
}

PlacementInfoOrdered::SameIdSet* PlacementInfoOrdered::find(const Identifier& rIdentifier) const {
    for (s32 i = 0; i < mSetNum; i++) {
        SameIdSet* pSet = mOrderedSetArray[i];
        if (*pSet == rIdentifier) {
            return pSet;
        }
    }

    return nullptr;
}

PlacementInfoOrdered::SameIdSet* PlacementInfoOrdered::createSameIdSet(const Identifier& rIdentifier) {
    SameIdSet* pSet = &mSetArray[mSetNum];
    mOrderedSetArray[mSetNum] = pSet;
    mSetNum++;
    *static_cast< Identifier* >(pSet) = rIdentifier;
    return pSet;
}

PlacementInfoOrdered::Index* PlacementInfoOrdered::createIndex(const TableIter& rIter) {
    Index* pIndex = &mIndexArray[mIndexCount];
    mIndexCount++;
    pIndex->mInfoIter = rIter;
    return pIndex;
}

PlacementInfoOrdered::Index::Index() : mInfoIter() {
}

NameObj* PlacementInfoOrdered::findMade(const Table* pTable, s32 entry) const {
    for (u32 i = 0; i < mIndexCount; i++) {
        if (mIndexArray[i].mInfoIter.mInfo == pTable && mIndexArray[i].mInfoIter.mIndex == entry) {
            return mIndexArray[i].mMade;
        }
    }
    return nullptr;
}

bool PlacementInfoOrdered::findPlaced(const void* pObject, TableIter* pOut) const {
    for (u32 i = 0; pObject != nullptr && i < mIndexCount; i++) {
        if (mIndexArray[i].mMade == pObject) {
            *pOut = mIndexArray[i].mInfoIter;
            return true;
        }
    }
    return false;
}

PlacementInfoOrdered::SameIdSet::SameIdSet() : Identifier(), mList() {
}

void PlacementInfoOrdered::attach(const Table* pInfo, PlacementInfoOrdered* pAfterScenario) {
    s32 count = pInfo->getNumEntries();
    for (s32 i = 0; i < count; i++) {
        TableIter iter(pInfo, i);
        const Identifier identifier = ::readIdentifier(iter);
        if (pAfterScenario != nullptr && ::isAfterScenario(identifier)) {
            pAfterScenario->insert(identifier, iter);
        } else {
            insert(identifier, iter);
        }
    }
}
