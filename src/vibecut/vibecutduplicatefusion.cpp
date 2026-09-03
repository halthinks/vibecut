/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutduplicatefusion.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecutembeddingstore.h"
#include "vibecutmediaevidence.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {
const QString kMiniLmModel = QStringLiteral("sentence-transformers/all-MiniLM-L6-v2");
const QString kMiniLmRevision = QStringLiteral("1110a243fdf4706b3f48f1d95db1a4f5529b4d41");
const QString kSiglipModel = QStringLiteral("google/siglip-base-patch16-224");
const QString kSiglipRevision = QStringLiteral("7fd15f0689c79d79e38b1c2e2e2370a7bf2761ed");

struct EmbeddingPairSignal {
    bool available = false;
    double similarity = 0.0;
    bool alignmentAvailable = false;
    double alignment = 0.0;
    int firstCount = 0;
    int secondCount = 0;
};

struct VecItem {
    QVector<double> vector;
    int frame = -1;
};

QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString statFingerprint(const QFileInfo &info)
{
    const QByteArray payload = info.canonicalFilePath().toUtf8() + '\n' + QByteArray::number(info.size()) + '\n' +
                               QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

double dot(const QVector<double> &a, const QVector<double> &b)
{
    if (a.size() != b.size() || a.isEmpty()) return -1.0;
    long double value = 0.0L;
    for (int i = 0; i < a.size(); ++i) value += static_cast<long double>(a.at(i)) * b.at(i);
    return qBound(-1.0, static_cast<double>(value), 1.0);
}

QList<VecItem> sourceEmbeddings(const QJsonObject &root,
                                const QString &model,
                                const QString &revision,
                                const QString &modality,
                                const QString &sourceId,
                                const QString &fingerprint)
{
    QList<VecItem> items;
    for (const QJsonValue &value : root.value(QStringLiteral("records")).toArray()) {
        if (!value.isObject()) continue;
        VibeCutEmbeddingRecord record;
        QString error;
        if (!VibeCutEmbeddingRecord::fromJson(value.toObject(), record, &error)) continue;
        if (record.model != model || record.modelRevision != revision || record.modality != modality ||
            record.sourceId != sourceId || record.sourceFingerprint != fingerprint) continue;
        VecItem item;
        item.vector = record.vector;
        item.frame = record.startFrame;
        items.append(item);
    }
    std::sort(items.begin(), items.end(), [](const VecItem &a, const VecItem &b) {
        return a.frame < b.frame;
    });
    return items;
}

EmbeddingPairSignal embeddingPairSignal(const QJsonObject &root,
                                        const QString &model,
                                        const QString &revision,
                                        const QString &modality,
                                        const QString &firstSource,
                                        const QString &firstFingerprint,
                                        const QString &secondSource,
                                        const QString &secondFingerprint)
{
    EmbeddingPairSignal result;
    const QList<VecItem> first = sourceEmbeddings(root, model, revision, modality, firstSource, firstFingerprint);
    const QList<VecItem> second = sourceEmbeddings(root, model, revision, modality, secondSource, secondFingerprint);
    result.firstCount = first.size();
    result.secondCount = second.size();
    if (first.isEmpty() || second.isEmpty()) return result;

    auto direction = [](const QList<VecItem> &from, const QList<VecItem> &to, double &meanBest, double &meanAlignment) {
        long double similaritySum = 0.0L;
        long double alignmentSum = 0.0L;
        for (int i = 0; i < from.size(); ++i) {
            double best = -2.0;
            int bestIndex = -1;
            for (int j = 0; j < to.size(); ++j) {
                const double value = dot(from.at(i).vector, to.at(j).vector);
                if (value > best) {
                    best = value;
                    bestIndex = j;
                }
            }
            similaritySum += qBound(0.0, best, 1.0);
            if (from.size() > 1 && to.size() > 1 && bestIndex >= 0) {
                const double fromPosition = static_cast<double>(i) / (from.size() - 1);
                const double toPosition = static_cast<double>(bestIndex) / (to.size() - 1);
                alignmentSum += 1.0 - qMin(1.0, std::abs(fromPosition - toPosition));
            }
        }
        meanBest = static_cast<double>(similaritySum / from.size());
        meanAlignment = (from.size() > 1 && to.size() > 1)
                            ? static_cast<double>(alignmentSum / from.size())
                            : 0.0;
    };

    double firstBest = 0.0;
    double firstAlignment = 0.0;
    double secondBest = 0.0;
    double secondAlignment = 0.0;
    direction(first, second, firstBest, firstAlignment);
    direction(second, first, secondBest, secondAlignment);
    result.available = true;
    result.similarity = qBound(0.0, (firstBest + secondBest) / 2.0, 1.0);
    if (first.size() > 1 && second.size() > 1) {
        result.alignmentAvailable = true;
        result.alignment = qBound(0.0, (firstAlignment + secondAlignment) / 2.0, 1.0);
    }
    return result;
}

QSet<QString> sourceTokens(const QJsonArray &evidence, const QString &sourceId, const QString &fingerprint)
{
    QSet<QString> tokens;
    const QRegularExpression expression(QStringLiteral("[\\p{L}\\p{N}_]+"));
    for (const QJsonValue &value : evidence) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("source_id")).toString() != sourceId ||
            object.value(QStringLiteral("source_fingerprint")).toString() != fingerprint) continue;
        const QString kind = object.value(QStringLiteral("kind")).toString().toLower();
        if (kind != QLatin1String("ocr_text") && !kind.contains(QStringLiteral("transcript"))) continue;
        const QString text = object.value(QStringLiteral("text")).toString().toLower();
        QRegularExpressionMatchIterator iterator = expression.globalMatch(text);
        while (iterator.hasNext() && tokens.size() < 5000) {
            const QString token = iterator.next().captured(0);
            if (token.size() > 1) tokens.insert(token);
        }
    }
    return tokens;
}

