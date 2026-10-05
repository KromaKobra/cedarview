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
// * a QObject with a refresh() slot, a SourceStatus per source, and whatever
//   scalars the header shows.
//
// Cache-first: the constructor reads back what the last run saved (Storage),
// so the screen opens on real figures, stamped with their age, and a refresh
// replaces them when it lands.

#pragma once

#include "core/models.h"
#include "core/providers/chapel.h"
#include "core/session.h"
#include "core/transport.h"
#include "sourcestatus.h"
#include "storage.h"

#include <QAbstractListModel>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>

#include <exception>
#include <functional>
#include <memory>
#include <optional>

namespace mycu {

// How long each chapel source counts as fresh, in seconds. The ledger is
// personal and changes when you miss a chapel; the schedule changes weekly.
inline constexpr qint64 SKIPS_FRESH_SECS = 5 * 60;
inline constexpr qint64 SCHEDULE_FRESH_SECS = 2 * 60 * 60;

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
        TitleRole,
        DetailRole,
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
        IsTodayRole,
        PartRole,
        YoutubeIdRole,
        StartsAtRole,
        DateTextRole,
        // 0 for this week's rows (header included), 1 for next week's, …
        WeekIndexRole,
        // The first chapel under its week's heading (no rule above it).
        FirstInWeekRole,
    };

    using QAbstractListModel::QAbstractListModel;

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Rows for `chapels` that are not over yet, grouped by week.
    //
    // Undated entries are left out: with no date there is no week to put them
    // under, and no way to say whether they have happened. When the rows are
    // the same chapels as before — a clock tick that only moved a countdown —
    // they are updated in place rather than reset, so the list does not
    // rebuild under the reader's thumb twice a minute.
    void replace(const QList<UpcomingChapel> &chapels, const QDateTime &now);

    // How many weeks the rows span.
    int weeks() const;

private:
    struct Row
    {
        bool isHeader = false;
        bool firstInWeek = false;
        QString heading, who, subtitle, description, dayName, dayNumber, timeText, badge, part, youtubeId,
            dateText;
        QDateTime startsAt;
        int weekIndex = 0;
        bool isNow = false;
        bool isToday = false;
        bool livestream = false;
    };
    QList<Row> m_rows;
};

// "Part 1 of 2" for each chapel in a run of consecutive chapels by the same
// named speaker, "" otherwise, in the order of `chapels` (soonest first).
// Consecutive means the next chapel in the feed, no more than three days later
// — so a Friday-and-Monday pair still counts.
QStringList seriesParts(const QList<UpcomingChapel> &chapels);

// "15.5 credits" from "Registered for 15.5 credits (more than 6)", and so on:
// the requirement reasons, short enough for a chip. Unrecognised text is kept,
// in sentence case.
QString shortReason(const QString &reason);

