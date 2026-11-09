// Dining › Menu › The Commons Express: its menu, by category, and where to
// order.
//
// A viewmodel of its own rather than more of DiningViewModel: it is a
// different service (core/providers/express.h) with no days, sittings or
// allergens, and nothing it shows depends on The Commons' menu.
//
// Exposed to QML as the context property `express`.

#pragma once

#include "core/providers/express.h"
#include "core/transport.h"
#include "sourcestatus.h"
#include "storage.h"

#include <QDateTime>
#include <QJsonValue>
#include <QObject>
#include <QVariantList>

#include <exception>
#include <functional>
#include <optional>

namespace mycu {

// How long the Commons Express menu counts as fresh, in seconds. It changes
// when someone edits the storefront, which is rarely.
inline constexpr qint64 EXPRESS_FRESH_SECS = 60 * 60;

class ExpressViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject *menuStatus READ menuStatus CONSTANT)
    // [{name, items: [{name, price, description}]}], in the storefront's
    // order, empty categories left out.
    Q_PROPERTY(QVariantList categories READ categories NOTIFY changed)
    Q_PROPERTY(QString orderUrl READ orderUrl CONSTANT)
    Q_PROPERTY(bool open READ open NOTIFY clockChanged)
    // "Open until 11:00 PM" / "Opens 8:00 PM" / "Opens tomorrow 8:00 PM" /
    // "Opens Wed 1:00 PM", or "" with no hours at all.
    Q_PROPERTY(QString hoursStatus READ hoursStatus NOTIFY clockChanged)
    // Monday first: [{day: "Mon", hours: "1 PM–4 PM, 8 PM–11 PM" | "Closed",
    // today}].
    Q_PROPERTY(QVariantList weekHours READ weekHours NOTIFY clockChanged)
    // The storefront's own hours, or (false) the posted ones in core/hours.h
    // because the storefront's could not be had.
    Q_PROPERTY(bool liveHours READ liveHours NOTIFY changed)

public:
    explicit ExpressViewModel(TransportPtr transport, std::optional<Storage> storage = std::nullopt,
                              QObject *parent = nullptr);

    SourceStatus *menuStatus() { return &m_status; }
    QVariantList categories() const;
    QString orderUrl() const { return EXPRESS_ORDER_URL; }
    bool open() const;
    QString hoursStatus() const;
    QVariantList weekHours() const;
    bool liveHours() const { return !m_menu.hours.isEmpty(); }

    const ExpressMenu &menu() const { return m_menu; }
    // What open, hoursStatus and weekHours read.
    hours::Schedule schedule() const;

    // Sample-data preview: drop what is shown, stop saving, and read the
    // fixtures from then on. Off again: back to what was saved.
    void setPreview(bool on);

    // The clock, as a seam for tests.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

    // How a fetch is run, as a seam for tests: in the background in the app.
    using Done = std::function<void(const ExpressMenu &, const QJsonValue &)>;
    using Failed = std::function<void(std::exception_ptr)>;
    std::function<void(ExpressProvider, Done, Failed)> fetchMenu;

public slots:
    // Fetch the menu, unless a fetch is already running.
    void refresh();
    // Re-read the clock. Named to match the other viewmodels; SyncCoordinator
    // calls it on its clock tick.
    void refreshAll();

signals:
    void changed();
    // Only the clock moved: the hours' wording, not the menu.
    void clockChanged();

private:
    bool persisting() const { return m_storage.has_value() && !m_preview; }
    void hydrate();
    void onLoaded(const ExpressMenu &menu);
    void onFailed(std::exception_ptr error);

    TransportPtr m_transport;
    std::optional<Storage> m_storage;
    ExpressMenu m_menu;
    SourceStatus m_status{EXPRESS_FRESH_SECS};
    bool m_busy = false;
    bool m_preview = false;
    // Bumped by setPreview, so a fetch that started on the other side is
    // dropped when it lands.
    int m_generation = 0;
};

} // namespace mycu
