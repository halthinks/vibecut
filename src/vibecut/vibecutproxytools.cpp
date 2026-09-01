/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutproxytools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "vibecuttoolsurface.h"

#include <QFileInfo>
#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QJsonObject proxyState(const QString &binId, const std::shared_ptr<ProjectClip> &clip)
{
    const QString proxyPath = clip ? clip->getProducerProperty(QStringLiteral("kdenlive:proxy")) : QString();
    const QString sourcePath = clip ? clip->getProducerProperty(QStringLiteral("kdenlive:originalurl")) : QString();
    const bool proxyConfigured = !proxyPath.isEmpty() && proxyPath != QLatin1String("-");
    const bool proxyExists = proxyConfigured && QFileInfo::exists(proxyPath);
    const bool active = clip && clip->hasProxy();
    return QJsonObject{{QStringLiteral("bin_id"), binId},
                       {QStringLiteral("name"), clip ? clip->getProducerProperty(QStringLiteral("kdenlive:clipname")) : QString()},
                       {QStringLiteral("source_path"), sourcePath.isEmpty() && clip ? clip->url() : sourcePath},
                       {QStringLiteral("proxy_path"), proxyPath},
                       {QStringLiteral("proxy_configured"), proxyConfigured},
                       {QStringLiteral("proxy_exists"), proxyExists},
                       {QStringLiteral("proxy_active"), active},
                       {QStringLiteral("proxy_pending"), proxyConfigured && !proxyExists},
                       {QStringLiteral("clip_status"), clip ? static_cast<int>(clip->clipStatus()) : -1},
                       {QStringLiteral("timeline_instances"), clip ? static_cast<int>(clip->timelineInstances().size()) : 0}};
}

QJsonObject listProxyStatus(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));

    const QString requestedId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (!requestedId.isEmpty()) {
        const std::shared_ptr<ProjectClip> clip = model->getClipByBinID(requestedId);
        if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(requestedId));
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("proxy"), proxyState(requestedId, clip)}};
    }

    QJsonArray proxies;
    for (const QString &binId : model->getAllClipIds()) {
        const std::shared_ptr<ProjectClip> clip = model->getClipByBinID(binId);
        if (!clip || !clip->hasUrl()) continue;
        proxies.append(proxyState(binId, clip));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("proxies"), proxies}};
}

QJsonObject setProxyEnabled(const QJsonObject &input)
{
    if (!pCore || !pCore->currentDoc()) return err(QStringLiteral("No project document is open."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));

    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    const bool enabled = input.value(QStringLiteral("enabled")).toBool();
    const bool force = input.value(QStringLiteral("force")).toBool(false);
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty"));

    const std::shared_ptr<ProjectClip> clip = model->getClipByBinID(binId);
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!clip->hasUrl()) return err(QStringLiteral("Bin clip '%1' is generated/non-file-backed and does not support ordinary proxy generation.").arg(binId));

    const QJsonObject before = proxyState(binId, clip);
    if (!enabled && !before.value(QStringLiteral("proxy_configured")).toBool()) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changed"), false},
                           {QStringLiteral("proxy"), before}, {QStringLiteral("verified"), true}};
    }
    if (enabled && before.value(QStringLiteral("proxy_active")).toBool() && !force) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changed"), false},
                           {QStringLiteral("proxy"), before}, {QStringLiteral("verified"), true}};
    }

    QList<std::shared_ptr<ProjectClip>> clips{clip};
    pCore->currentDoc()->slotProxyCurrentItem(enabled, clips, force);

    const std::shared_ptr<ProjectClip> live = model->getClipByBinID(binId);
    if (!live) return err(QStringLiteral("Proxy request completed but the bin clip is no longer available."));
    const QJsonObject after = proxyState(binId, live);

    if (!enabled && after.value(QStringLiteral("proxy_configured")).toBool()) {
        return err(QStringLiteral("Kdenlive did not verify proxy removal on the live bin clip."));
    }

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("changed"), true},
                       {QStringLiteral("requested_enabled"), enabled},
                       {QStringLiteral("proxy"), after},
                       {QStringLiteral("verified"), !enabled || after.value(QStringLiteral("proxy_configured")).toBool()},
                       {QStringLiteral("note"), enabled
                            ? QStringLiteral("Kdenlive accepted the native proxy request. Generation may continue in Kdenlive's background ProxyTask; call proxy_status to observe pending/active state.")
                            : QStringLiteral("Kdenlive removed the proxy mapping through its native undoable project path.")}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required},
                       {QStringLiteral("additionalProperties"), false}};
}
} // namespace

bool registerVibeCutProxyTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject statusInput = objectSchema(
        QJsonObject{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                           {QStringLiteral("description"), QStringLiteral("Optional bin id; omit to inspect all file-backed assets.")}}}},
        QJsonArray{});
    const QJsonObject statusSchema{{QStringLiteral("name"), QStringLiteral("proxy_status")},
                                   {QStringLiteral("description"), QStringLiteral("Inspect Kdenlive proxy state for one or all file-backed bin assets, including configured path, file existence, active/pending state, clip status and timeline usage. Read-only.")},
                                   {QStringLiteral("input_schema"), statusInput}};
    VibeCutToolPolicy statusPolicy;
    statusPolicy.name = QStringLiteral("proxy_status");
    statusPolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(statusSchema, statusPolicy, listProxyStatus, error)) return false;

    const QJsonObject setInput = objectSchema(
        QJsonObject{{QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                    {QStringLiteral("enabled"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
                    {QStringLiteral("force"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                                                          {QStringLiteral("description"), QStringLiteral("When enabling, request Kdenlive to rebuild/regenerate even if a proxy is already active.")}}}},
        QJsonArray{QStringLiteral("bin_id"), QStringLiteral("enabled")});
    const QJsonObject setSchema{{QStringLiteral("name"), QStringLiteral("proxy_set_enabled")},
                                {QStringLiteral("description"), QStringLiteral("Request Kdenlive's native proxy add/remove path for a file-backed bin clip. Proxy creation may continue in Kdenlive's background ProxyTask; removal and project mapping changes remain undoable.")},
                                {QStringLiteral("input_schema"), setInput}};
    VibeCutToolPolicy setPolicy;
    setPolicy.name = QStringLiteral("proxy_set_enabled");
    setPolicy.risk = VibeCutToolRisk::ExternalSideEffect;
    setPolicy.reversible = true;
    setPolicy.mutatesProject = true;
    return surface.registerTool(setSchema, setPolicy, setProxyEnabled, error);
}
