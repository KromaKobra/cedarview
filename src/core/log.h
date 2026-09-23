// Logging categories, all under `mycu.*`.
//
// `cedarview -v` turns on their debug output; otherwise info and above is
// shown, which is what the app has always logged at. On Android, Qt already
// routes these to logcat, so `adb logcat | grep mycu` works with no extra
// plumbing.

#pragma once

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcTransport)
Q_DECLARE_LOGGING_CATEGORY(lcSession)
Q_DECLARE_LOGGING_CATEGORY(lcChapel)
Q_DECLARE_LOGGING_CATEGORY(lcSchedule)
Q_DECLARE_LOGGING_CATEGORY(lcDining)
Q_DECLARE_LOGGING_CATEGORY(lcMeals)
Q_DECLARE_LOGGING_CATEGORY(lcLogin)
Q_DECLARE_LOGGING_CATEGORY(lcTasks)
Q_DECLARE_LOGGING_CATEGORY(lcSettings)
Q_DECLARE_LOGGING_CATEGORY(lcWebView)
Q_DECLARE_LOGGING_CATEGORY(lcSemester)
Q_DECLARE_LOGGING_CATEGORY(lcPlatform)
