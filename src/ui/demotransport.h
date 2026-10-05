// Sample data, moved onto the dates the app is asking about.
//
// The committed captures (`tests/fixtures/`) hold real responses from fixed
// days in 2026 — two days of menus, a schedule starting Sep 17, a term's meal
// activity — and the tests depend on them staying exactly that. Served as-is
// in sample-data preview, today would have no menu, every chapel would be
// over, and the activity list would start months ago.
//
// So these wrap the fixture transport and rewrite only the dates, keyed on
// `today` (a seam, so --clock moves them too):
//
// * RedatedMenusTransport answers each `?days=N&start=D` request with N dates
//   starting at D (today when absent), each carrying one of the captured days'
//   blocks. The choice is keyed on the date itself, not its position in the
//   window, so a day shows the same menu whichever window it was fetched in —
//   paging back and forth never makes Tuesday's dinner change.
// * RedatedScheduleTransport shifts the chapel feed by whole weeks, so the
//   captured chapel on today's weekday falls today (on a weekend, on Monday).
// * RedatedBalanceTransport shifts the meal-plan activity so its newest row
//   was yesterday.
//
// Preview only. Nothing else should ever see data that was not served for the
// date it claims.

#pragma once

#include "core/transport.h"

#include <QDate>

#include <functional>

namespace mycu {

using TodayFn = std::function<QDate()>;

class RedatedMenusTransport : public Transport
{
public:
    explicit RedatedMenusTransport(TransportPtr fixtures, TodayFn today = {});

    Response get(const QString &path) override;

private:
    TransportPtr m_fixtures;
    TodayFn m_today;
};

class RedatedScheduleTransport : public Transport
{
public:
    explicit RedatedScheduleTransport(TransportPtr fixtures, TodayFn today = {});

    Response get(const QString &path) override;

private:
    TransportPtr m_fixtures;
    TodayFn m_today;
};

class RedatedBalanceTransport : public Transport
{
public:
    explicit RedatedBalanceTransport(TransportPtr fixtures, TodayFn today = {});

    Response get(const QString &path) override;

private:
    TransportPtr m_fixtures;
    TodayFn m_today;
};

} // namespace mycu
