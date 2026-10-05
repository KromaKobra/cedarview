// The Dining tab's Hours section: what is open now, what opens next, a
// timeline of the day, and the meal-swipe periods.
//
// Like CurfewViewModel this wraps hand-entered tables (core/hours.h) and a
// clock, not a provider: there is nothing to fetch. It is a viewmodel rather
// than JavaScript in the QML so the arithmetic is tested and the bindings are
// checked by tst_qml_contract.
//
// Exposed to QML as the context property `hours`.

#pragma once

#include "core/hours.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <functional>

namespace mycu {

// The timeline's axis: 7 AM to midnight, in minutes after midnight.
inline constexpr int TIMELINE_START = 7 * 60;
inline constexpr int TIMELINE_END = 24 * 60;

// One bar per place, for the day type shown. `segments` is a list of
// {left, width, kind}, the first two as fractions of the axis and `kind` one
// of "past", "now", "next" or "flexOnly" (the tail of an exchange venue's day,
// after meal exchanges stop).
class TimelineModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        StatusRole,
        LiveRole,
        SegmentsRole,
    };

    struct Row
    {
        QString name;
        QString status;
        bool live = false;
        QVariantList segments;

        bool operator==(const Row &) const = default;
    };

    using QAbstractListModel::QAbstractListModel;

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void replace(const QList<Row> &rows);

private:
    QList<Row> m_rows;
};

class HoursViewModel : public QObject
{
    Q_OBJECT

    // Something is open now. When nothing is, the hero says so and leads with
    // what opens next.
    Q_PROPERTY(bool anyOpen READ anyOpen NOTIFY changed)
    // The place the hero leads with: The Commons when it is serving, else the
    // open place that stays open longest.
    Q_PROPERTY(QString openName READ openName NOTIFY changed)
    // "until 7:00 PM".
    Q_PROPERTY(QString openUntil READ openUntil NOTIFY changed)
    // Everything else open now.
    Q_PROPERTY(QStringList alsoOpen READ alsoOpen NOTIFY changed)
    // "Opening at 10:30 AM", or "" when nothing opens again today.
    Q_PROPERTY(QString nextOpeningText READ nextOpeningText NOTIFY changed)
    // "in 48 min".
    Q_PROPERTY(QString nextOpeningIn READ nextOpeningIn NOTIFY changed)
    // Every place opening at that time — The Commons by its sitting
    // ("Commons lunch").
    Q_PROPERTY(QStringList nextOpeningNames READ nextOpeningNames NOTIFY changed)

    Q_PROPERTY(QObject *timeline READ timeline CONSTANT)
    // Where "now" sits on the timeline, 0–1, or -1 when the day shown is not
    // today or it is before 7 AM.
    Q_PROPERTY(double nowFraction READ nowFraction NOTIFY changed)
    // "9:42".
    Q_PROPERTY(QString nowText READ nowText NOTIFY changed)
    // [{left, label}] along the axis.
    Q_PROPERTY(QVariantList ticks READ ticks CONSTANT)

    // 0 Mon–Fri, 1 Sat, 2 Sun — the dining page's columns.
    Q_PROPERTY(int dayType READ dayType NOTIFY changed)
    Q_PROPERTY(int todayType READ todayType NOTIFY changed)
    Q_PROPERTY(bool showingToday READ showingToday NOTIFY changed)
    Q_PROPERTY(QStringList dayTypeLabels READ dayTypeLabels CONSTANT)
    // "Weekday hours" / "Saturday hours" / "Sunday hours", for today.
    Q_PROPERTY(QString todayTypeText READ todayTypeText NOTIFY changed)

    // [{name, text, current}] — the card scans once in each of these.
    Q_PROPERTY(QVariantList swipePeriods READ swipePeriods NOTIFY changed)

public:
    explicit HoursViewModel(QObject *parent = nullptr);

    bool anyOpen() const;
    QString openName() const;
    QString openUntil() const;
    QStringList alsoOpen() const;
    QString nextOpeningText() const;
    QString nextOpeningIn() const;
    QStringList nextOpeningNames() const;

    QObject *timeline() { return &m_timeline; }
    double nowFraction() const;
    QString nowText() const;
    QVariantList ticks() const;

    int dayType() const { return m_dayType; }
    int todayType() const;
    bool showingToday() const { return m_dayType == todayType(); }
    QStringList dayTypeLabels() const;
    QString todayTypeText() const;
    QVariantList swipePeriods() const;

    // Every dining place open at `now`, with when it closes, closing soonest
    // first — for Today's "Still open" card.
    struct OpenPlace
    {
        QString name;
        QString note;
        QDateTime closes;
    };
    static QList<OpenPlace> openDiningPlaces(const QDateTime &now);

    // The clock, as a seam for tests.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

public slots:
    void setDayType(int type);
    // Re-read the clock. Named to match the other viewmodels.
    void refreshAll();

signals:
    void changed();

private:
    struct Place
    {
        QString name;
        QString note;
        hours::Schedule schedule;
    };
    static QList<Place> places();
    void rebuild();

    QDateTime m_now;
    int m_dayType = 0;
    bool m_followToday = true;
    TimelineModel m_timeline;
};

} // namespace mycu
