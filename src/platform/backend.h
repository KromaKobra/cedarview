// The interface a web backend must satisfy — the entire Android-specific
// surface of the app. Everything else (core, viewmodels, QML) is shared.
//
//   Backend    Web engine                         Initialisation
//   desktop    QtWebEngine (Chromium, in-proc)    before QGuiApplication
//   android    QtWebView (the system WebView)     before QGuiApplication
//
// Which one is built is decided at compile time (Q_OS_ANDROID in
// CMakeLists.txt picks desktop.cpp or android.cpp), so a desktop binary never
// links QtWebView and the APK never contains QtWebEngine — the platform module
// that does not exist on the other side can never be reached by accident.
//
// Three responsibilities, and nothing else:
//
// 1. Initialise the web engine at the right point in startup (beforeApp /
//    afterApp).
// 2. Say which QML file provides the web surface (surfaceQml) — the files
//    expose an identical interface, so no other QML differs.
// 3. Configure persistence, where the platform lets us.

#pragma once

#include <QString>

class QObject;

namespace mycu {

class WebBackend
{
public:
    virtual ~WebBackend() = default;

    // "desktop" or "android".
    virtual QString name() const = 0;

    // The QML file, next to Main.qml, implementing the web surface.
    virtual QString surfaceQml() const = 0;

    // Run before QGuiApplication is constructed.
    virtual void beforeApp() {}

    // Run after QGuiApplication exists, before the QML engine loads.
    virtual void afterApp() {}

    // Point the browser's persistent storage at `storageDir`.
    //
    // Desktop honours this so the session survives a relaunch. Android cannot:
    // the system WebView owns its cookie jar and keeps it in app-private
    // storage already, which is the behaviour we wanted anyway.
    virtual void configureProfile(const QString &storageDir) { Q_UNUSED(storageDir) }

    // The web profile the QML surface should use, or null for its default.
    // Exposed to QML as `webProfile`.
    virtual QObject *qmlProfile() const { return nullptr; }

    // Release web-engine objects once the QML engine is gone.
    virtual void shutdown() {}

    // Drop all cookies, forcing a fresh login. Used by "Sign out".
    virtual void clearCookies() = 0;
};

// The backend for the platform this binary was built for. Defined in
// desktop.cpp or android.cpp — exactly one of them is compiled.
WebBackend *createBackend();

} // namespace mycu
