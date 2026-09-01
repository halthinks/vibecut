/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutrenderrecommendtools.h"

#include "core.h"
#include "renderpresets/renderpresetmodel.hpp"
#include "renderpresets/renderpresetrepository.hpp"
#include "vibecutpreflighttools.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>

#include <algorithm>
#include <vector>

namespace {
struct Candidate
{
    QString name;
    QString group;
    QString extension;
    QString vcodec;
    QString acodec;
    int score = 0;
    QStringList reasons;
};

QString lower(QString value)
{
    return value.toLower();
}

void add(Candidate &candidate, int points, const QString &reason)
{
    candidate.score += points;
    candidate.reasons.append(reason);
}

void scoreCommon(Candidate &candidate, const QString &destination)
{
    const QString ext = lower(candidate.extension);
    const QString vcodec = lower(candidate.vcodec);
    const QString acodec = lower(candidate.acodec);
    const QString haystack = lower(candidate.name + QLatin1Char(' ') + candidate.group);

    if (destination == QLatin1String("youtube") || destination == QLatin1String("social") || destination == QLatin1String("general")) {
        if (ext == QLatin1String("mp4") || ext == QLatin1String("m4v")) add(candidate, 30, QStringLiteral("MP4 is broadly compatible for delivery."));
        if (vcodec.contains(QStringLiteral("264"))) add(candidate, 35, QStringLiteral("H.264 is a strong compatibility/default delivery codec."));
        if (vcodec.contains(QStringLiteral("265")) || vcodec.contains(QStringLiteral("hevc"))) add(candidate, 15, QStringLiteral("HEVC is efficient but less universally compatible than H.264."));
        if (acodec.contains(QStringLiteral("aac"))) add(candidate, 20, QStringLiteral("AAC is a standard delivery audio codec."));
        if (haystack.contains(QStringLiteral("youtube"))) add(candidate, 45, QStringLiteral("Preset is explicitly labeled for YouTube."));
        if (destination == QLatin1String("social") && (haystack.contains(QStringLiteral("social")) || haystack.contains(QStringLiteral("mobile")))) {
            add(candidate, 25, QStringLiteral("Preset naming indicates social/mobile delivery intent."));
        }
    } else if (destination == QLatin1String("review")) {
        if (ext == QLatin1String("mp4") || ext == QLatin1String("webm")) add(candidate, 30, QStringLiteral("Container is convenient for review sharing."));
        if (vcodec.contains(QStringLiteral("264")) || vcodec.contains(QStringLiteral("vp9")) || vcodec.contains(QStringLiteral("av1"))) {
            add(candidate, 25, QStringLiteral("Codec is efficient for review copies."));
        }
        if (haystack.contains(QStringLiteral("mobile")) || haystack.contains(QStringLiteral("web")) || haystack.contains(QStringLiteral("low"))) {
            add(candidate, 20, QStringLiteral("Preset naming suggests lightweight review delivery."));
        }
    } else if (destination == QLatin1String("archive")) {
        if (ext == QLatin1String("mov") || ext == QLatin1String("mkv") || ext == QLatin1String("mxf")) add(candidate, 20, QStringLiteral("Container is commonly used for mezzanine/archive workflows."));
        if (vcodec.contains(QStringLiteral("prores")) || vcodec.contains(QStringLiteral("dnx")) || vcodec.contains(QStringLiteral("ffv1")) ||
            vcodec.contains(QStringLiteral("huff")) || haystack.contains(QStringLiteral("lossless"))) {
            add(candidate, 55, QStringLiteral("Codec/preset indicates mezzanine or lossless archival quality."));
        }
        if (haystack.contains(QStringLiteral("archive")) || haystack.contains(QStringLiteral("master"))) add(candidate, 35, QStringLiteral("Preset is explicitly labeled for archive/master output."));
    } else if (destination == QLatin1String("audio")) {
        if (ext == QLatin1String("wav") || ext == QLatin1String("flac")) add(candidate, 55, QStringLiteral("Lossless audio container is preferred for audio masters."));
        if (ext == QLatin1String("mp3") || ext == QLatin1String("m4a") || ext == QLatin1String("ogg")) add(candidate, 25, QStringLiteral("Compressed audio container is convenient for delivery."));
        if (vcodec.isEmpty() || vcodec == QLatin1String("none") || haystack.contains(QStringLiteral("audio"))) add(candidate, 35, QStringLiteral("Preset appears audio-oriented rather than video-oriented."));
    }
}

QJsonObject recommendFor(const QString &destination)
{
    std::vector<Candidate> candidates;
    for (const QString &presetId : RenderPresetRepository::get()->getAllPresets()) {
        if (!RenderPresetRepository::get()->presetExists(presetId)) continue;
        std::unique_ptr<RenderPresetModel> &preset = RenderPresetRepository::get()->getPreset(presetId);
        if (!preset || !preset->isValid() || !preset->error().isEmpty()) continue;

        Candidate candidate;
        candidate.name = preset->name();
        candidate.group = preset->groupId();
        candidate.extension = preset->extension();
        candidate.vcodec = preset->getParam(QStringLiteral("vcodec"));
        candidate.acodec = preset->getParam(QStringLiteral("acodec"));
        scoreCommon(candidate, destination);
        if (!preset->warning().isEmpty()) {
            candidate.score -= 10;
            candidate.reasons.append(QStringLiteral("Kdenlive warning: %1").arg(preset->warning()));
        }
        if (candidate.score > 0) candidates.push_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        if (a.score != b.score) return a.score > b.score;
        return a.name.toLower() < b.name.toLower();
    });

