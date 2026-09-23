// Desktop backend: QtWebEngine (Chromium, in-process).
//
// Unlike Android, this backend *can* see cookies, including HttpOnly ones,
// through QWebEngineCookieStore. We deliberately do not use that: the
// transport makes its request from inside the page on both platforms so there
// is exactly one code path to debug. The cookie store is used only to clear
// cookies on sign-out.

#include "backend.h"

#include "core/log.h"

#include <QDir>
#include <QQuickWebEngineProfile>
#include <QWebEngineCookieStore>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

namespace mycu {

namespace {

class DesktopBackend final : public WebBackend
{
public:
    QString name() const override { return QStringLiteral("desktop"); }
    QString surfaceQml() const override { return QStringLiteral("WebSurfaceDesktop.qml"); }

    // Initialise Chromium before the application object exists. QtWebEngine
    // spins up its own process infrastructure and must do so before
    // QGuiApplication; calling this late is a hard crash, not an error.
    void beforeApp() override
    {
        QtWebEngineQuick::initialize();
        qCDebug(lcPlatform) << "QtWebEngineQuick initialised";
    }

    // Persist cookies under `storageDir` so the login survives restarts.
    //
    // This has to be a profile of our own. In Qt 6 the default profile is
    // off-the-record, and an off-the-record profile silently ignores
    // setPersistentStoragePath and forces NoPersistentCookies — so configuring
    // the default one looks right and persists nothing. Every launch would
    // then start at the Microsoft sign-in page and trigger a fresh MFA prompt.
    //
    // The profile is handed to QML as `webProfile` and bound by
    // WebSurfaceDesktop.qml. Must run after QGuiApplication exists and before
    // the QML engine loads.
    void configureProfile(const QString &storageDir) override
    {
        if (!QDir(storageDir).exists()) {
            QDir().mkpath(storageDir);
            QFile::setPermissions(storageDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }

        // Order matters: setting the storage name resets the paths to their
        // defaults, and it does not by itself clear the off-the-record flag.
        // "mycu" is the name the profile has always had, so an existing
        // sign-in survives the move to C++.
        m_profile = new QQuickWebEngineProfile;
        m_profile->setStorageName(QStringLiteral("mycu"));
        m_profile->setOffTheRecord(false);
        m_profile->setPersistentStoragePath(storageDir);
        m_profile->setCachePath(QDir(storageDir).filePath(QStringLiteral("cache")));
        m_profile->setPersistentCookiesPolicy(QQuickWebEngineProfile::ForcePersistentCookies);
        qCInfo(lcPlatform).noquote() << "web profile persisted at" << storageDir;
        if (m_profile->isOffTheRecord())
            qCWarning(lcPlatform) << "the web profile is off the record; the sign-in will not persist";
    }

    QObject *qmlProfile() const override { return m_profile; }

    // Only safe after every view using the profile is destroyed.
    void shutdown() override
    {
        delete m_profile;
        m_profile = nullptr;
    }

    void clearCookies() override
    {
        if (!m_profile)
            return;
        m_profile->cookieStore()->deleteAllCookies();
        qCInfo(lcPlatform) << "desktop cookies cleared";
    }

private:
    QQuickWebEngineProfile *m_profile = nullptr;
};

} // namespace

WebBackend *createBackend()
{
    return new DesktopBackend;
}

} // namespace mycu
