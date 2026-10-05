// The Campus tab's buildings: which are open, when each closes, and the ones
// you starred.
//
// The hours are hand-entered (core/hours.h); this wraps them and a clock, like
// HoursViewModel. Buildings are grouped by when they close, soonest first, so
// the question the tab answers at 10:48 PM — "what is still open, and for how
// long?" — is answered by the first group. Closed buildings come last.
//
// Exposed to QML as the context property `campus`.

#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>

#include <functional>

namespace mycu {

class SettingsController;

// A building counts as "closing soon" this long before it closes.
inline constexpr qint64 CLOSING_SOON_SECS = 30 * 60;

class CampusViewModel : public QObject
{
    Q_OBJECT

    // [{title, badge, closed, buildings: [building]}] — open buildings by
    // closing time, then the closed ones. A building is {name, code, note,
    // isOpen, closesText, closesIn, closingSoon, favorite}.
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    // The starred buildings, as above, in the order they were starred.
    Q_PROPERTY(QVariantList favoriteBuildings READ favoriteBuildings NOTIFY changed)
    // 0 everything, 1 open now, 2 closing soon, 3 closed.
    Q_PROPERTY(int filter READ filter NOTIFY changed)
    Q_PROPERTY(int openCount READ openCount NOTIFY changed)
    Q_PROPERTY(int closingSoonCount READ closingSoonCount NOTIFY changed)
    Q_PROPERTY(int closedCount READ closedCount NOTIFY changed)
    Q_PROPERTY(QString query READ query NOTIFY changed)
    // "Search 19 buildings".
    Q_PROPERTY(QString searchPlaceholder READ searchPlaceholder CONSTANT)

public:
    explicit CampusViewModel(QObject *parent = nullptr);

    // Stars are remembered through `settings`.
    void attachSettings(SettingsController *settings);

    QVariantList groups() const;
    QVariantList favoriteBuildings() const;
    int filter() const { return m_filter; }
    int openCount() const;
    int closingSoonCount() const;
    int closedCount() const;
    QString query() const { return m_query; }
    QString searchPlaceholder() const;

    // The clock, as a seam for tests.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

public slots:
    // Picking the filter that is on turns it off.
    void setFilter(int filter);
    void setQuery(const QString &query);
    void toggleFavorite(const QString &name);
    bool isFavorite(const QString &name) const;
    // Re-read the clock. Named to match the other viewmodels.
    void refreshAll();

signals:
    void changed();

private:
    struct Entry
    {
        QString name, code, note;
        bool isOpen = false;
        bool closingSoon = false;
        QDateTime closes;
        QDateTime opens;
    };
    QList<Entry> entries() const;
    QVariantMap describe(const Entry &entry) const;
    bool matches(const Entry &entry) const;

    QDateTime m_now;
    int m_filter = 0;
    QString m_query;
    QStringList m_favorites;
    QPointer<SettingsController> m_settings;
};

} // namespace mycu
