// Summary screen: how much of the semester is left.
//
// The odd one out among the viewmodels. Every other one wraps a provider, a
// worker thread and an error path; this one wraps core/calendar.h and a clock.
// There is no refresh() and no busy — there is nothing to fetch, because
// Cedarville does not publish term boundaries anywhere the app can read (see
// the header of core/calendar.h).
//
// It is still a viewmodel rather than a few expressions in QML, for two
// reasons: the date formatting belongs with the rest of the app's date
// formatting, and tst_qml_contract only guards bindings whose backing object
// is a registered viewmodel. Free-floating JS in the QML would be checked by
// nothing.
//
// Exposed to QML as the context property `semester`.

#pragma once

#include <QDate>
#include <QObject>

#include <functional>

namespace mycu {

class SemesterViewModel : public QObject
{
    Q_OBJECT

    // False over the summer and the Christmas break.
    //
    // The QML reads this to swap the whole card between a countdown and a
    // "Spring starts …" line, rather than rendering a confident zero.
    Q_PROPERTY(bool inTerm READ inTerm NOTIFY changed)
    // "Fall" / "Spring", or "" between terms.
    Q_PROPERTY(QString termName READ termName NOTIFY changed)
    // Days to the last day of term, or -1 when there is no term.
    //
    // -1 rather than 0 for the same reason the chapel figures use it: QML has
    // no null int, and a zero here would read as "the semester ends today".
    Q_PROPERTY(int daysLeft READ daysLeft NOTIFY changed)
    // The countdown's starting figure — the "/ 114" — or -1 between terms.
    Q_PROPERTY(int totalDays READ totalDays NOTIFY changed)
    // 0.0–1.0 of the term completed, for MeterBar. Zero between terms, where
    // the bar hides.
    Q_PROPERTY(double elapsedFraction READ elapsedFraction NOTIFY changed)
    // "ends Fri, Dec 11" — the date the countdown is counting to.
    Q_PROPERTY(QString endDateText READ endDateText NOTIFY changed)
    // "Spring starts Mon, Jan 5" — the between-terms line, else "".
    Q_PROPERTY(QString nextTermText READ nextTermText NOTIFY changed)

public:
    explicit SemesterViewModel(QObject *parent = nullptr);

    bool inTerm() const;
    QString termName() const;
    int daysLeft() const;
    int totalDays() const;
    double elapsedFraction() const;
    QString endDateText() const;
    QString nextTermText() const;

    // The clock, as a seam for tests.
    std::function<QDate()> today = [] { return QDate::currentDate(); };

public slots:
    // Re-read the clock.
    //
    // Named to match the other viewmodels so the refresh gesture can call it
    // without a special case. It exists because the app is left running
    // overnight on a phone more often than it is restarted, and a countdown
    // that is a day stale is a countdown that is wrong.
    void refreshAll();

signals:
    void changed();

private:
    QDate m_today;
};

} // namespace mycu
