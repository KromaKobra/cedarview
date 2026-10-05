// Tonight's curfew, for the Campus tab and Today's evening: when it falls, how
// long is left, and how far the evening has run.
//
// This wraps a rule from the core (core/curfew.h) and a clock, not a
// provider: there is nothing to fetch. It is a viewmodel rather
// than a few lines of JS in the QML so that tst_qml_contract checks the
// bindings, and so the time formatting matches the rest of the app's.
//
// Exposed to QML as the context property `curfew`.

#pragma once

#include <QDateTime>
#include <QObject>

#include <functional>

namespace mycu {

class CurfewViewModel : public QObject
{
    Q_OBJECT

    // "11:59 PM" / "12:59 AM".
    Q_PROPERTY(QString timeText READ timeText NOTIFY changed)
    // "Friday night" — named after the evening, so it still reads "Friday
    // night" at 12:30 AM on Saturday.
    Q_PROPERTY(QString nightText READ nightText NOTIFY changed)
    // True for Friday and Saturday nights, which run to 12:59 AM.
    Q_PROPERTY(bool lateNight READ lateNight NOTIFY changed)
    // "42:07" in the last hour before curfew, else "". The QML shows the
    // countdown only when this is non-empty.
    Q_PROPERTY(QString countdownText READ countdownText NOTIFY changed)
    // "1h 11m" from 8 PM until curfew, else "" — the hero's figure in the
    // evening, when how long is left matters more than what time it falls.
    Q_PROPERTY(QString timeLeftText READ timeLeftText NOTIFY changed)
    // How far the evening has run, 8 PM to curfew, 0–1; -1 before 8 PM.
    Q_PROPERTY(double eveningFraction READ eveningFraction NOTIFY changed)
    // "10:48" — the marker on the evening bar.
    Q_PROPERTY(QString nowText READ nowText NOTIFY changed)
    // "Sunday–Thursday curfew" / "Friday–Saturday curfew".
    Q_PROPERTY(QString ruleText READ ruleText NOTIFY changed)

public:
    explicit CurfewViewModel(QObject *parent = nullptr);

    QString timeText() const;
    QString nightText() const;
    bool lateNight() const;
    QString countdownText() const;
    QString timeLeftText() const;
    double eveningFraction() const;
    QString nowText() const;
    QString ruleText() const;

    // When the evening the bar measures begins, on the night `now` belongs to.
    QDateTime eveningStarts() const;
    QDateTime curfewAt() const;

    // The clock, as a seam for tests.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

public slots:
    // Re-read the clock. Named to match the other viewmodels; SyncCoordinator
    // calls it on its clock tick.
    void refreshAll();

signals:
    void changed();

private:
    QDateTime m_now;
};

} // namespace mycu
