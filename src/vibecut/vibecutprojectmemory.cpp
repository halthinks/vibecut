/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutprojectmemory.h"

#include "core.h"
#include "doc/kdenlivedoc.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace {
QString pathFor(const QUrl &projectUrl)
{
    if (!projectUrl.isValid() || !projectUrl.isLocalFile() || projectUrl.toLocalFile().isEmpty()) return QString();
    const QFileInfo info(projectUrl.toLocalFile());
    return info.absoluteDir().filePath(VibeCutProjectMemory::fileName());
}

QJsonObject rootFor(const QJsonArray &entries)
{
    return QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("entries"), entries}};
}

bool saveCurrentArray(const QJsonArray &entries, QString *error)
{
    if (error) error->clear();
    if (!pCore || !pCore->currentDoc()) {
        if (error) *error = QStringLiteral("No current project is available.");
        return false;
    }
    const QString path = pathFor(pCore->currentDoc()->url());
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("Project must be saved locally before VibeCut memory can be persisted.");
        return false;
    }
    const QByteArray data = QJsonDocument(rootFor(entries)).toJson(QJsonDocument::Indented);
    if (data.size() > VibeCutProjectMemory::MaxBytes) {
        if (error) *error = QStringLiteral("Project memory would exceed the %1 byte limit.").arg(VibeCutProjectMemory::MaxBytes);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Could not commit %1.").arg(path);
        return false;
    }
    return true;
}
}

QString VibeCutProjectMemory::fileName()
{
    return QStringLiteral(".vibecutmemory.json");
}

QJsonArray VibeCutProjectMemory::loadForProjectUrl(const QUrl &projectUrl, QString *error)
{
    if (error) error->clear();
    const QString path = pathFor(projectUrl);
    if (path.isEmpty()) return QJsonArray();
    QFile file(path);
    if (!file.exists()) return QJsonArray();
    if (file.size() > MaxBytes) {
        if (error) *error = QStringLiteral("%1 exceeds the %2 byte project-memory limit.").arg(path).arg(MaxBytes);
        return QJsonArray();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString());
        return QJsonArray();
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Invalid project memory JSON in %1: %2").arg(path, parseError.errorString());
        return QJsonArray();
    }
    const QJsonArray entries = document.object().value(QStringLiteral("entries")).toArray();
    if (entries.size() > MaxEntries) {
        if (error) *error = QStringLiteral("Project memory contains more than %1 entries.").arg(MaxEntries);
        return QJsonArray();
    }
    return entries;
}

QJsonArray VibeCutProjectMemory::loadCurrent(QString *error)
{
    if (!pCore || !pCore->currentDoc()) {
        if (error) error->clear();
        return QJsonArray();
    }
    return loadForProjectUrl(pCore->currentDoc()->url(), error);
}

QString VibeCutProjectMemory::contextText(QString *error)
{
    const QJsonArray entries = loadCurrent(error);
    QStringList lines;
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const QString text = entry.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty()) continue;
        lines.append(QStringLiteral("- [%1] %2").arg(entry.value(QStringLiteral("source")).toString(QStringLiteral("unknown")), text));
    }
    return lines.join(QLatin1Char('\n'));
}

bool VibeCutProjectMemory::putCurrent(const QString &text, const QString &source, QString *id, QString *error)
{
    if (error) error->clear();
    const QString cleanText = text.trimmed();
    const QString cleanSource = source.trimmed().isEmpty() ? QStringLiteral("agent") : source.trimmed();
    if (cleanText.isEmpty()) {
        if (error) *error = QStringLiteral("Memory text must not be empty.");
        return false;
    }
    if (cleanText.size() > 2048) {
        if (error) *error = QStringLiteral("One project-memory entry may not exceed 2048 characters.");
        return false;
    }

    QJsonArray entries = loadCurrent(error);
    if (error && !error->isEmpty()) return false;
    for (const QJsonValue &value : entries) {
        if (value.toObject().value(QStringLiteral("text")).toString() == cleanText) {
            if (id) *id = value.toObject().value(QStringLiteral("id")).toString();
            return true;
        }
    }
    if (entries.size() >= MaxEntries) {
        entries.removeFirst();
    }
    const QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entries.append(QJsonObject{{QStringLiteral("id"), newId},
                               {QStringLiteral("text"), cleanText},
                               {QStringLiteral("source"), cleanSource},
                               {QStringLiteral("created_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    if (!saveCurrentArray(entries, error)) return false;
    if (id) *id = newId;
    return true;
}

bool VibeCutProjectMemory::forgetCurrent(const QString &id, QString *error)
{
    if (error) error->clear();
    const QString cleanId = id.trimmed();
    if (cleanId.isEmpty()) {
        if (error) *error = QStringLiteral("Memory id must not be empty.");
        return false;
    }
    QJsonArray entries = loadCurrent(error);
    if (error && !error->isEmpty()) return false;
    QJsonArray filtered;
    bool removed = false;
    for (const QJsonValue &value : entries) {
        if (value.toObject().value(QStringLiteral("id")).toString() == cleanId) {
            removed = true;
            continue;
        }
        filtered.append(value);
    }
    if (!removed) {
        if (error) *error = QStringLiteral("Unknown project-memory id: %1").arg(cleanId);
        return false;
    }
    return saveCurrentArray(filtered, error);
}
