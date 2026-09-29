#include "android_urlprobe.h"

#include <QFuture>
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

namespace mycu {

void AndroidUrlProbe::probe()
{
    if (m_pending)
        return;
    m_pending = true;

    // Never waited on, so a busy UI thread (it is also rendering the page)
    // cannot stall the Qt one.
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        const QJniObject url = QJniObject::callStaticObjectMethod(
            "com/kromakobra/cedarview/WebViewUrl", "current",
            "(Landroid/content/Context;)Ljava/lang/String;", context.object());
        QJniEnvironment env;
        if (env.checkAndClearExceptions() || !url.isValid())
            return QVariant(QString());
        return QVariant(url.toString());
    }).then(this, [this](const QVariant &url) {
        m_pending = false;
        emit observed(url.toString());
    });
}

} // namespace mycu
