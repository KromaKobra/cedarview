// `--demo`'s dining menus, moved onto the dates the app is asking about.
//
// The committed capture (`tests/fixtures/diningdata_cedarville_edu_api_menus.json`)
// holds two real days, 2026-09-16/17, and the tests depend on it staying
// exactly that. Served as-is, the Home Cooking tab and the summary card's next
// meal are empty in demo mode on every other day, which makes both untestable
// without a phone and a live menu.
//
// So this wraps the fixture transport and answers each `?days=N&start=D`
// request with N dates starting at D (today when absent), each carrying one of
// the captured days' blocks. The choice is keyed on the date itself, not its
// position in the window, so a day shows the same menu whichever window it was
// fetched in — paging back and forth never makes Tuesday's dinner change.
//
// Demo mode only. Nothing else should ever see a menu that was not served for
// the date it claims.

#pragma once

#include "core/transport.h"

namespace mycu {

class RedatedMenusTransport : public Transport
{
public:
    explicit RedatedMenusTransport(TransportPtr fixtures);

    Response get(const QString &path) override;

private:
    TransportPtr m_fixtures;
};

} // namespace mycu
