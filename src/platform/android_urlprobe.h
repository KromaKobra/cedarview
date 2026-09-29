// The address the sign-in WebView is really on, as soon as it gets there.
//
// QtWebView reports a page that was reached by a chain of redirects and form
// posts only once that page has finished loading, so without this the
// Self-Service dashboard sits on screen for seconds after a sign-in before the
// app notices it and hides it. See android/src/.../WebViewUrl.java for why.
//
// Exposed to QML as `urlProbe`, on Android only. WebSurfaceAndroid.qml polls
// it while the surface is showing and feeds what it reports into currentUrl,
// the same property the load signals write to.

#pragma once

#include <QObject>

namespace mycu {

class AndroidUrlProbe : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    // Read the WebView's address on the Android UI thread. The answer arrives
    // later, through observed(). A call made while one is still outstanding is
    // dropped.
    void probe();

signals:
    // Empty when no WebView is on screen.
    void observed(const QString &url);

private:
    bool m_pending = false;
};

} // namespace mycu
