// The Today screen: what matters now, then the rest of the day.
//
// Not a source of its own. It reads the chapel schedule and skips (chapel),
// the menus (dining), the hours tables (core/hours.h) and the curfew rule
// (curfew), and decides by the clock what leads:
//
//   chapel   on a chapel day, until that chapel ends
//   curfew   from 8 PM, or once The Commons has closed for the day
//   meal     otherwise — the next sitting at The Commons
//
// Below the hero is everything else still to come today, in time order; at
// night, the places still open and tomorrow morning instead.
//
// Exposed to QML as the context property `today`.

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>

#include <functional>

namespace mycu {

class ChapelViewModel;
class CurfewViewModel;
class DiningViewModel;

// One line of a day's timeline.
struct AgendaRow
{
    QDateTime at;
    // "10:30" and "AM", set apart because the design sets them apart.
    QString time, meridiem;
    QString title;
    // "in 48 min" on the next thing only.
    QString chip;
    QString detail;
    // "Garden Bites: Broccoli Alfredo" under a meal.
    QString extra;
    // "meal" | "chapel" | "curfew"
    QString kind;
    // The dot's colour: "gold" for what is next, "cedar" for later meals,
    // "faint" for the rest.
    QString accent;
    // Where tapping goes; see SearchResultModel::Row.
    int tab = 0;
    int diningSection = -1;
    QString menuMeal;

    bool operator==(const AgendaRow &) const = default;
};

class AgendaModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        TimeRole = Qt::UserRole + 1,
        MeridiemRole,
        TitleRole,
        ChipRole,
        DetailRole,
        ExtraRole,
        KindRole,
        AccentRole,
        TabRole,
        DiningSectionRole,
        MenuMealRole,
        LastRole,
    };

    using QAbstractListModel::QAbstractListModel;

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Resets only when the rows changed: this is rebuilt on every tick.
    void replace(const QList<AgendaRow> &rows);
    const QList<AgendaRow> &rows() const { return m_rows; }

private:
    QList<AgendaRow> m_rows;
};

class TodayViewModel : public QObject
{
    Q_OBJECT

    // "chapel" | "meal" | "curfew".
    Q_PROPERTY(QString heroKind READ heroKind NOTIFY changed)
    // "Thursday, Sep 17".
    Q_PROPERTY(QString dateText READ dateText NOTIFY changed)
    // "Now 9:42 AM".
    Q_PROPERTY(QString nowText READ nowText NOTIFY changed)

    // ---- The chapel hero: today's chapel, also while it is on
    Q_PROPERTY(QString chapelSpeaker READ chapelSpeaker NOTIFY changed)
    Q_PROPERTY(QString chapelDescription READ chapelDescription NOTIFY changed)
    // "10:00 AM".
    Q_PROPERTY(QString chapelTime READ chapelTime NOTIFY changed)
    // "Thu, Sep 17".
    Q_PROPERTY(QString chapelDateText READ chapelDateText NOTIFY changed)
    // "in 18 min" / "now".
    Q_PROPERTY(QString chapelCountdown READ chapelCountdown NOTIFY changed)
    Q_PROPERTY(QDateTime chapelStartsAt READ chapelStartsAt NOTIFY changed)
    Q_PROPERTY(bool chapelLivestream READ chapelLivestream NOTIFY changed)
    Q_PROPERTY(QString chapelYoutubeId READ chapelYoutubeId NOTIFY changed)

    // ---- The meal hero: the next sitting at The Commons
    // "Lunch" / "Hot breakfast".
    Q_PROPERTY(QString mealLabel READ mealLabel NOTIFY changed)
    // "Opens in 48 min" / "Serving now" / "Opens at 4:30 PM".
    Q_PROPERTY(QString mealStatus READ mealStatus NOTIFY changed)
    // Serving now, or opening within the hour.
    Q_PROPERTY(bool mealLive READ mealLive NOTIFY changed)
    // "10:30 AM – 2:30 PM".
    Q_PROPERTY(QString mealHoursText READ mealHoursText NOTIFY changed)
    // Home Cooking's dishes at that sitting, as far as the menu says.
    Q_PROPERTY(QStringList mealItems READ mealItems NOTIFY changed)
    // "Garden Bites: Broccoli Alfredo".
    Q_PROPERTY(QString mealExtra READ mealExtra NOTIFY changed)
    // "breakfast" / "lunch" / "dinner", for opening the menu at it.
    Q_PROPERTY(QString mealSlot READ mealSlot NOTIFY changed)