    QJsonArray ranked;
    const int limit = qMin(5, static_cast<int>(candidates.size()));
    for (int i = 0; i < limit; ++i) {
        const Candidate &candidate = candidates.at(static_cast<size_t>(i));
        QJsonArray reasons;
        for (const QString &reason : candidate.reasons) reasons.append(reason);
        ranked.append(QJsonObject{{QStringLiteral("preset"), candidate.name},
                                  {QStringLiteral("group"), candidate.group},
                                  {QStringLiteral("extension"), candidate.extension},
                                  {QStringLiteral("vcodec"), candidate.vcodec},
                                  {QStringLiteral("acodec"), candidate.acodec},
                                  {QStringLiteral("score"), candidate.score},
                                  {QStringLiteral("reasons"), reasons}});
    }

    return QJsonObject{{QStringLiteral("recommended_preset"), limit > 0 ? candidates.front().name : QString()},
                       {QStringLiteral("ranked"), ranked}};
}

QJsonObject recommend(const QJsonObject &input)
{
    const QString destination = input.value(QStringLiteral("destination")).toString(QStringLiteral("general")).trimmed().toLower();
    const QStringList allowed{QStringLiteral("general"), QStringLiteral("youtube"), QStringLiteral("review"), QStringLiteral("archive"),
                              QStringLiteral("social"), QStringLiteral("audio")};
    if (!allowed.contains(destination)) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("destination must be one of: %1").arg(allowed.join(QStringLiteral(", ")))}};
    }

    QJsonObject result = recommendFor(destination);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("destination"), destination);
    result.insert(QStringLiteral("preflight"), vibeCutProjectPreflight());
    result.insert(QStringLiteral("note"), QStringLiteral("Recommendation ranks installed Kdenlive presets by deterministic container/codec/destination heuristics. It does not change project resolution/aspect ratio; conform remains a separate governed edit."));
    return result;
}

