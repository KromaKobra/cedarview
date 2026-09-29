// Buildings tab: tonight's curfew, and the countdown to it in the last hour.
//
// Like SemesterViewModel, this wraps a rule from the core (core/curfew.h) and a
// clock, not a provider: there is nothing to fetch. It is a viewmodel rather
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

public:
    explicit CurfewViewModel(QObject *parent = nullptr);

    QString timeText() const;
    QString nightText() const;
    bool lateNight() const;
    QString countdownText() const;

    // The clock, as a seam for tests.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

public slots:
    // Re-read the clock. Named to match the other viewmodels; the Buildings tab
    // calls it on a timer while it is showing, every second during the
    // countdown.
    void refreshAll();

signals:
    void changed();

private:
    QDateTime m_now;
};

} // namespace mycu