double jaccard(const QSet<QString> &a, const QSet<QString> &b)
{
    if (a.isEmpty() || b.isEmpty()) return 0.0;
    int intersection = 0;
    for (const QString &value : a) if (b.contains(value)) ++intersection;
    const int unionCount = a.size() + b.size() - intersection;
    return unionCount > 0 ? static_cast<double>(intersection) / unionCount : 0.0;
}

QJsonObject signal(bool available, double score, const QJsonObject &metadata = QJsonObject())
{
    return QJsonObject{{QStringLiteral("available"), available},
                       {QStringLiteral("score"), available ? qBound(0.0, score, 1.0) : 0.0},
                       {QStringLiteral("metadata"), metadata}};
}

QJsonObject mpeg7Signal(const QJsonArray &evidence,
                        const QString &firstBin,
                        const QString &secondBin,
                        const QString &firstFingerprint,
                        const QString &secondFingerprint)
{
    for (const QJsonValue &value : evidence) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const QString kind = object.value(QStringLiteral("kind")).toString();
        if (kind != QLatin1String("video_similarity_match") && kind != QLatin1String("video_similarity_no_match")) continue;
        const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
        const QString firstId = metadata.value(QStringLiteral("first_bin_id")).toString();
        const QString secondId = metadata.value(QStringLiteral("second_bin_id")).toString();
        const QString fp1 = metadata.value(QStringLiteral("first_source_fingerprint")).toString();
        const QString fp2 = metadata.value(QStringLiteral("second_source_fingerprint")).toString();
        const bool sameOrder = firstId == firstBin && secondId == secondBin && fp1 == firstFingerprint && fp2 == secondFingerprint;
        const bool reverseOrder = firstId == secondBin && secondId == firstBin && fp1 == secondFingerprint && fp2 == firstFingerprint;
        if (!sameOrder && !reverseOrder) continue;
        const bool matched = metadata.value(QStringLiteral("matched")).toBool(kind == QLatin1String("video_similarity_match"));
        return signal(true, matched ? 1.0 : 0.0,
                      QJsonObject{{QStringLiteral("matched"), matched},
                                  {QStringLiteral("matching_frames"), metadata.value(QStringLiteral("matching_frames"))},
                                  {QStringLiteral("method"), metadata.value(QStringLiteral("method"))}});
    }
    return signal(false, 0.0);
}