// State and actions for the chapel screen.
//
// Exposed to QML as the context property `chapel`.
class ChapelViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject *records READ records CONSTANT)
    Q_PROPERTY(QObject *skipsStatus READ skipsStatus CONSTANT)
    Q_PROPERTY(QObject *scheduleStatus READ scheduleStatus CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // True once there are figures to show: fetched this run, or saved by the
    // last one.
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString term READ term NOTIFY changed)
    // "Fall 2026" — the term, short enough for a header.
    Q_PROPERTY(QString termLabel READ termLabel NOTIFY changed)
    Q_PROPERTY(QString studentName READ studentName NOTIFY changed)

    // `used`, `allowed` and `remaining` are Cedarville's own figures, passed
    // through untouched. -1 is the "not known" sentinel: QML has no null int,
    // and 0 would render as "0 of 0 skips" — a confident lie.
    Q_PROPERTY(int used READ used NOTIFY changed)
    Q_PROPERTY(int allowed READ allowed NOTIFY changed)
    Q_PROPERTY(int remaining READ remaining NOTIFY changed)

    Q_PROPERTY(QString allowanceText READ allowanceText NOTIFY changed)
    // The allowance as figures: [{figure: "17", label: "Base allowance"},
    // {figure: "+1", label: "Manual arrangement"}].
    Q_PROPERTY(QVariantList allowance READ allowance NOTIFY changed)
    Q_PROPERTY(bool inGoodStanding READ inGoodStanding NOTIFY changed)
    Q_PROPERTY(bool requiredToAttend READ requiredToAttend NOTIFY changed)
    // Why attendance is required, each short enough for a chip.
    Q_PROPERTY(QStringList requirementReasons READ requirementReasons NOTIFY changed)
    Q_PROPERTY(double remainingFraction READ remainingFraction NOTIFY changed)
    // "About 1 a week through Dec 11", or "" outside a term.
    Q_PROPERTY(QString skipsPerWeekText READ skipsPerWeekText NOTIFY clockChanged)
    // "3 entries".
    Q_PROPERTY(QString historyText READ historyText NOTIFY changed)

    Q_PROPERTY(bool hasNextChapel READ hasNextChapel NOTIFY changed)
    Q_PROPERTY(QString nextSpeaker READ nextSpeaker NOTIFY changed)
    Q_PROPERTY(QString nextChapelWhen READ nextChapelWhen NOTIFY clockChanged)
    Q_PROPERTY(QString nextChapelDay READ nextChapelDay NOTIFY clockChanged)
    Q_PROPERTY(QString nextChapelDateText READ nextChapelDateText NOTIFY changed)
    Q_PROPERTY(QString nextChapelTitle READ nextChapelTitle NOTIFY changed)
    Q_PROPERTY(QString nextChapelTime READ nextChapelTime NOTIFY changed)
    Q_PROPERTY(QDateTime nextChapelStartsAt READ nextChapelStartsAt NOTIFY changed)
    Q_PROPERTY(QString nextChapelDescription READ nextChapelDescription NOTIFY changed)
    Q_PROPERTY(bool nextChapelLivestream READ nextChapelLivestream NOTIFY changed)
    Q_PROPERTY(QString nextChapelYoutubeId READ nextChapelYoutubeId NOTIFY changed)
    // "in 18 min" / "in 3 h" / "tomorrow" — or "now" while it is on.
    Q_PROPERTY(QString nextChapelCountdown READ nextChapelCountdown NOTIFY clockChanged)
    // Today's chapel, from now until it ends.
    Q_PROPERTY(bool chapelToday READ chapelToday NOTIFY clockChanged)

    Q_PROPERTY(QObject *schedule READ schedule CONSTANT)
    // How many weeks the schedule spans, for "Show 3 more weeks".
    Q_PROPERTY(int scheduleWeeks READ scheduleWeeks NOTIFY changed)
    Q_PROPERTY(QString scheduleEmptyText READ scheduleEmptyText NOTIFY changed)

