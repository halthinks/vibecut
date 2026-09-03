/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutduplicatecandidates.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QJsonObject discover(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface || !pCore) return err(QStringLiteral("VibeCut/Kdenlive core is unavailable."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    if (!model) return err(QStringLiteral("Project bin model is unavailable."));

    const int maxAssets = qBound(2, input.value(QStringLiteral("max_assets")).toInt(50), 100);
    const int maxPairs = qBound(1, input.value(QStringLiteral("max_pairs")).toInt(1000), 2000);
    const int minSignals = qBound(1, input.value(QStringLiteral("min_signals")).toInt(2), 6);
    const double minScore = qBound(0.0, input.value(QStringLiteral("min_score")).toDouble(0.55), 1.0);
    const bool includeInsufficient = input.value(QStringLiteral("include_insufficient")).toBool(false);

    QStringList requestedIds;
    QSet<QString> requestedSet;
    const QJsonArray requested = input.value(QStringLiteral("bin_ids")).toArray();
    for (const QJsonValue &value : requested) {
        if (!value.isString()) return err(QStringLiteral("bin_ids must contain strings only."));
        const QString id = value.toString().trimmed();
        if (id.isEmpty()) return err(QStringLiteral("bin_ids may not contain an empty id."));
        if (!requestedSet.contains(id)) {
            requestedSet.insert(id);
            requestedIds.append(id);
        }
    }

    QStringList eligible;
    const QStringList candidateIds = requestedIds.isEmpty() ? model->getAllClipIds() : requestedIds;
    for (const QString &binId : candidateIds) {
        const std::shared_ptr<ProjectClip> clip = model->getClipByBinID(binId);
        if (!clip) {
            if (!requestedIds.isEmpty()) return err(QStringLiteral("Requested bin clip '%1' does not exist.").arg(binId));
            continue;
        }
        if (!clip->hasUrl() || !clip->hasVideo()) {
            if (!requestedIds.isEmpty()) return err(QStringLiteral("Requested bin clip '%1' is not a file-backed video asset.").arg(binId));
            continue;
        }
        const QFileInfo info(clip->url());
        if (!info.exists() || !info.isFile()) {
            if (!requestedIds.isEmpty()) return err(QStringLiteral("Requested source file for bin '%1' is unavailable.").arg(binId));
            continue;
        }
        eligible.append(binId);
    }
    std::sort(eligible.begin(), eligible.end());
    if (eligible.size() < 2) return err(QStringLiteral("At least two eligible file-backed video assets are required."));
    if (eligible.size() > maxAssets) {
        return err(QStringLiteral("Duplicate discovery found %1 eligible assets, exceeding max_assets=%2. Supply bin_ids or increase max_assets within the 100-asset safety bound.")
                       .arg(eligible.size()).arg(maxAssets));
    }
    const qint64 possiblePairs = static_cast<qint64>(eligible.size()) * (eligible.size() - 1) / 2;
    if (possiblePairs > maxPairs) {
        return err(QStringLiteral("Duplicate discovery would evaluate %1 pairs, exceeding max_pairs=%2. Narrow bin_ids or raise max_pairs within the 2000-pair safety bound.")
                       .arg(possiblePairs).arg(maxPairs));
    }

    QList<QJsonObject> results;
    int insufficientSkipped = 0;
    int belowScoreSkipped = 0;
    for (int i = 0; i < eligible.size(); ++i) {
        for (int j = i + 1; j < eligible.size(); ++j) {
            const QJsonObject fused = surface->invoke(QStringLiteral("media_duplicate_fusion"),
                                                      QJsonObject{{QStringLiteral("first_bin_id"), eligible.at(i)},
                                                                  {QStringLiteral("second_bin_id"), eligible.at(j)}});
            if (!fused.value(QStringLiteral("ok")).toBool(false)) {
                QJsonObject failure{{QStringLiteral("first_bin_id"), eligible.at(i)},
                                    {QStringLiteral("second_bin_id"), eligible.at(j)},
                                    {QStringLiteral("classification"), QStringLiteral("fusion_error")},
                                    {QStringLiteral("error"), fused.value(QStringLiteral("error")).toString()},
                                    {QStringLiteral("fusion_score"), 0.0},
                                    {QStringLiteral("independent_signal_count"), 0}};
                if (includeInsufficient) results.append(failure);
                else ++insufficientSkipped;
                continue;
            }
            const QString classification = fused.value(QStringLiteral("classification")).toString();
            const int signals = fused.value(QStringLiteral("independent_signal_count")).toInt(0);
            const double score = fused.value(QStringLiteral("fusion_score")).toDouble(0.0);
            const bool insufficient = classification == QLatin1String("insufficient_evidence") || signals < minSignals;
            if (insufficient && !includeInsufficient) {
                ++insufficientSkipped;
                continue;
            }
            if (!insufficient && score < minScore) {
                ++belowScoreSkipped;
                continue;
            }
            results.append(fused);
        }
    }
    std::sort(results.begin(), results.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const double aScore = a.value(QStringLiteral("fusion_score")).toDouble();
        const double bScore = b.value(QStringLiteral("fusion_score")).toDouble();
        if (aScore != bScore) return aScore > bScore;
        const int aSignals = a.value(QStringLiteral("independent_signal_count")).toInt();
        const int bSignals = b.value(QStringLiteral("independent_signal_count")).toInt();
        if (aSignals != bSignals) return aSignals > bSignals;
        const QString aFirst = a.value(QStringLiteral("first_bin_id")).toString();
        const QString bFirst = b.value(QStringLiteral("first_bin_id")).toString();
        if (aFirst != bFirst) return aFirst < bFirst;
        return a.value(QStringLiteral("second_bin_id")).toString() < b.value(QStringLiteral("second_bin_id")).toString();
    });

    QJsonArray candidates;
    for (const QJsonObject &result : results) candidates.append(result);
    QJsonArray assets;
    for (const QString &id : eligible) assets.append(id);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("authority"), QStringLiteral("derived_candidate_set")},
                       {QStringLiteral("score_semantics"), QStringLiteral("pairwise_weighted_available_evidence_similarity_not_probability")},
                       {QStringLiteral("eligible_asset_count"), eligible.size()},
                       {QStringLiteral("evaluated_pair_count"), static_cast<qint64>(possiblePairs)},
                       {QStringLiteral("candidate_count"), candidates.size()},
                       {QStringLiteral("insufficient_or_error_pairs_skipped"), insufficientSkipped},
                       {QStringLiteral("below_score_pairs_skipped"), belowScoreSkipped},
                       {QStringLiteral("min_signals"), minSignals},
                       {QStringLiteral("min_score"), minScore},
                       {QStringLiteral("include_insufficient"), includeInsufficient},
                       {QStringLiteral("assets"), assets},
                       {QStringLiteral("candidates"), candidates},
                       {QStringLiteral("note"), QStringLiteral("Discovery reuses only currently available pair evidence/embeddings. It never auto-runs MPEG-7 or learned extractors; each candidate discloses missing evidence through media_duplicate_fusion.")}};
}
} // namespace

bool registerVibeCutDuplicateCandidateTools(VibeCutToolSurface &surface, QString *error)
{
    VibeCutToolSurface *surfacePtr = &surface;
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("bin_ids"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                        {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                                        {QStringLiteral("maxItems"), 100}}},
                                {QStringLiteral("max_assets"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 2}, {QStringLiteral("maximum"), 100}}},
                                {QStringLiteral("max_pairs"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 2000}}},
                                {QStringLiteral("min_signals"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 6}}},
                                {QStringLiteral("min_score"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("include_insufficient"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_duplicate_candidates");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Scan a bounded set of current file-backed video bin assets and rank pairwise duplicate/near-duplicate candidates using the existing evidence-fusion primitive. Does not launch missing extractors or mutate the project; missing evidence remains explicit.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, [surfacePtr](const QJsonObject &args) { return discover(surfacePtr, args); }, error);
}