QJsonObject duplicateTool(const QJsonObject &input)
{
    if (!pCore) return err(QStringLiteral("Kdenlive core is unavailable."));
    const QString firstBin = input.value(QStringLiteral("first_bin_id")).toString().trimmed();
    const QString secondBin = input.value(QStringLiteral("second_bin_id")).toString().trimmed();
    if (firstBin.isEmpty() || secondBin.isEmpty() || firstBin == secondBin) {
        return err(QStringLiteral("first_bin_id and second_bin_id must identify two distinct assets."));
    }
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> first = model ? model->getClipByBinID(firstBin) : nullptr;
    const std::shared_ptr<ProjectClip> second = model ? model->getClipByBinID(secondBin) : nullptr;
    if (!first || !second) return err(QStringLiteral("Both bin assets must exist."));
    if (!first->hasUrl() || !second->hasUrl() || !first->hasVideo() || !second->hasVideo()) {
        return err(QStringLiteral("Duplicate fusion currently requires two file-backed video assets."));
    }
    const QFileInfo firstInfo(first->url());
    const QFileInfo secondInfo(second->url());
    if (!firstInfo.exists() || !firstInfo.isFile() || !secondInfo.exists() || !secondInfo.isFile()) {
        return err(QStringLiteral("Both source files must currently exist."));
    }
    const QString firstFingerprint = statFingerprint(firstInfo);
    const QString secondFingerprint = statFingerprint(secondInfo);
    const QString firstSource = QStringLiteral("bin:%1").arg(firstBin);
    const QString secondSource = QStringLiteral("bin:%1").arg(secondBin);

    QString evidenceError;
    const QJsonArray evidence = VibeCutMediaEvidence::loadCurrent(&evidenceError);
    if (!evidenceError.isEmpty()) return err(evidenceError);
    QString embeddingError;
    const QJsonObject embeddings = VibeCutEmbeddingStore::loadCurrent(&embeddingError);
    if (!embeddingError.isEmpty()) return err(embeddingError);

    QJsonObject components;
    components.insert(QStringLiteral("mpeg7"), mpeg7Signal(evidence, firstBin, secondBin, firstFingerprint, secondFingerprint));

    const EmbeddingPairSignal visual = embeddingPairSignal(embeddings, kSiglipModel, kSiglipRevision, QStringLiteral("visual"),
                                                           firstSource, firstFingerprint, secondSource, secondFingerprint);
    components.insert(QStringLiteral("siglip_visual"),
                      signal(visual.available, visual.similarity,
                             QJsonObject{{QStringLiteral("first_embedding_count"), visual.firstCount},
                                         {QStringLiteral("second_embedding_count"), visual.secondCount}}));
    components.insert(QStringLiteral("siglip_temporal_alignment"),
                      signal(visual.alignmentAvailable, visual.alignment,
                             QJsonObject{{QStringLiteral("score_semantics"), QStringLiteral("bidirectional_nearest_neighbor_normalized_order_alignment")}}));

    const EmbeddingPairSignal text = embeddingPairSignal(embeddings, kMiniLmModel, kMiniLmRevision, QStringLiteral("text"),
                                                         firstSource, firstFingerprint, secondSource, secondFingerprint);
    components.insert(QStringLiteral("minilm_text"),
                      signal(text.available, text.similarity,
                             QJsonObject{{QStringLiteral("first_embedding_count"), text.firstCount},
                                         {QStringLiteral("second_embedding_count"), text.secondCount}}));

    const QSet<QString> firstTokens = sourceTokens(evidence, firstSource, firstFingerprint);
    const QSet<QString> secondTokens = sourceTokens(evidence, secondSource, secondFingerprint);
    components.insert(QStringLiteral("lexical_text"),
                      signal(!firstTokens.isEmpty() && !secondTokens.isEmpty(), jaccard(firstTokens, secondTokens),
                             QJsonObject{{QStringLiteral("first_unique_tokens"), firstTokens.size()},
                                         {QStringLiteral("second_unique_tokens"), secondTokens.size()},
                                         {QStringLiteral("score_semantics"), QStringLiteral("source_bound_transcript_ocr_token_jaccard")}}));

    const int firstDuration = qMax(0, first->getFramePlaytime());
    const int secondDuration = qMax(0, second->getFramePlaytime());
    const bool durationAvailable = firstDuration > 0 && secondDuration > 0;
    const double durationScore = durationAvailable
                                     ? static_cast<double>(qMin(firstDuration, secondDuration)) / qMax(firstDuration, secondDuration)
                                     : 0.0;
    components.insert(QStringLiteral("duration"),
                      signal(durationAvailable, durationScore,
                             QJsonObject{{QStringLiteral("first_duration_frames"), firstDuration},
                                         {QStringLiteral("second_duration_frames"), secondDuration},
                                         {QStringLiteral("score_semantics"), QStringLiteral("min_duration_over_max_duration")}}));

    QJsonObject fused = fuseVibeCutDuplicateSignals(components);
    fused.insert(QStringLiteral("ok"), true);
    fused.insert(QStringLiteral("first_bin_id"), firstBin);
    fused.insert(QStringLiteral("second_bin_id"), secondBin);
    fused.insert(QStringLiteral("first_source_fingerprint"), firstFingerprint);
    fused.insert(QStringLiteral("second_source_fingerprint"), secondFingerprint);
    QJsonArray missing;
    if (!components.value(QStringLiteral("mpeg7")).toObject().value(QStringLiteral("available")).toBool()) missing.append(QStringLiteral("run media_similarity_compare for this pair"));
    if (!visual.available) missing.append(QStringLiteral("run semantic_visual_refresh for both assets"));
    if (!text.available) missing.append(QStringLiteral("run semantic_text_refresh when source-bound transcript/OCR text exists"));
    if (firstTokens.isEmpty() || secondTokens.isEmpty()) missing.append(QStringLiteral("add or refresh source-bound transcript/OCR evidence"));
    fused.insert(QStringLiteral("missing_evidence"), missing);
    return fused;
}
} // namespace

