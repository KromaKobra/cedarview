#include "jsonfile.h"

#include "log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

namespace mycu {

void ensurePrivateDir(const QString &dir)
{
    if (QDir(dir).exists())
        return;
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

bool atomicWriteJson(const QString &path, const QJsonObject &object, QJsonDocument::JsonFormat format)
{
    ensurePrivateDir(QFileInfo(path).absolutePath());

    const QString tmp = path + QStringLiteral(".tmp");
    {
        QFile file(tmp);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcSession) << "could not write" << tmp << ":" << file.errorString();
            return false;
        }
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        if (file.write(QJsonDocument(object).toJson(format)) < 0) {
            qCWarning(lcSession) << "could not write" << tmp << ":" << file.errorString();
            return false;
        }
    }
    QFile::setPermissions(tmp, QFile::ReadOwner | QFile::WriteOwner);

    // rename(2) replaces the target atomically; QFile::rename refuses to.
    if (std::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(path).constData()) != 0) {
        qCWarning(lcSession) << "could not replace" << path;
        QFile::remove(tmp);
        return false;
    }
    return true;
}

std::optional<QJsonObject> readJsonObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    return doc.object();
}

} // namespace mycu
