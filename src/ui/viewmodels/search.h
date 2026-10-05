// Search over what the app already holds: the menus from today on, the hours
// and building tables, and the chapel schedule. Nothing is fetched to answer
// a query.
//
// Matching is by token prefix, case- and diacritic-insensitive: "jal" finds
// "Pickled Jalapeño", "lib" finds "Centennial Library", "chick fil" finds
// "Chick-fil-A". A query's tokens must all match; a handful of filler words
// ("when", "the", "hours") are dropped first so a typed question still finds
// its subject.
//
// Results come in sections — Places (with whether each is open now), Menu
// (where and when an item is next served) and Chapel (speakers) — under a top
// answer, the single most useful result spelled out. Each carries where
// tapping it should go.
//
// Exposed to QML as the context property `search`.

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

#include <functional>

namespace mycu {

class ChapelViewModel;
class DiningViewModel;
class SettingsController;

namespace search {

// "Jalapeño Poppers!" -> ["jalapeno", "poppers"].
QStringList tokens(const QString &text);

// `query`'s tokens without the filler words.
QStringList queryTokens(const QString &query);

// Whether every one of `query` (already tokenised) is a prefix of some token
// of `text`. An empty query matches nothing.
bool matches(const QStringList &query, const QString &text);

} // namespace search

// The result list, flat: a header row per section, then its results.
class SearchResultModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        RowTypeRole = Qt::UserRole + 1, // "section" | "item"
        SectionRole,                    // "places" | "menu" | "chapel"
        TitleRole,
        DetailRole,
        BadgeRole,
        HotRole,
        IconRole,
        TabRole,
        DiningSectionRole,
        MenuDayRole,
        MenuMealRole,
    };

    struct Row
    {
        QString rowType, section, title, detail, badge, icon;
        bool hot = false;
        // Where tapping goes: a tab (0 Today, 1 Chapel, 2 Dining, 3 Campus),
        // a Dining section (0 Plan, 1 Menu, 2 Hours) or -1, and for a dish
        // the day (from today) and sitting to open the menu at.
        int tab = 0;
        int diningSection = -1;
        int menuDay = 0;
        QString menuMeal;
    };

    using QAbstractListModel::QAbstractListModel;

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void replace(const QList<Row> &rows);

private:
    QList<Row> m_rows;
};

class SearchViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString query READ query NOTIFY changed)
    Q_PROPERTY(QObject *results READ results CONSTANT)
    Q_PROPERTY(bool hasResults READ hasResults NOTIFY changed)
    Q_PROPERTY(bool hasTopAnswer READ hasTopAnswer NOTIFY changed)
    // {kicker, tag, headline, chips, footer, tab, diningSection, menuDay,
    // menuMeal}.
    Q_PROPERTY(QVariantMap topAnswer READ topAnswer NOTIFY changed)
    // 0 everything, 1 menu, 2 places, 3 chapel.
    Q_PROPERTY(int scope READ scope NOTIFY changed)
    Q_PROPERTY(int menuCount READ menuCount NOTIFY changed)
    Q_PROPERTY(int placeCount READ placeCount NOTIFY changed)
    Q_PROPERTY(int chapelCount READ chapelCount NOTIFY changed)
    Q_PROPERTY(QStringList suggestions READ suggestions CONSTANT)
    Q_PROPERTY(QStringList recent READ recent NOTIFY changed)

public:
    SearchViewModel(ChapelViewModel *chapel, DiningViewModel *dining, SettingsController *settings,
                    QObject *parent = nullptr);

    QString query() const { return m_query; }
    QObject *results() { return &m_results; }
    bool hasResults() const { return m_results.rowCount() > 0 || !m_topAnswer.isEmpty(); }
    bool hasTopAnswer() const { return !m_topAnswer.isEmpty(); }
    QVariantMap topAnswer() const { return m_topAnswer; }
    int scope() const { return m_scope; }
    int menuCount() const { return static_cast<int>(m_menu.size()); }
    int placeCount() const { return static_cast<int>(m_places.size()); }
    int chapelCount() const { return static_cast<int>(m_chapels.size()); }
    QStringList suggestions() const;
    QStringList recent() const;

    // The clock, as a seam for tests.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

public slots:
    void setQuery(const QString &query);
    // Picking the scope that is on turns it off.
    void setScope(int scope);
    // The query was acted on — a result tapped, or Enter pressed: remember it.
    void commit();
    void clearRecent();

signals:
    void changed();

private:
    void rebuild();
    void findPlaces(const QStringList &query);
    void findMenu(const QStringList &query);
    void findChapels(const QStringList &query);
    void publish();

    QPointer<ChapelViewModel> m_chapel;
    QPointer<DiningViewModel> m_dining;
    QPointer<SettingsController> m_settings;

    QString m_query;
    int m_scope = 0;
    QList<SearchResultModel::Row> m_places;
    QList<SearchResultModel::Row> m_menu;
    QList<SearchResultModel::Row> m_chapels;
    // Each section's best result, spelled out; the top answer is one of them.
    QVariantMap m_placeAnswer;
    QVariantMap m_menuAnswer;
    QVariantMap m_chapelAnswer;
    // A place matched on its own name, rather than its note — which outranks
    // a dish ("library" is a question about the library).
    bool m_placeByName = false;
    QVariantMap m_topAnswer;
    SearchResultModel m_results;
};

} // namespace mycu
