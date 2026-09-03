/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutpolicyoverrides.h"

#include "core.h"
#include "doc/kdenlivedoc.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QStringList stringList(const QJsonValue &value)
{
    QStringList result;
    for (const QJsonValue &entry : value.toArray()) {
        const QString name = entry.toString().trimmed();
        if (!name.isEmpty() && !result.contains(name)) result.append(name);
    }
    return result;
}
}

QString VibeCutPolicyOverrides::fileName()
{
    return QStringLiteral(".vibecutpolicy.json");
}

QHash<QString, VibeCutToolPolicy> VibeCutPolicyOverrides::applyObject(const QHash<QString, VibeCutToolPolicy> &basePolicies,
                                                                      const QJsonObject &root)
{
    QHash<QString, VibeCutToolPolicy> result = basePolicies;
    const QStringList deny = stringList(root.value(QStringLiteral("deny")));
    const QStringList confirm = stringList(root.value(QStringLiteral("always_confirm")));
    const QStringList allow = stringList(root.value(QStringLiteral("auto_allow")));

    for (const QString &name : deny) {
        if (result.contains(name)) result[name].enabled = false;
    }

    // Project auto-allow can relax ordinary review policy, but never a
    // code-defined hard confirmation requirement or an irreversible tool.
    for (const QString &name : allow) {
        if (!result.contains(name)) continue;
        const VibeCutToolPolicy base = basePolicies.value(name);
        if (base.confirmationRequired || base.risk == VibeCutToolRisk::Irreversible) continue;
        result[name].autoAllowed = true;
        result[name].confirmationRequired = false;
    }

    // Explicit project always_confirm is an additional restriction and wins
    // conflicts with auto_allow.
    for (const QString &name : confirm) {
        if (!result.contains(name)) continue;
        result[name].confirmationRequired = true;
        result[name].autoAllowed = false;
    }
    return result;
}

QHash<QString, VibeCutToolPolicy> VibeCutPolicyOverrides::applyCurrent(const QHash<QString, VibeCutToolPolicy> &basePolicies, QString *error)
{
    if (error) error->clear();
    if (!pCore || !pCore->currentDoc()) return basePolicies;
    const QUrl url = pCore->currentDoc()->url();
    if (!url.isValid() || !url.isLocalFile() || url.toLocalFile().isEmpty()) return basePolicies;

    const QFileInfo projectInfo(url.toLocalFile());
    QFile file(projectInfo.absoluteDir().filePath(fileName()));
    if (!file.exists()) return basePolicies;
    if (file.size() > 32768) {
        if (error) *error = QStringLiteral("%1 exceeds the 32768 byte policy limit.").arg(file.fileName());
        return basePolicies;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not read %1: %2").arg(file.fileName(), file.errorString());
        return basePolicies;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Invalid VibeCut policy JSON: %1").arg(parseError.errorString());
        return basePolicies;
    }
    return applyObject(basePolicies, document.object());
}
