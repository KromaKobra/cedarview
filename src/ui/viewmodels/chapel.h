// Chapel screen: the skip ledger, the upcoming-chapel schedule, and the
// viewmodel.
//
// A viewmodel holds one screen's worth of state as Qt properties, runs its
// provider on a worker thread, and translates core exceptions into things QML
// can display. It contains no parsing and no HTTP.
//
// The pattern for a new screen is always the same:
//
// * a QAbstractListModel for the rows (QML list views want a model, not a
//   property holding a list — a property re-emits the whole list on every
//   change);
// * a QObject with a refresh() slot, busy/error properties, and whatever
//   scalars the header shows.

#pragma once

#include "core/models.h"
#include "core/session.h"
#include "core/transport.h"

#include <QAbstractListModel>
#include <QObject>

#include <exception>
#include <functional>
#include <optional>

namespace mycu {

// Exposes the chapel-skip ledger to a QML ListView.
//
// Rows are *balance movements*, not attendance. `whenText` is pre-formatted
// here because the fallback — show the reason when there is no chapel date,
// which is the normal case for manual adjustments — is a data decision, not a
// presentation one.
class ChapelListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        WhenRole = Qt::UserRole + 1,
        ReasonRole,
        TypeRole,
        CountRole,
        SkipRole,
    };

    using QAbstractListModel::QAbstractListModel;

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void replace(const QList<ChapelLedgerEntry> &entries);

private:
    QList<ChapelLedgerEntry> m_entries;
};

// Current and upcoming chapels, flat, with a header row per week.
//
// Flat for the same reason as the dining tab's models: one model, one
// Repeater, and the delegate picks a look from `isHeader`. Everything is
// pre-formatted here, because "is this one happening now" and "which week is
// this" are questions about the clock, and the clock is C++'s.
class ScheduleListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        HeaderRole = Qt::UserRole + 1,
        HeadingRole,
        WhoRole,
        SubtitleRole,
        DescriptionRole,
        DayNameRole,
        DayNumberRole,
        TimeTextRole,
        BadgeRole,
        IsNowRole,
        LivestreamRole,
    };

    using QAbstractListModel::QAbstractListModel;

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Rows for `chapels` that are not over yet, grouped by week.
    //
    // Undated entries are left out: with no date there is no week to put them
    // under, and no way to say whether they have happened.
    void replace(const QList<UpcomingChapel> &chapels, const QDateTime &now);

private:
    struct Row
    {
        bool isHeader = false;
        QString heading, who, subtitle, description, dayName, dayNumber, timeText, badge;
        bool isNow = false;
        bool livestream = false;
    };
    QList<Row> m_rows;
};

// State and actions for the chapel screen.
//
// Exposed to QML as the context property `chapel`.
class ChapelViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject *records READ records CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // True once a fetch has succeeded at least once this run.
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString term READ term NOTIFY changed)
    Q_PROPERTY(QString studentName READ studentName NOTIFY changed)

    // `used`, `allowed` and `remaining` are Cedarville's own figures, passed
    // through untouched. -1 is the "not known" sentinel: QML has no null int,
    // and 0 would render as "0 of 0 skips" — a confident lie.
    Q_PROPERTY(int used READ used NOTIFY changed)
    Q_PROPERTY(int allowed READ allowed NOTIFY changed)
    Q_PROPERTY(int remaining READ remaining NOTIFY changed)

    Q_PROPERTY(QString allowanceText READ allowanceText NOTIFY changed)
    Q_PROPERTY(bool inGoodStanding READ inGoodStanding NOTIFY changed)
    Q_PROPERTY(bool requiredToAttend READ requiredToAttend NOTIFY changed)
    Q_PROPERTY(double remainingFraction READ remainingFraction NOTIFY changed)

    Q_PROPERTY(QString nextSpeaker READ nextSpeaker NOTIFY changed)
    Q_PROPERTY(QString nextChapelWhen READ nextChapelWhen NOTIFY changed)
    Q_PROPERTY(QString nextChapelDay READ nextChapelDay NOTIFY changed)
    Q_PROPERTY(QString nextChapelDateText READ nextChapelDateText NOTIFY changed)
    Q_PROPERTY(QString nextChapelTitle READ nextChapelTitle NOTIFY changed)

    Q_PROPERTY(QObject *schedule READ schedule CONSTANT)
    Q_PROPERTY(QString scheduleEmptyText READ scheduleEmptyText NOTIFY changed)

