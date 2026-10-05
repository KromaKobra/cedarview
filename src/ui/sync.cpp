#include "sync.h"

#include "core/log.h"
#include "modetransport.h"
#include "viewmodels/campus.h"
#include "viewmodels/chapel.h"
#include "viewmodels/curfew.h"
#include "viewmodels/dining.h"
#include "viewmodels/format.h"
#include "viewmodels/hours.h"
#include "viewmodels/sourcestatus.h"
#include "viewmodels/today.h"

#include <algorithm>

namespace mycu {

using Phase = LoginController::Phase;

SyncCoordinator::SyncCoordinator(Parts parts, QObject *parent)
    : QObject(parent)
    , m_parts(std::move(parts))
    , m_lastPhase(m_parts.login->phase())
{
    LoginController *login = m_parts.login;
    connect(login, &LoginController::phaseChanged, this, &SyncCoordinator::onPhaseChanged);
    connect(login, &LoginController::previewChanged, this, &SyncCoordinator::onPreviewChanged);
    connect(login, &LoginController::signedOut, this, &SyncCoordinator::onSignedOut);
    connect(login, &LoginController::phaseChanged, this, &SyncCoordinator::changed);

    // Two sources can notice an expired session — a fetch that failed, and
    // (on the desktop) the WebView transport noticing first. Both land in
    // the one check; the login controller ignores the second.
    connect(m_parts.chapel, &ChapelViewModel::sessionExpired, this, &SyncCoordinator::onSessionExpired);
    connect(m_parts.dining, &DiningViewModel::sessionExpired, this, &SyncCoordinator::onSessionExpired);

    for (SourceStatus *status : statuses())
        connect(status, &SourceStatus::changed, this, &SyncCoordinator::changed);
    for (SourceStatus *status : personal())
        connect(status, &SourceStatus::loaded, this, &SyncCoordinator::onPersonalLoaded);

    m_clock.setInterval(CLOCK_TICK_MS);
    connect(&m_clock, &QTimer::timeout, this, &SyncCoordinator::tick);
    m_clock.start();

    onPhaseChanged();
}

QList<SourceStatus *> SyncCoordinator::personal() const
{
    return {m_parts.chapel->skipsStatus(), m_parts.dining->planStatus()};
}

QList<SourceStatus *> SyncCoordinator::statuses() const
{
    return {m_parts.chapel->skipsStatus(), m_parts.dining->planStatus(), m_parts.dining->menuStatus(),
            m_parts.chapel->scheduleStatus()};
}

namespace {

bool fresh(const SourceStatus *status)
{
    const qint64 age = status->ageSecs();
    return status->hasData() && age >= 0 && age < status->freshSecs();
}

} // namespace

bool SyncCoordinator::menusFresh() const
{
    return fresh(m_parts.dining->menuStatus()) && m_parts.dining->hasMenuFor(now().date());
}

bool SyncCoordinator::scheduleFresh() const
{
    return fresh(m_parts.chapel->scheduleStatus());
}

bool SyncCoordinator::personalFresh() const
{
    const QList<SourceStatus *> sources = personal();
    return std::all_of(sources.cbegin(), sources.cend(), fresh);
}

bool SyncCoordinator::busy() const
{
    const QList<SourceStatus *> all = statuses();
    return m_parts.login->phase() == Phase::Checking
        || std::any_of(all.cbegin(), all.cend(), [](const SourceStatus *s) { return s->fetching(); });
}

bool SyncCoordinator::offline() const
{
    QList<SourceStatus *> failing;
    for (SourceStatus *status : statuses()) {
        if (!status->error().isEmpty())
            failing.append(status);
    }
    return !failing.isEmpty()
        && std::all_of(failing.cbegin(), failing.cend(), [](const SourceStatus *s) { return s->failedOffline(); });
}

QString SyncCoordinator::lastUpdatedText() const
{
    QDateTime newest;
    for (const SourceStatus *status : statuses()) {
        if (status->hasData() && (!newest.isValid() || status->updatedAt() > newest))
            newest = status->updatedAt();
    }
    const QString text = SourceStatus::describeAge(newest, now());
    return text.isEmpty() ? text : text.left(1).toLower() + text.mid(1);
}

QString SyncCoordinator::savedAtText() const
{
    QDateTime oldest;
    for (const SourceStatus *status : statuses()) {
        if (status->hasData() && (!oldest.isValid() || status->updatedAt() < oldest))
            oldest = status->updatedAt();
    }
    if (!oldest.isValid())
        return {};
    if (oldest.date() == now().date())
        return fmt::clock(oldest.time());
    if (oldest.date() == now().date().addDays(-1))
        return QStringLiteral("yesterday");
    return fmt::monthDay(oldest.date());
}

void SyncCoordinator::start()
{
    fetchPublic(false);
    switch (m_parts.login->phase()) {
    case Phase::SignedIn:
        if (!personalFresh())
            fetchPersonal();
        break;
    case Phase::Welcome:
        // The WebView may hold a session the app forgot (an upgrade from v0.3,
        // which could lose its record of the sign-in): a personal fetch says
        // so without showing anything, and its success skips Welcome. Only
        // then — after a sign-out it would be the app signing itself back in
        // behind the user's back, and a fresh install has nothing to find. On
        // the desktop a fetch needs a loaded page, so there is nothing cheap to
        // try.
        if (m_parts.nativeSession && m_parts.login->mayHoldForgottenSession()) {
            qCInfo(lcApp) << "sync: first run; checking quietly for a session the WebView kept";
            fetchPersonal();
        }
        break;
    default:
        // Checking waits for its answer; preview fetched when it began.
        break;
    }
}

void SyncCoordinator::fetchPublic(bool force)
{
    if (force || !menusFresh())
        m_parts.dining->refresh();
    else
        qCDebug(lcApp) << "sync: the saved menu is fresh; not fetching";
    if (force || !scheduleFresh())
        m_parts.chapel->refreshSchedule();
    else
        qCDebug(lcApp) << "sync: the saved schedule is fresh; not fetching";
}

void SyncCoordinator::fetchPersonal()
{
    m_parts.chapel->refresh();
    m_parts.dining->refreshPlan();
}

void SyncCoordinator::refreshAll()
{
    fetchPublic(true);
    switch (m_parts.login->phase()) {
    case Phase::SignedIn:
    case Phase::Preview:
        fetchPersonal();
        break;
    case Phase::NeedsSignIn:
        m_parts.login->checkAgain();
        break;
    default:
        break;
    }
    tick();
}

void SyncCoordinator::resume()
{
    tick();
    fetchPublic(false);
    if (m_parts.login->phase() == Phase::SignedIn && !personalFresh())
        fetchPersonal();
}

void SyncCoordinator::tick()
{
    m_parts.chapel->tick();
    m_parts.dining->tick();
    if (m_parts.curfew)
        m_parts.curfew->refreshAll();
    if (m_parts.hours)
        m_parts.hours->refreshAll();
    if (m_parts.campus)
        m_parts.campus->refreshAll();
    if (m_parts.today)
        m_parts.today->refreshAll();
    emit changed();
}

void SyncCoordinator::onPhaseChanged()
{
    const Phase phase = m_parts.login->phase();
    const bool waiting = phase == Phase::Checking || phase == Phase::SigningIn;
    const bool noSession = phase == Phase::Welcome || phase == Phase::NeedsSignIn;
    for (SourceStatus *status : personal()) {
        status->setAwaitingSignIn(waiting);
        status->setNeedsSignIn(noSession);
    }

    // Newly signed in — a sign-in, a silent check that came back, or a check
    // that gave up waiting — means the personal figures are worth fetching,
    // unless they were fetched a moment ago. (Not while sample data is still
    // switching off: the coordinator does that itself.)
    const bool previewing = m_parts.mode && m_parts.mode->preview();
    if (phase == Phase::SignedIn && m_lastPhase != Phase::SignedIn && !previewing && !personalFresh())
        fetchPersonal();
    m_lastPhase = phase;
}

void SyncCoordinator::onPreviewChanged(bool on)
{
    qCInfo(lcApp) << "sync: sample data" << (on ? "on" : "off");
    if (m_parts.mode)
        m_parts.mode->setPreview(on);
    m_parts.chapel->setPreview(on);
    m_parts.dining->setPreview(on);

    if (on) {
        // Fixtures, so everything at once and at once.
        fetchPublic(true);
        fetchPersonal();
        return;
    }
    onPhaseChanged();
    fetchPublic(false);
    if (m_parts.login->phase() == Phase::SignedIn && !personalFresh())
        fetchPersonal();
}

void SyncCoordinator::onSignedOut()
{
    // The records, saved and shown.
    if (m_parts.cache)
        m_parts.cache->clearPersonal();
    m_parts.chapel->clearPersonal();
    m_parts.dining->clearPersonal();
}

void SyncCoordinator::onSessionExpired()
{
    m_parts.login->onSessionExpired();
}

void SyncCoordinator::onPersonalLoaded()
{
    if (m_parts.mode && m_parts.mode->preview())
        return;
    m_parts.login->confirmSignedIn();
}

} // namespace mycu
