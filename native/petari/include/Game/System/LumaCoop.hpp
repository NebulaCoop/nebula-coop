#pragma once

// LumaCore's `coop` service (luma_coop.h): NebulaCoop's room for mods, over NebulaCoopMods.

#include <luma_coop.h>
#include <luma_service_provider.h>
#include <nebula_types.h>

namespace luma {
    namespace coop {
        // True once a mod has called the service: NebulaCoopMods then reads the room each frame.
        bool used();
        // NebulaCoopMods::update, for what changed: a LUMA_COOP_EVENT_* to every watching mod.
        void event(u32 kind, s32 value);
        // A chat command this player typed (its name, its word's place): a COMMAND event to the mod that added it.
        void command(const char* pName, s32 word);
        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();
    }
}