QJsonObject fuseVibeCutDuplicateSignals(const QJsonObject &components)
{
    const QJsonObject weights{{QStringLiteral("mpeg7"), 0.30},
                              {QStringLiteral("siglip_visual"), 0.25},
                              {QStringLiteral("siglip_temporal_alignment"), 0.10},
                              {QStringLiteral("minilm_text"), 0.15},
                              {QStringLiteral("lexical_text"), 0.10},
                              {QStringLiteral("duration"), 0.10}};
    double weighted = 0.0;
    double availableWeight = 0.0;
    int signalCount = 0;
    QJsonObject annotated;
    QJsonArray invalid;
    for (auto it = weights.constBegin(); it != weights.constEnd(); ++it) {
        const QString name = it.key();
        const double weight = it.value().toDouble();
        QJsonObject component = components.value(name).toObject();
        const bool available = component.value(QStringLiteral("available")).toBool(false);
        const double rawScore = component.value(QStringLiteral("score")).toDouble(-1.0);
        if (available && (!std::isfinite(rawScore) || rawScore < 0.0 || rawScore > 1.0)) {
            invalid.append(name);
            component.insert(QStringLiteral("accepted"), false);
            component.insert(QStringLiteral("weight"), weight);
            annotated.insert(name, component);
            continue;
        }
        component.insert(QStringLiteral("accepted"), available);
        component.insert(QStringLiteral("weight"), weight);
        annotated.insert(name, component);
        if (!available) continue;
        weighted += rawScore * weight;
        availableWeight += weight;
        ++signalCount;
    }
    const double fusionScore = availableWeight > 0.0 ? qBound(0.0, weighted / availableWeight, 1.0) : 0.0;
    QString classification;
    if (signalCount < 2 || availableWeight < 0.40) classification = QStringLiteral("insufficient_evidence");
    else if (fusionScore >= 0.90 && signalCount >= 3) classification = QStringLiteral("strong_duplicate_candidate");
    else if (fusionScore >= 0.75) classification = QStringLiteral("near_duplicate_candidate");
    else if (fusionScore >= 0.55) classification = QStringLiteral("possible_related_candidate");
    else classification = QStringLiteral("weak_duplicate_evidence");

    return QJsonObject{{QStringLiteral("authority"), QStringLiteral("derived_candidate")},
                       {QStringLiteral("candidate_kind"), QStringLiteral("duplicate_or_near_duplicate")},
                       {QStringLiteral("score_semantics"), QStringLiteral("weighted_available_evidence_similarity_not_probability")},
                       {QStringLiteral("fusion_score"), fusionScore},
                       {QStringLiteral("available_weight"), availableWeight},
                       {QStringLiteral("independent_signal_count"), signalCount},
                       {QStringLiteral("classification"), classification},
                       {QStringLiteral("components"), annotated},
                       {QStringLiteral("invalid_components"), invalid}};
}

bool registerVibeCutDuplicateFusionTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("first_bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("second_bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("first_bin_id"), QStringLiteral("second_bin_id")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("media_duplicate_fusion");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Fuse current source-fingerprint MPEG-7, SigLIP visual similarity/order alignment, MiniLM source-text similarity, transcript/OCR token overlap and duration evidence for two file-backed video assets. Returns a derived duplicate/near-duplicate candidate score with coverage and missing-evidence disclosure; the score is not a probability or duplicate fact.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, duplicateTool, error);
}