QJsonObject exportPolicy(const QJsonObject &input)
{
    const QString profile = input.value(QStringLiteral("profile")).toString().trimmed().toLower();
    const QStringList allowed{QStringLiteral("youtube"), QStringLiteral("review_proxy"), QStringLiteral("archive_master"),
                              QStringLiteral("social_vertical"), QStringLiteral("social_square"), QStringLiteral("audio_master")};
    if (!allowed.contains(profile)) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("profile must be one of: %1").arg(allowed.join(QStringLiteral(", ")))}};
    }

    QString destination;
    bool useProxies = false;
    bool requireOriginals = true;
    int targetW = 0;
    int targetH = 0;
    QString purpose;
    if (profile == QLatin1String("youtube")) {
        destination = QStringLiteral("youtube");
        purpose = QStringLiteral("High-quality broadly compatible upload master preserving project aspect ratio.");
    } else if (profile == QLatin1String("review_proxy")) {
        destination = QStringLiteral("review");
        useProxies = true;
        requireOriginals = false;
        purpose = QStringLiteral("Fast lightweight review copy; proxy-only media is acceptable.");
    } else if (profile == QLatin1String("archive_master")) {
        destination = QStringLiteral("archive");
        purpose = QStringLiteral("High-quality mezzanine/lossless archive master; originals required.");
    } else if (profile == QLatin1String("social_vertical")) {
        destination = QStringLiteral("social");
        targetW = 1080;
        targetH = 1920;
        purpose = QStringLiteral("9:16 vertical social delivery; project may require a separate governed reframe/conform edit before render.");
    } else if (profile == QLatin1String("social_square")) {
        destination = QStringLiteral("social");
        targetW = 1080;
        targetH = 1080;
        purpose = QStringLiteral("1:1 square social delivery; project may require a separate governed reframe/conform edit before render.");
    } else {
        destination = QStringLiteral("audio");
        targetW = 0;
        targetH = 0;
        purpose = QStringLiteral("Lossless audio master when an installed audio preset is available.");
    }

    const QSize frameSize = pCore ? pCore->getCurrentFrameSize() : QSize();
    bool requiresConform = false;
    if (targetW > 0 && targetH > 0 && frameSize.width() > 0 && frameSize.height() > 0) {
        const qint64 lhs = static_cast<qint64>(frameSize.width()) * targetH;
        const qint64 rhs = static_cast<qint64>(frameSize.height()) * targetW;
        requiresConform = lhs != rhs;
    }

    const QJsonObject recommendation = recommendFor(destination);
    const QJsonObject preflight = vibeCutProjectPreflight();
    const int proxyOnly = preflight.value(QStringLiteral("proxy_only_asset_count")).toInt(0);
    QJsonArray blockers;
    if (requireOriginals && proxyOnly > 0) {
        blockers.append(QJsonObject{{QStringLiteral("code"), QStringLiteral("originals_required")},
                                    {QStringLiteral("message"), QStringLiteral("This export profile requires original media, but proxy-only assets are present.")},
                                    {QStringLiteral("count"), proxyOnly}});
    }
    if (requiresConform) {
        blockers.append(QJsonObject{{QStringLiteral("code"), QStringLiteral("conform_required")},
                                    {QStringLiteral("message"), QStringLiteral("Project aspect ratio does not match the requested delivery profile; perform a governed reframe/conform edit before rendering.")}});
    }

    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("profile"), profile},
                       {QStringLiteral("purpose"), purpose}, {QStringLiteral("destination_class"), destination},
                       {QStringLiteral("recommended_preset"), recommendation.value(QStringLiteral("recommended_preset"))},
                       {QStringLiteral("ranked_presets"), recommendation.value(QStringLiteral("ranked"))},
                       {QStringLiteral("use_proxies"), useProxies}, {QStringLiteral("require_originals"), requireOriginals},
                       {QStringLiteral("project_width"), frameSize.width()}, {QStringLiteral("project_height"), frameSize.height()},
                       {QStringLiteral("target_width"), targetW}, {QStringLiteral("target_height"), targetH},
                       {QStringLiteral("requires_conform"), requiresConform},
                       {QStringLiteral("ready_for_profile"), preflight.value(QStringLiteral("ready_for_long_jobs")).toBool(false) && blockers.isEmpty()},
                       {QStringLiteral("blockers"), blockers}, {QStringLiteral("preflight"), preflight}};
}
} // namespace

bool registerVibeCutRenderRecommendTools(VibeCutToolSurface &surface, QString *error)
{
    QJsonObject destinationProperty;
    destinationProperty.insert(QStringLiteral("type"), QStringLiteral("string"));
    destinationProperty.insert(QStringLiteral("enum"), QJsonArray{QStringLiteral("general"), QStringLiteral("youtube"), QStringLiteral("review"),
                                                                  QStringLiteral("archive"), QStringLiteral("social"), QStringLiteral("audio")});
    QJsonObject inputProperties;
    inputProperties.insert(QStringLiteral("destination"), destinationProperty);
    const QJsonObject inputSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), inputProperties},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("render_recommend")},
                             {QStringLiteral("description"), QStringLiteral("Rank only the render presets actually installed in Kdenlive for a requested destination using deterministic codec/container heuristics, and include current project preflight state. Read-only.")},
                             {QStringLiteral("input_schema"), inputSchema}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("render_recommend");
    policy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(schema, policy, recommend, error)) return false;

    QJsonObject profileProperty;
    profileProperty.insert(QStringLiteral("type"), QStringLiteral("string"));
    profileProperty.insert(QStringLiteral("enum"), QJsonArray{QStringLiteral("youtube"), QStringLiteral("review_proxy"), QStringLiteral("archive_master"),
                                                              QStringLiteral("social_vertical"), QStringLiteral("social_square"), QStringLiteral("audio_master")});
    QJsonObject policyProperties;
    policyProperties.insert(QStringLiteral("profile"), profileProperty);
    const QJsonObject policyInput{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), policyProperties},
                                  {QStringLiteral("required"), QJsonArray{QStringLiteral("profile")}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject policySchema{{QStringLiteral("name"), QStringLiteral("render_profile_policy")},
                                   {QStringLiteral("description"), QStringLiteral("Resolve a named product export profile into explicit original/proxy requirements, target geometry/conform requirement, current preflight blockers, and the best installed Kdenlive preset. Read-only; does not silently reframe the project or start rendering.")},
                                   {QStringLiteral("input_schema"), policyInput}};
    VibeCutToolPolicy profilePolicy;
    profilePolicy.name = QStringLiteral("render_profile_policy");
    profilePolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(policySchema, profilePolicy, exportPolicy, error);
}