public:
    ChapelViewModel(TransportPtr transport, SessionStore store, QObject *parent = nullptr);

    QObject *records() { return &m_model; }
    bool busy() const { return m_busy; }
    bool loaded() const { return m_loaded; }
    QString error() const { return m_error; }
    QString term() const;
    QString studentName() const { return m_summary.studentName; }
    int used() const { return m_summary.used.value_or(-1); }
    int allowed() const { return m_summary.total.value_or(-1); }
    int remaining() const { return m_summary.remaining.value_or(-1); }

    // How the total is made up — "17 skips allowed + 1 manual arrangement".
    //
    // Worth surfacing: it is the only place the app can explain why the total
    // is 18 rather than the 17 everyone expects.
    QString allowanceText() const;
    bool inGoodStanding() const { return m_summary.isInGoodStanding; }
    bool requiredToAttend() const { return m_summary.isRequiredToAttend; }

    // How much of the allowance is left, 0.0–1.0, for the progress bar.
    //
    // 0.0 when either figure is unknown, which the QML reads together with
    // `remaining >= 0` to hide the bar rather than draw an empty one. Clamped
    // because the server's `used` and `total` come from different halves of
    // its own arithmetic (see ChapelSummary) and are not guaranteed to agree —
    // a bar overflowing its track would look broken where a full bar just
    // looks full.
    double remainingFraction() const;

    // Who is speaking at the next chapel, or "" if unknown.
    //
    // Empty rather than a placeholder so the QML can hide the whole row: over
    // the summer there genuinely is no next chapel, and "TBA" would be a claim
    // we cannot support.
    QString nextSpeaker() const;

    // "Today 10:00 AM" / "Tomorrow 10:00 AM" / "Mon 10:00 AM".
    QString nextChapelWhen() const;

    // "Today" / "Tomorrow" / "Friday" — the summary screen's badge.
    //
    // Split out from nextChapelWhen rather than parsed back out of it: the
    // badge and the date line are two separate pieces of text on the summary
    // card, and slicing a formatted string to get one of them back is how a UI
    // ends up displaying "Tomorrow 10:00" in a pill.
    QString nextChapelDay() const;

    // "Fri, Sep 18 · 10:00 AM" — the exact when, under the headline.
    //
    // The badge says "Tomorrow"; this says which day that actually is, which
    // is the thing you need when deciding whether to set an alarm.
    QString nextChapelDateText() const;

    // The event name, but only when it adds something.
    //
    // The API sets Title to the speaker's name verbatim, so showing both gives
    // "Garrett Kell — Garrett Kell".
    QString nextChapelTitle() const;

    QObject *schedule() { return &m_schedule; }

    // Why the schedule list is empty, or "" when it is not.
    QString scheduleEmptyText() const;

    // Which chapel is happening now is a function of the clock, so the clock is
    // a seam — as in DiningViewModel.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

public slots:
    // Fetch chapel attendance on a worker thread.
    //
    // Cache-and-refresh-on-demand, never polling. This is one student reading
    // their own record; it should behave like it.
    void refresh();

    // Load the whole upcoming-chapel feed. Needs no session.
    void refreshSchedule();

    // Everything on this screen.
    //
    // What the Refresh button and pull-to-refresh call. The screen draws on
    // two unrelated sources — the authenticated skip ledger and the public
    // upcoming-chapel feed — and a reader pulling down means "update what I am
    // looking at", not "update the half of it that needs a session". Keeping
    // the composition here means a third source is wired in one place rather
    // than in every gesture handler.
    void refreshAll();

signals:
    void changed();

    // The provider hit an expired session. main.cpp wires this to
    // LoginController::onSessionExpired, which reopens the sign-in surface and
    // then re-triggers refresh().
    void sessionExpired();

private:
    friend class TestViewModels;

    void onLoaded(const ChapelSummary &summary);
    void onFailed(std::exception_ptr error);
    void onScheduleLoaded(const QList<UpcomingChapel> &chapels);
    void onScheduleFailed(std::exception_ptr error);

    TransportPtr m_transport;
    SessionStore m_store;
    SessionState m_state;
    ChapelListModel m_model;
    ChapelSummary m_summary;
    bool m_busy = false;
    QString m_error;
    bool m_loaded = false;

    // The upcoming-chapel feed is a *different*, unauthenticated service
    // (mediaserve.cedarville.edu). It is on this screen because that is where
    // it belongs to a reader, not because it shares a source — so it loads
    // independently and a failure in one never blanks the other.
    std::optional<UpcomingChapel> m_next;

    // The Chapel tab's list: every chapel from now to the end of the feed,
    // from the same fetch as m_next.
    ScheduleListModel m_schedule;
    bool m_scheduleLoaded = false;
    QString m_scheduleError;
};

} // namespace mycu
