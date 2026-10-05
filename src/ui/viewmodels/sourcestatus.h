// Where one source of data stands: loaded or not, loading or not, how old,
// and what went wrong.
//
// Every figure on screen comes from one of four sources — the skip ledger,
// the chapel schedule, the menu feed and the meal plan — and each has one of
// these, exposed as `chapel.skipsStatus`, `chapel.scheduleStatus`,
// `dining.menuStatus` and `dining.planStatus`. The QML reads them the same way
// everywhere:
//
//   !hasData && loading   a skeleton where the figure will be
//   hasData               the figure (plus the "Updated …" stamp when it is
//                         stale or came from the on-device cache)
//   !hasData && error     an inline message with Retry
//   needsSignIn           "Sign in to see …"
//
// which is what retires the em dash as the answer to "not loaded yet". The
// dash stays for one thing only: loaded, and the server did not report it
// (the no-confident-zeros rule in docs/architecture.md).
//
// The viewmodel that owns a source drives loading, data and errors. Whether
// the user is signed in is the coordinator's to say (setNeedsSignIn,
// setAwaitingSignIn), since only it sees the login flow.

#pragma once

#include <QDateTime>
#include <QObject>

#include <functional>

namespace mycu {

class SourceStatus : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool hasData READ hasData NOTIFY changed)
    // A fetch is in flight, or one is waiting on a silent sign-in.
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    // What is shown was read from the device at startup, not fetched this run.
    Q_PROPERTY(bool fromCache READ fromCache NOTIFY changed)
    // Older than this source's freshness window.
    Q_PROPERTY(bool stale READ stale NOTIFY changed)
    Q_PROPERTY(QDateTime updatedAt READ updatedAt NOTIFY changed)
    // "Updated 9:41 AM" / "Updated yesterday" / "Updated Sep 14", or "".
    Q_PROPERTY(QString updatedText READ updatedText NOTIFY changed)
    // The same without "Updated" — "9:41 AM" — for a card's corner stamp.
    Q_PROPERTY(QString stampText READ stampText NOTIFY changed)
    // Show the stamp: the figures are a saved copy, or old.
    Q_PROPERTY(bool showStamp READ showStamp NOTIFY changed)
    // Why the last fetch failed, or "". Kept alongside data that is still
    // shown — a failed refresh never blanks what was there.
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // Personal data the user has to sign in to see.
    Q_PROPERTY(bool needsSignIn READ needsSignIn NOTIFY changed)

public:
    // `freshSecs`: how long a fetch counts as fresh.
    explicit SourceStatus(qint64 freshSecs, QObject *parent = nullptr);

    bool hasData() const { return m_hasData; }
    bool loading() const { return m_loading || m_awaitingSignIn; }
    bool fromCache() const { return m_fromCache; }
    bool stale() const;
    QDateTime updatedAt() const { return m_updatedAt; }
    QString updatedText() const;
    QString stampText() const;
    bool showStamp() const { return m_hasData && (m_fromCache || stale()); }
    QString error() const { return m_error; }
    bool needsSignIn() const { return m_needsSignIn && !loading(); }

    // Whether the last failure was the network rather than the server: no
    // answer arrived at all. What "you're offline" is built on.
    bool failedOffline() const { return !m_error.isEmpty() && m_offline; }
    bool fetching() const { return m_loading; }
    // Seconds since the shown data was fetched; -1 with none.
    qint64 ageSecs() const;
    qint64 freshSecs() const { return m_freshSecs; }

    // "Updated 9:41 AM" for `at`, relative to `now`.
    static QString describeAge(const QDateTime &at, const QDateTime &now);

    // The clock, as a seam for tests.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

    // ---- Driven by the owning viewmodel
    void begin();
    void succeeded(const QDateTime &at);
    // Data read back from the device, saved at `savedAt`.
    void restored(const QDateTime &savedAt);
    // A fetch failed; whatever data there was stays.
    void failed(const QString &message, bool offline);
    // A fetch stopped without an error to show (an expired session, which the
    // login flow answers by itself).
    void stopped();
    // Nothing loaded, nothing loading — after sign-out, or switching mode.
    void reset();

    // ---- Driven by the coordinator
    void setNeedsSignIn(bool value);
    void setAwaitingSignIn(bool value);

public slots:
    // Re-read the clock: `stale` and the stamp's wording move with it.
    void tick();

signals:
    void changed();
    // A fetch succeeded — fresh data, not a read-back. For personal sources
    // this is proof of a working session.
    void loaded();

private:
    qint64 m_freshSecs;
    bool m_hasData = false;
    bool m_loading = false;
    bool m_fromCache = false;
    bool m_offline = false;
    bool m_needsSignIn = false;
    bool m_awaitingSignIn = false;
    QDateTime m_updatedAt;
    QString m_error;
};

} // namespace mycu
