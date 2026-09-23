// The few odds and ends QML needs that do not belong to a viewmodel.
//
// Exposed to QML as the context property `bridge`.

#pragma once

#include <QObject>
#include <QPointer>

namespace mycu {

class WebViewTransport;

class Bridge : public QObject
{
    Q_OBJECT

    // Where the surface should navigate first.
    //
    // This is also the SAML RelayState value, so after a sign-in the browser
    // lands back here on its own.
    Q_PROPERTY(QString startPath READ startPath CONSTANT)
    Q_PROPERTY(QString platformName READ platformName CONSTANT)
    Q_PROPERTY(QString surfaceQml READ surfaceQml CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    // `transport` is null in demo mode, where there is no browser.
    Bridge(WebViewTransport *transport, QString startPath, QString platformName, QString surfaceQml,
           QObject *parent = nullptr);

    QString startPath() const { return m_startPath; }
    QString platformName() const { return m_platformName; }
    QString surfaceQml() const { return m_surfaceQml; }
    QString version() const;

public slots:
    // Hand the loaded QML surface to the transport.
    //
    // A no-op in demo mode, where the transport is a FixtureTransport and
    // there is no browser.
    void attachSurface(QObject *surface);

private:
    QPointer<WebViewTransport> m_transport;
    QString m_startPath;
    QString m_platformName;
    QString m_surfaceQml;
};

} // namespace mycu
