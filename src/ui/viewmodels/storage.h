// Where a viewmodel keeps what it loaded between runs: the session metadata
// (remembered IDs, the last term) and the on-device payload cache.
//
// Optional everywhere it is taken. A viewmodel without one keeps nothing,
// which is what the tests that do not care about persistence want. Sample-data
// preview switches it off at runtime instead (setPreview), for the same
// reason: sample figures must never be written over a real user's saved ones.

#pragma once

#include "core/cache.h"
#include "core/session.h"

namespace mycu {

struct Storage
{
    SessionStore session;
    PayloadCache cache;

    // Both under one state directory, which is how the app lays them out.
    static Storage at(const QString &stateDir) { return {SessionStore(stateDir), PayloadCache(stateDir)}; }
};

} // namespace mycu
