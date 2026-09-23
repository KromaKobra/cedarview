#include "log.h"

#include "errors.h"

// QtInfoMsg and up by default; `-v` flips debug on with a filter rule.
Q_LOGGING_CATEGORY(lcApp, "mycu", QtInfoMsg)
Q_LOGGING_CATEGORY(lcTransport, "mycu.transport", QtInfoMsg)
Q_LOGGING_CATEGORY(lcSession, "mycu.session", QtInfoMsg)
Q_LOGGING_CATEGORY(lcChapel, "mycu.chapel", QtInfoMsg)
Q_LOGGING_CATEGORY(lcSchedule, "mycu.chapel_schedule", QtInfoMsg)
Q_LOGGING_CATEGORY(lcDining, "mycu.dining", QtInfoMsg)
Q_LOGGING_CATEGORY(lcMeals, "mycu.meals", QtInfoMsg)
Q_LOGGING_CATEGORY(lcLogin, "mycu.login", QtInfoMsg)
Q_LOGGING_CATEGORY(lcTasks, "mycu.tasks", QtInfoMsg)
Q_LOGGING_CATEGORY(lcSettings, "mycu.settings", QtInfoMsg)
Q_LOGGING_CATEGORY(lcWebView, "mycu.webview", QtInfoMsg)
Q_LOGGING_CATEGORY(lcSemester, "mycu.semester", QtInfoMsg)
Q_LOGGING_CATEGORY(lcPlatform, "mycu.platform", QtInfoMsg)

namespace mycu {

QString describe(std::exception_ptr error)
{
    if (!error)
        return {};
    try {
        std::rethrow_exception(error);
    } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
    } catch (...) {
        return QStringLiteral("unknown exception");
    }
}

} // namespace mycu
