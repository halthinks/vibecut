/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutrenderrecommendtools.h"

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

QJsonObject recommend(const QJsonObject &input)
{
    const QString destination = input.value(QStringLiteral("destination")).toString(QStringLiteral("general")).trimmed().toLower();
    const QStringList allowed{QStringLiteral("general"), QStringLiteral("youtube"), QStringLiteral("review"), QStringLiteral("archive"),
                              QStringLiteral("social"), QStringLiteral("audio")};
    if (!allowed.contains(destination)) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("destination must be one of: %1").arg(allowed.join(QStringLiteral(", ")))}};
    }

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

    const QJsonObject preflight = vibeCutProjectPreflight();
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("destination"), destination},
                       {QStringLiteral("recommended_preset"), limit > 0 ? candidates.front().name : QString()},
                       {QStringLiteral("ranked"), ranked},
                       {QStringLiteral("preflight"), preflight},
                       {QStringLiteral("note"), QStringLiteral("Recommendation ranks installed Kdenlive presets by deterministic container/codec/destination heuristics. It does not change project resolution/aspect ratio; social/vertical conform remains a separate editing decision.")}};
}
} // namespace

bool registerVibeCutRenderRecommendTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject inputSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"), QJsonObject{
                                      {QStringLiteral("destination"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("general"), QStringLiteral("youtube"), QStringLiteral("review"), QStringLiteral("archive"), QStringLiteral("social"), QStringLiteral("audio")}}}}},
                                  {QStringLiteral("additionalProperties"), false}};
    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("render_recommend")},
                             {QStringLiteral("description"), QStringLiteral("Rank only the render presets actually installed in Kdenlive for a requested destination using deterministic codec/container heuristics, and include current project preflight state. Read-only." )},
                             {QStringLiteral("input_schema"), inputSchema}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("render_recommend");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(schema, policy, recommend, error);
}
