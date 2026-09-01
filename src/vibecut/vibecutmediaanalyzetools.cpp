/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutmediaanalyzetools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QJsonObject analyze(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface || !pCore) return err(QStringLiteral("VibeCut/Kdenlive core is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!clip->hasUrl()) return err(QStringLiteral("Basic media analysis currently requires a file-backed source."));

    QJsonArray started;
    QJsonArray failed;
    auto invoke = [&](const QString &tool, const QJsonObject &args) {
        const QJsonObject result = surface->invoke(tool, args);
        QJsonObject entry{{QStringLiteral("tool"), tool}, {QStringLiteral("result"), result}};
        if (result.value(QStringLiteral("ok")).toBool(false)) started.append(entry);
        else failed.append(entry);
    };

    invoke(QStringLiteral("media_source_metadata_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
    if (clip->hasAudio()) {
        invoke(QStringLiteral("media_silence_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
        invoke(QStringLiteral("media_loudness_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
    }
    if (clip->hasVideo()) {
        invoke(QStringLiteral("media_shots_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
        invoke(QStringLiteral("media_black_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
        invoke(QStringLiteral("media_freeze_refresh"), QJsonObject{{QStringLiteral("bin_id"), binId}});
    }

    return QJsonObject{{QStringLiteral("ok"), failed.isEmpty()},
                       {QStringLiteral("bin_id"), binId},
                       {QStringLiteral("has_audio"), clip->hasAudio()},
                       {QStringLiteral("has_video"), clip->hasVideo()},
                       {QStringLiteral("started_count"), started.size()},
                       {QStringLiteral("failed_count"), failed.size()},
                       {QStringLiteral("started"), started},
                       {QStringLiteral("failed"), failed},
                       {QStringLiteral("asynchronous_children"), true}};
}
} // namespace

bool registerVibeCutMediaAnalyzeTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                            {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("media_analyze_refresh")},
                             {QStringLiteral("description"), QStringLiteral("Run VibeCut's deterministic basic media-intelligence suite for one file-backed bin asset: source metadata always; silence/loudness when audio exists; shot/black/freeze detection when video exists. Starts child JobManager jobs and returns their ids/results without mutating the Kdenlive project.")},
                             {QStringLiteral("input_schema"), input}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_analyze_refresh");
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.asynchronous = true;
    policy.mutatesProject = false;
    return surface.registerTool(schema, policy, [&surface](const QJsonObject &input) { return analyze(&surface, input); }, error);
}
