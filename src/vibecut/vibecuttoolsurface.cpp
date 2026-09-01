/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecuttoolsurface.h"

#include "vibecutaudiomonitortools.h"
#include "vibecutbinfoldertools.h"
#include "vibecutbinmetadatatools.h"
#include "vibecutbintools.h"
#include "vibecutbulkcopytools.h"
#include "vibecutbulktools.h"
#include "vibecutbuseffecttools.h"
#include "vibecutcompositiontracktools.h"
#include "vibecutedittools.h"
#include "vibecuteffectgrouptools.h"
#include "vibecuteffectkeyframetools.h"
#include "vibecuteffectstackcopytools.h"
#include "vibecuteffecttools.h"
#include "vibecutextractorprovidertools.h"
#include "vibecutgrouptools.h"
#include "vibecutmarkertools.h"
#include "vibecutmediaevidencetools.h"
#include "vibecutmemorytools.h"
#include "vibecutmixtools.h"
#include "vibecutpolicyoverrides.h"
#include "vibecutpreflighttools.h"
#include "vibecutproxytools.h"
#include "vibecutrelinkdiscoverytools.h"
#include "vibecutrelinktools.h"
#include "vibecutrenderrecommendtools.h"
#include "vibecutrendertools.h"
#include "vibecutroutingtools.h"
#include "vibecutselectiontools.h"
#include "vibecutsequencetools.h"
#include "vibecutsimilaritytools.h"
#include "vibecutsubtitleedittools.h"
#include "vibecuttitleinspecttools.h"
#include "vibecuttitletools.h"
#include "vibecuttracktools.h"
#include "vibecuttransitionparamtools.h"
#include "vibecuttransitiontools.h"
#include "vibecuttools.h"

#include <QDebug>

namespace {
QJsonObject errorResult(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}
} // namespace

VibeCutToolSurface::VibeCutToolSurface(VibeCutTools *baseTools)
    : m_baseTools(baseTools)
{
    QString error;
    if (!registerVibeCutAudioMonitorTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] audio monitor tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutBinTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] bin tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutBinMetadataTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] bin metadata tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutBinFolderTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] bin folder tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutBulkTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] bulk tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutBulkCopyTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] bulk copy tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutBusEffectTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] bus effect tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutCompositionTrackTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] composition track tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutEditTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] core edit tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutEffectTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] effect tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutEffectGroupTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] effect group tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutEffectKeyframeTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] effect keyframe tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutEffectStackCopyTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] effect stack copy tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutExtractorProviderTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] extractor provider tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutGroupTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] group tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutMarkerTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] guide tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutMediaEvidenceTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] media evidence tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutSimilarityTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] media similarity tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutMemoryTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] project memory tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutMixTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] mix tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutPreflightTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] preflight tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutProxyTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] proxy tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutRelinkDiscoveryTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] relink discovery tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutRelinkTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] relink tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutRenderRecommendTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] render recommendation tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutRoutingTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] routing tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutSelectionTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] selection tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutSequenceTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] sequence tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutSubtitleEditTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] subtitle edit tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutTitleTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] title tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutTitleInspectTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] title inspection tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutTrackTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] track tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutTransitionTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] transition tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutTransitionParameterTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] transition parameter tools unavailable: %1").arg(error);
    }
    error.clear();
    if (!registerVibeCutRenderTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] render tools unavailable: %1").arg(error);
    }
}

bool VibeCutToolSurface::baseContains(const QString &name) const
{
    if (!m_baseTools) return false;
    const QJsonArray baseSchemas = m_baseTools->schemas();
    for (const QJsonValue &value : baseSchemas) {
        if (value.toObject().value(QStringLiteral("name")).toString() == name) return true;
    }
    return false;
}

