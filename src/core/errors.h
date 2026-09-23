// Exception types shared by the core and the Qt layer.
//
// Kept in the core (and therefore GUI-free) so that providers can throw them
// and viewmodels can catch them without either side depending on the other's
// world.
//
// The control flow here is exception-driven on purpose, the same shape the app
// has always had: SessionExpired goes to the login controller, ParseError and
// TransportError become different copy in the UI. Each viewmodel's failure
// handler is a catch ladder over exactly these types.

#pragma once

#include <QString>

#include <stdexcept>

namespace mycu {

// Base class for every error this app throws deliberately.
class MycuError : public std::runtime_error
{
public:
    explicit MycuError(const QString &message)
        : std::runtime_error(message.toStdString())
    {}

    QString message() const { return QString::fromUtf8(what()); }
};

// The Ellucian session is no longer valid; the user must log in again.
//
// This is *detected, never predicted*. We do not track cookie lifetimes or
// guess at Entra ID's session policy — we make the request, look at where we
// landed, and if it looks like an identity provider instead of Self-Service,
// we throw this. The viewmodel's response is always the same: reopen the login
// surface, then retry the request once.
//
// See looksLikeLogin() in transport.h for the detection rules.
class SessionExpired : public MycuError
{
public:
    using MycuError::MycuError;
};

// The request could not be completed for a reason other than auth.
//
// Network down, WebView not ready, JavaScript threw, timeout waiting for the
// in-page fetch to deposit its result. Distinct from SessionExpired because the
// remedy is different: retry or surface the error, do not bounce the user to a
// login page.
class TransportError : public MycuError
{
public:
    using MycuError::MycuError;
};

// The response arrived but did not have the shape we expect.
//
// Almost always means Ellucian changed the page. `scripts/check-live` exists to
// tell you that quickly instead of leaving you guessing.
class ParseError : public MycuError
{
public:
    using MycuError::MycuError;
};

// The message of whatever exception `error` holds, for log lines and UI copy.
// The C++ spelling of Python's `str(exc)`.
QString describe(std::exception_ptr error);

} // namespace mycu
