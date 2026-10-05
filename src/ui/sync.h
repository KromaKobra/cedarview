// When to fetch what. The one place that decides.
//
// Before v0.4 fetches were started from main.cpp at launch, from Main.qml's
// refresh button, from each screen's pull-to-refresh, and from the login
// controller on every sign-in. Each knew a little; none knew whether the data
// on screen was already fresh. This replaces all of them.
//
// Startup, in order:
//
// 1. The viewmodels have already read back what the last run saved (in their
//    constructors, before QML loads), so there is something to show.
// 2. The public sources are fetched — unless what was saved is fresh: menus
//    under 30 minutes old with today in them, a schedule under two hours.
// 3. The personal sources (skips, meal plan), by phase: signed in → fetch;
//    checking → wait for the check; Welcome after an upgrade from v0.3 → on
//    Android, a silent probe (the WebView may hold a session after all, and
//    then Welcome is skipped); otherwise nothing.
//
// After that: a fetch is repeated when the app comes back to the foreground
// and its source has gone stale, when the user pulls to refresh (everything,
// regardless), and when a sign-in or a silent check succeeds (personal).
//
// It also turns the login phase into what each personal source shows —
// "awaiting sign-in" while a check runs, "sign in to see" when there is no
// session — and owns the clock tick that keeps every countdown honest.
//
// Exposed to QML as the context property `sync`.

#pragma once

#include "core/cache.h"
#include "login.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>

namespace mycu {

class CampusViewModel;
class ChapelViewModel;
class CurfewViewModel;
class DiningViewModel;
class HoursViewModel;
class ModeTransport;
class SourceStatus;
class TodayViewModel;

// How often the clock-driven text is re-read, in milliseconds.
inline constexpr int CLOCK_TICK_MS = 15000;

class SyncCoordinator : public QObject
{
    Q_OBJECT

    // Something is being fetched — the thin bar along the top.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // Every source that last failed failed for want of a network.
    Q_PROPERTY(bool offline READ offline NOTIFY changed)
    // "updated 9:41 AM" — the newest data on screen — or "".
    Q_PROPERTY(QString lastUpdatedText READ lastUpdatedText NOTIFY changed)
    // "8:15 AM": when the oldest figure on screen was saved, for the offline
    // banner's "Showing what was saved at …".
    Q_PROPERTY(QString savedAtText READ savedAtText NOTIFY changed)

public:
    struct Parts
    {
        LoginController *login = nullptr;
        ChapelViewModel *chapel = nullptr;
        DiningViewModel *dining = nullptr;
        CurfewViewModel *curfew = nullptr;
        HoursViewModel *hours = nullptr;
        CampusViewModel *campus = nullptr;
        TodayViewModel *today = nullptr;
        // Null in tests, which have no preview to switch to.
        std::shared_ptr<ModeTransport> mode;
        std::optional<PayloadCache> cache;
        // Personal data can be fetched with no page loaded (Android).
        bool nativeSession = false;
    };

    explicit SyncCoordinator(Parts parts, QObject *parent = nullptr);

    bool busy() const;
    bool offline() const;
    QString lastUpdatedText() const;
    QString savedAtText() const;

    // Step 2 and 3 above. Call once, after LoginController::start().
    void start();

    // The clock, as a seam for tests.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

public slots:
    // Pull-to-refresh: every source, fresh or not; under the "session ended"
    // banner, the silent check again.
    void refreshAll();
    // Back in the foreground: only what has gone stale.
    void resume();
    // Re-read the clock everywhere.
    void tick();

signals:
    void changed();

private:
    QList<SourceStatus *> statuses() const;
    QList<SourceStatus *> personal() const;
    bool menusFresh() const;
    bool scheduleFresh() const;
    bool personalFresh() const;
    void fetchPublic(bool force);
    void fetchPersonal();
    void onPhaseChanged();
    void onPreviewChanged(bool on);
    void onSignedOut();
    void onSessionExpired();
    void onPersonalLoaded();

    Parts m_parts;
    LoginController::Phase m_lastPhase;
    QTimer m_clock;
};

} // namespace mycu