bool VibeCutToolSurface::validateRegistration(const QJsonObject &schema, const VibeCutToolPolicy &policy, const Handler &handler, QString *error)
{
    if (error) error->clear();
    const QString name = schema.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) *error = QStringLiteral("tool schema requires a non-empty name");
        return false;
    }
    if (policy.name != name) {
        if (error) *error = QStringLiteral("tool policy name '%1' does not match schema name '%2'").arg(policy.name, name);
        return false;
    }
    if (!schema.value(QStringLiteral("input_schema")).isObject()) {
        if (error) *error = QStringLiteral("tool '%1' requires an input_schema object").arg(name);
        return false;
    }
    if (!handler) {
        if (error) *error = QStringLiteral("tool '%1' requires a handler").arg(name);
        return false;
    }
    return true;
}

bool VibeCutToolSurface::registerTool(const QJsonObject &schema, const VibeCutToolPolicy &policy, const Handler &handler, QString *error)
{
    if (!validateRegistration(schema, policy, handler, error)) return false;
    const QString name = policy.name;
    if (m_extensions.contains(name) || m_overrides.contains(name) || baseContains(name)) {
        if (error) *error = QStringLiteral("tool '%1' is already registered").arg(name);
        return false;
    }
    Extension extension;
    extension.schema = schema;
    extension.policy = policy;
    extension.handler = handler;
    m_extensions.insert(name, extension);
    m_extensionOrder.append(name);
    return true;
}

bool VibeCutToolSurface::overrideBaseTool(const QJsonObject &schema, const VibeCutToolPolicy &policy, const Handler &handler, QString *error)
{
    if (!validateRegistration(schema, policy, handler, error)) return false;
    const QString name = policy.name;
    if (!baseContains(name)) {
        if (error) *error = QStringLiteral("cannot override unknown native tool '%1'").arg(name));
        return false;
    }
    if (m_extensions.contains(name) || m_overrides.contains(name)) {
        if (error) *error = QStringLiteral("tool '%1' already has a surface registration").arg(name);
        return false;
    }
    Extension override;
    override.schema = schema;
    override.policy = policy;
    override.handler = handler;
    m_overrides.insert(name, override);
    return true;
}

QJsonArray VibeCutToolSurface::schemas() const
{
    QJsonArray result;
    const VibeCutPolicyOverrides overrides = VibeCutPolicyOverrides::loadCurrent();
    if (m_baseTools) {
        for (const QJsonValue &value : m_baseTools->schemas()) {
            const QJsonObject baseSchema = value.toObject();
            const QString name = baseSchema.value(QStringLiteral("name")).toString();
            if (overrides.isDenied(name)) continue;
            const auto override = m_overrides.constFind(name);
            result.append(override != m_overrides.constEnd() ? override.value().schema : baseSchema);
        }
    }
    for (const QString &name : m_extensionOrder) {
        if (!overrides.isDenied(name)) result.append(m_extensions.value(name).schema);
    }
    return result;
}

QHash<QString, VibeCutToolPolicy> VibeCutToolSurface::policies() const
{
    QHash<QString, VibeCutToolPolicy> result = m_baseTools ? m_baseTools->policies() : QHash<QString, VibeCutToolPolicy>();
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it) result.insert(it.key(), it.value().policy);
    for (const QString &name : m_extensionOrder) result.insert(name, m_extensions.value(name).policy);
    return VibeCutPolicyOverrides::loadCurrent().apply(result);
}

QJsonObject VibeCutToolSurface::invoke(const QString &name, const QJsonObject &input) const
{
    const VibeCutPolicyOverrides overrides = VibeCutPolicyOverrides::loadCurrent();
    if (overrides.isDenied(name)) return errorResult(QStringLiteral("Tool '%1' is denied by .vibecutpolicy.json").arg(name));
    const auto override = m_overrides.constFind(name);
    if (override != m_overrides.constEnd()) return override.value().handler(input);
    const auto extension = m_extensions.constFind(name);
    if (extension != m_extensions.constEnd()) return extension.value().handler(input);
    return invokeBase(name, input);
}

QJsonObject VibeCutToolSurface::invokeBase(const QString &name, const QJsonObject &input) const
{
    if (m_baseTools) return m_baseTools->invoke(name, input);
    return errorResult(QStringLiteral("Unknown tool: %1").arg(name));
}

quint64 VibeCutToolSurface::projectRevision() const
{
    return m_baseTools ? m_baseTools->projectRevision() : 0;
}