public:
    // No `storage`: nothing is read back or saved.
    ChapelViewModel(TransportPtr transport, std::optional<Storage> storage = std::nullopt,
                    QObject *parent = nullptr);

    QObject *records() { return &m_model; }
    SourceStatus *skipsStatus() { return &m_skipsStatus; }
    SourceStatus *scheduleStatus() { return &m_scheduleStatus; }
    bool busy() const { return m_busy; }
    bool loaded() const { return m_skipsStatus.hasData(); }
    QString error() const { return m_error; }
    QString term() const;
    QString termLabel() const;
    QString studentName() const { return m_summary.studentName; }
    int used() const { return m_summary.used.value_or(-1); }
    int allowed() const { return m_summary.total.value_or(-1); }
    int remaining() const { return m_summary.remaining.value_or(-1); }

    // How the total is made up — "17 skips allowed + 1 manual arrangement".
    //
    // Worth surfacing: it is the only place the app can explain why the total
    // is 18 rather than the 17 everyone expects.
    QString allowanceText() const;
    QVariantList allowance() const;
    bool inGoodStanding() const { return m_summary.isInGoodStanding; }
    bool requiredToAttend() const { return m_summary.isRequiredToAttend; }
    QStringList requirementReasons() const;

    // How much of the allowance is left, 0.0–1.0, for the progress bar.
    //
    // 0.0 when either figure is unknown, which the QML reads together with
    // `remaining >= 0` to hide the bar rather than draw an empty one. Clamped
    // because the server's `used` and `total` come from different halves of
    // its own arithmetic (see ChapelSummary) and are not guaranteed to agree —
    // a bar overflowing its track would look broken where a full bar just
    // looks full.
    double remainingFraction() const;

    // How fast the skips left can be spent and still last the term.
    QString skipsPerWeekText() const;
    QString historyText() const;

    bool hasNextChapel() const { return m_next.has_value(); }

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

    // "10:00 AM".
    QString nextChapelTime() const;
    QDateTime nextChapelStartsAt() const;
    QString nextChapelDescription() const;
    bool nextChapelLivestream() const { return m_next && m_next->willLivestream; }
    QString nextChapelYoutubeId() const { return m_next ? m_next->youtubeId : QString(); }
    QString nextChapelCountdown() const;
    bool chapelToday() const;

    QObject *schedule() { return &m_schedule; }
    int scheduleWeeks() const { return m_schedule.weeks(); }

    // Why the schedule list is empty, or "" when it is not.
    QString scheduleEmptyText() const;

    // "18 min" / "2 h 5 min" / "3 days" from now until `when`, for the
    // speaker sheet; "Now" while a chapel is on, "" once it is over.
    Q_INVOKABLE QString fromNowText(const QDateTime &when) const;

    // Where "Watch live" goes: the livestream on YouTube, in the browser.
    Q_INVOKABLE QString watchUrl(const QString &youtubeId) const;

    // Every chapel in the feed, soonest first — what search reads.
    const QList<UpcomingChapel> &chapels() const { return m_chapels; }

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

    // Re-read the clock: which chapel is next and every countdown move on.
    void tick();

    // Forget the signed-in student's records, in memory: the figures, the
    // ledger, the remembered ID. Sign-out does this alongside deleting the
    // saved copy. The public schedule stays.
    void clearPersonal();

    // Sample data on or off. Either way everything shown is dropped; leaving
    // preview reads the user's own saved data back.
    void setPreview(bool on);

signals:
    void changed();
    // Only the clock moved: countdowns and "Today"/"Tomorrow". Kept apart from
    // changed() so a tick does not make every list on the screen rebuild.
    // Every changed() is also a clockChanged().
    void clockChanged();

    // The provider hit an expired session. The coordinator answers with a
    // silent sign-in, then calls refresh() again.
    void sessionExpired();

private:
    friend class TestViewModels;

    bool persisting() const { return m_storage.has_value() && !m_preview; }
    void hydrate();
    void resetProvider(const QString &studentId);
    void applySummary(const ChapelSummary &summary);
    void applySchedule(const QList<UpcomingChapel> &chapels);
    void onPayloadLoaded(const QJsonObject &payload);
    void onLoaded(const ChapelSummary &summary);
    void onFailed(std::exception_ptr error);
    void onScheduleLoaded(const QList<UpcomingChapel> &chapels);
    void onSchedulePayloadLoaded(const QJsonObject &payload);
    void onScheduleFailed(std::exception_ptr error);

    TransportPtr m_transport;
    std::optional<Storage> m_storage;
    SessionState m_state;
    bool m_preview = false;
    // Bumped whenever what is shown stops belonging to whoever asked for it
    // (sign-out, switching to or from sample data). A fetch that started under
    // an older generation is dropped when it lands.
    int m_generation = 0;
    // One for the life of the screen, so the student ID it reads is kept.
    std::shared_ptr<ChapelProvider> m_provider;

    ChapelListModel m_model;
    ChapelSummary m_summary;
    SourceStatus m_skipsStatus{SKIPS_FRESH_SECS};
    bool m_busy = false;
    QString m_error;

    // The upcoming-chapel feed is a *different*, unauthenticated service
    // (mediaserve.cedarville.edu). It is on this screen because that is where
    // it belongs to a reader, not because it shares a source — so it loads
    // independently and a failure in one never blanks the other.
    QList<UpcomingChapel> m_chapels;
    std::optional<UpcomingChapel> m_next;

    // The Chapel tab's list: every chapel from now to the end of the feed,
    // from the same fetch as m_next.
    ScheduleListModel m_schedule;
    SourceStatus m_scheduleStatus{SCHEDULE_FRESH_SECS};
    bool m_scheduleBusy = false;
    QString m_scheduleError;
};

} // namespace mycu
