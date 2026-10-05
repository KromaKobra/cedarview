// Live data or sample data, switchable while the app runs.
//
// Every viewmodel is handed this one transport. Normally it passes requests to
// the live router (Self-Service, the menu and the chapel feed); in sample-data
// preview it passes them to the bundled fixtures instead, redated so that
// today has a menu and the next chapel is this week. Nothing above it knows
// which — which is the whole design of the transport seam, now used at
// runtime rather than only in tests.
//
// The switch is atomic and takes effect on the next request. A request
// already in flight finishes against whichever side it started on; the
// viewmodels drop such a late answer themselves (their generation counter),
// so sample figures never land on a live screen or the other way round.

#pragma once

#include "core/transport.h"

#include <QDate>

#include <atomic>
#include <functional>

namespace mycu {

class ModeTransport : public Transport
{
public:
    ModeTransport(TransportPtr live, TransportPtr preview);

    void setPreview(bool on) { m_preview.store(on); }
    bool preview() const { return m_preview.load(); }

    Response get(const QString &path) override;

private:
    TransportPtr m_live;
    TransportPtr m_sample;
    std::atomic<bool> m_preview{false};
};

// The sample-data side: the fixtures in `root` (the bundled ":/fixtures" in the
// app), with the menus, the chapel schedule and the meal-plan activity moved
// onto the dates around `today`.
TransportPtr makePreviewTransport(const QString &root, std::function<QDate()> today);

// What stands in for the live side under --demo, where there is no browser and
// no network: every request fails, plainly.
class UnavailableTransport : public Transport
{
public:
    Response get(const QString &path) override;
};

} // namespace mycu