    Q_PROPERTY(QObject *agenda READ agenda CONSTANT)
    Q_PROPERTY(bool hasAgenda READ hasAgenda NOTIFY changed)
    // Night mode: up to three places still open, closing soonest first —
    // [{name, detail, closesIn, soon, kind}].
    Q_PROPERTY(QVariantList stillOpen READ stillOpen NOTIFY changed)
    // Night mode: tomorrow's first sitting and chapel, as agenda rows.
    Q_PROPERTY(QVariantList tomorrow READ tomorrow NOTIFY changed)
    // "Thu, Sep 17".
    Q_PROPERTY(QString tomorrowDateText READ tomorrowDateText NOTIFY changed)
    // "Tomorrow morning" — or "This morning" once it is past midnight.
    Q_PROPERTY(QString morningTitle READ morningTitle NOTIFY changed)

public:
    TodayViewModel(ChapelViewModel *chapel, DiningViewModel *dining, CurfewViewModel *curfew,
                   QObject *parent = nullptr);

    QString heroKind() const { return m_heroKind; }
    QString dateText() const;
    QString nowText() const;

    QString chapelSpeaker() const { return m_chapelSpeaker; }
    QString chapelDescription() const { return m_chapelDescription; }
    QString chapelTime() const { return m_chapelTime; }
    QString chapelDateText() const;
    QString chapelCountdown() const { return m_chapelCountdown; }
    QDateTime chapelStartsAt() const { return m_chapelStartsAt; }
    bool chapelLivestream() const { return m_chapelLivestream; }
    QString chapelYoutubeId() const { return m_chapelYoutubeId; }

    QString mealLabel() const { return m_mealLabel; }
    QString mealStatus() const { return m_mealStatus; }
    bool mealLive() const { return m_mealLive; }
    QString mealHoursText() const { return m_mealHoursText; }
    QStringList mealItems() const { return m_mealItems; }
    QString mealExtra() const { return m_mealExtra; }
    QString mealSlot() const { return m_mealSlot; }

    QObject *agenda() { return &m_agenda; }
    bool hasAgenda() const { return m_agenda.rowCount() > 0; }
    QVariantList stillOpen() const { return m_stillOpen; }
    QVariantList tomorrow() const { return m_tomorrow; }
    QString tomorrowDateText() const;
    QString morningTitle() const;

    // The clock, as a seam for tests.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

public slots:
    // Re-read the clock and everything it decides.
    void refreshAll();

signals:
    void changed();

private:
    void rebuild();
    // Everything rebuild() sets that QML reads, for telling whether it moved.
    QVariantList snapshot() const;
    // The morning night mode looks ahead to: tomorrow's, or today's once it is
    // past midnight.
    QDate nextMorning() const;

    QPointer<ChapelViewModel> m_chapel;
    QPointer<DiningViewModel> m_dining;
    QPointer<CurfewViewModel> m_curfew;

    QDateTime m_now;
    QString m_heroKind = QStringLiteral("meal");

    QString m_chapelSpeaker, m_chapelDescription, m_chapelTime, m_chapelCountdown, m_chapelYoutubeId;
    QDateTime m_chapelStartsAt;
    bool m_chapelLivestream = false;

    QString m_mealLabel, m_mealStatus, m_mealHoursText, m_mealExtra, m_mealSlot;
    QStringList m_mealItems;
    bool m_mealLive = false;

    AgendaModel m_agenda;
    QVariantList m_stillOpen;
    QVariantList m_tomorrow;
    QVariantList m_published;
};

} // namespace mycu
