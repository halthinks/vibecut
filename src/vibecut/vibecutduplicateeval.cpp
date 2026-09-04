/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutduplicateeval.h"

#include "vibecutretrievaleval.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
const QSet<QString> kClassifications{
    QStringLiteral("insufficient_evidence"),
    QStringLiteral("strong_duplicate_candidate"),
    QStringLiteral("near_duplicate_candidate"),
    QStringLiteral("possible_related_candidate"),
    QStringLiteral("weak_duplicate_evidence"),
};

bool parsePair(const QJsonValue &value, bool ranked, QString &pairId, QJsonObject &normalized, QString *error)
{
    if (!value.isObject()) {
        if (error) *error = QStringLiteral("Every duplicate pair must be an object.");
        return false;
    }
    const QJsonObject object = value.toObject();
    if (!object.value(QStringLiteral("first_bin_id")).isString() ||
        !object.value(QStringLiteral("second_bin_id")).isString()) {
        if (error) *error = QStringLiteral("Duplicate pair first_bin_id and second_bin_id must be strings.");
        return false;
    }
    QString first = object.value(QStringLiteral("first_bin_id")).toString().trimmed();
    QString second = object.value(QStringLiteral("second_bin_id")).toString().trimmed();
    pairId = vibeCutDuplicatePairId(first, second, error);
    if (pairId.isEmpty()) return false;
    if (second < first) std::swap(first, second);
    normalized = QJsonObject{{QStringLiteral("pair_id"), pairId},
                             {QStringLiteral("first_bin_id"), first},
                             {QStringLiteral("second_bin_id"), second}};

    if (!ranked) return true;
    if (object.contains(QStringLiteral("fusion_score"))) {
        const QJsonValue raw = object.value(QStringLiteral("fusion_score"));
        if (!raw.isDouble() || !std::isfinite(raw.toDouble()) || raw.toDouble() < 0.0 || raw.toDouble() > 1.0) {
            if (error) *error = QStringLiteral("Ranked pair fusion_score must be finite and between 0 and 1 when supplied.");
            return false;
        }
        normalized.insert(QStringLiteral("fusion_score"), raw.toDouble());
    }
    if (object.contains(QStringLiteral("classification"))) {
        if (!object.value(QStringLiteral("classification")).isString()) {
            if (error) *error = QStringLiteral("Ranked pair classification must be a string when supplied.");
            return false;
        }
        const QString classification = object.value(QStringLiteral("classification")).toString().trimmed();
        if (!kClassifications.contains(classification)) {
            if (error) *error = QStringLiteral("Unknown duplicate-fusion classification '%1'.").arg(classification);
            return false;
        }
        normalized.insert(QStringLiteral("classification"), classification);
    }
    if (object.contains(QStringLiteral("independent_signal_count"))) {
        const QJsonValue raw = object.value(QStringLiteral("independent_signal_count"));
        const double number = raw.toDouble(-1.0);
        if (!raw.isDouble() || !std::isfinite(number) || std::floor(number) != number || number < 0.0 || number > 6.0) {
            if (error) *error = QStringLiteral("Ranked pair independent_signal_count must be an integer 0..6 when supplied.");
            return false;
        }
        normalized.insert(QStringLiteral("independent_signal_count"), static_cast<int>(number));
    }
    if (object.contains(QStringLiteral("available_weight"))) {
        const QJsonValue raw = object.value(QStringLiteral("available_weight"));
        if (!raw.isDouble() || !std::isfinite(raw.toDouble()) || raw.toDouble() < 0.0 || raw.toDouble() > 1.0) {
            if (error) *error = QStringLiteral("Ranked pair available_weight must be finite and between 0 and 1 when supplied.");
            return false;
        }
        normalized.insert(QStringLiteral("available_weight"), raw.toDouble());
    }
    return true;
}

bool parsePairs(const QJsonArray &input, bool ranked, bool requireNonEmpty,
                QJsonArray &ids, QJsonArray &normalized, QString *error)
{
    ids = {};
    normalized = {};
    if ((requireNonEmpty && input.isEmpty()) || input.size() > 1000) {
        if (error) *error = QStringLiteral("%1 duplicate pairs must contain %2..1000 entries.")
                               .arg(ranked ? QStringLiteral("Ranked") : QStringLiteral("Relevant reference"))
                               .arg(requireNonEmpty ? 1 : 0);
        return false;
    }
    QSet<QString> seen;
    for (const QJsonValue &value : input) {
        QString pairId;
        QJsonObject pair;
        if (!parsePair(value, ranked, pairId, pair, error)) return false;
        if (seen.contains(pairId)) {
            if (error) *error = QStringLiteral("Duplicate pair list contains the same unordered asset pair more than once.");
            return false;
        }
        seen.insert(pairId);
        ids.append(pairId);
        normalized.append(pair);
    }
    return true;
}

bool exactK(const QJsonValue &value, int &k, QString *error)
{
    if (!value.isDouble()) {
        if (error) *error = QStringLiteral("k must be an integer 1..1000.");
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < 1.0 || number > 1000.0) {
        if (error) *error = QStringLiteral("k must be an integer 1..1000.");
        return false;
    }
    k = static_cast<int>(number);
    return true;
}

QJsonObject toolHandler(const QJsonObject &input)
{
    if (!input.value(QStringLiteral("relevant_pairs")).isArray() || !input.value(QStringLiteral("ranked_pairs")).isArray()) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("relevant_pairs and ranked_pairs must be arrays.")}};
    }
    int k = 0;
    QString error;
    if (!exactK(input.value(QStringLiteral("k")), k, &error)) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    }
    QJsonObject result = evaluateVibeCutDuplicateRanking(input.value(QStringLiteral("relevant_pairs")).toArray(),
                                                         input.value(QStringLiteral("ranked_pairs")).toArray(), k, &error);
    if (!error.isEmpty()) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    result.insert(QStringLiteral("ok"), true);
    return result;
}
} // namespace

QString vibeCutDuplicatePairId(const QString &firstBinId, const QString &secondBinId, QString *error)
{
    if (error) error->clear();
    QString first = firstBinId.trimmed();
    QString second = secondBinId.trimmed();
    if (first.isEmpty() || second.isEmpty() || first.size() > 1024 || second.size() > 1024) {
        if (error) *error = QStringLiteral("Duplicate pair asset IDs must contain 1..1024 characters.");
        return {};
    }
    if (first == second) {
        if (error) *error = QStringLiteral("Duplicate evaluation requires two distinct asset IDs per pair.");
        return {};
    }
    if (second < first) std::swap(first, second);
    const QJsonArray identity{first, second};
    const QByteArray payload = QJsonDocument(identity).toJson(QJsonDocument::Compact);
    return QStringLiteral("pair:%1").arg(QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex()));
}

QJsonObject evaluateVibeCutDuplicateRanking(const QJsonArray &relevantPairs,
                                            const QJsonArray &rankedPairs,
                                            int k,
                                            QString *error)
{
    if (error) error->clear();
    QJsonArray relevantIds;
    QJsonArray rankedIds;
    QJsonArray normalizedRelevant;
    QJsonArray normalizedRanked;
    if (!parsePairs(relevantPairs, false, true, relevantIds, normalizedRelevant, error) ||
        !parsePairs(rankedPairs, true, false, rankedIds, normalizedRanked, error)) return {};

    QString metricError;
    QJsonObject result = evaluateVibeCutRetrievalRanking(relevantIds, rankedIds, k, &metricError);
    if (!metricError.isEmpty()) {
        if (error) *error = metricError;
        return {};
    }

    QJsonObject classificationCounts;
    int classificationAvailable = 0;
    int insufficientAtK = 0;
    int signalCountAvailable = 0;
    qint64 signalCountSum = 0;
    int availableWeightAvailable = 0;
    double availableWeightSum = 0.0;
    const int inspectCount = qMin(k, normalizedRanked.size());
    for (int i = 0; i < inspectCount; ++i) {
        const QJsonObject pair = normalizedRanked.at(i).toObject();
        if (pair.contains(QStringLiteral("classification"))) {
            const QString classification = pair.value(QStringLiteral("classification")).toString();
            classificationCounts.insert(classification, classificationCounts.value(classification).toInt(0) + 1);
            ++classificationAvailable;
            if (classification == QLatin1String("insufficient_evidence")) ++insufficientAtK;
        }
        if (pair.contains(QStringLiteral("independent_signal_count"))) {
            signalCountSum += pair.value(QStringLiteral("independent_signal_count")).toInt();
            ++signalCountAvailable;
        }
        if (pair.contains(QStringLiteral("available_weight"))) {
            availableWeightSum += pair.value(QStringLiteral("available_weight")).toDouble();
            ++availableWeightAvailable;
        }
    }

    result.insert(QStringLiteral("evaluation_semantics"), QStringLiteral("ranked_duplicate_pair_agreement_against_explicit_reference_not_duplicate_truth_or_probability_calibration"));
    result.insert(QStringLiteral("pair_identity_semantics"), QStringLiteral("order_independent_sha256_of_sorted_asset_ids"));
    result.insert(QStringLiteral("normalized_relevant_pairs"), normalizedRelevant);
    result.insert(QStringLiteral("normalized_ranked_pairs"), normalizedRanked);
    result.insert(QStringLiteral("classification_metadata_available_at_k"), classificationAvailable);
    result.insert(QStringLiteral("classification_counts_at_k"), classificationCounts);
    result.insert(QStringLiteral("insufficient_evidence_count_at_k"), insufficientAtK);
    result.insert(QStringLiteral("mean_independent_signal_count_at_k"),
                  signalCountAvailable > 0 ? static_cast<double>(signalCountSum) / signalCountAvailable : -1.0);
    result.insert(QStringLiteral("mean_available_weight_at_k"),
                  availableWeightAvailable > 0 ? availableWeightSum / availableWeightAvailable : -1.0);
    result.insert(QStringLiteral("duplicate_truth_claim"), false);
    result.insert(QStringLiteral("quality_claim"), false);
    return result;
}

bool registerVibeCutDuplicateEvalTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject pairProperties{
        {QStringLiteral("first_bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 1024}}},
        {QStringLiteral("second_bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 1024}}},
    };
    const QJsonObject referencePair{{QStringLiteral("type"), QStringLiteral("object")},
                                    {QStringLiteral("properties"), pairProperties},
                                    {QStringLiteral("required"), QJsonArray{QStringLiteral("first_bin_id"), QStringLiteral("second_bin_id")}},
                                    {QStringLiteral("additionalProperties"), false}};
    QJsonObject rankedProperties = pairProperties;
    rankedProperties.insert(QStringLiteral("fusion_score"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}});
    rankedProperties.insert(QStringLiteral("classification"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                           {QStringLiteral("enum"), QJsonArray{QStringLiteral("insufficient_evidence"),
                                                                                                                 QStringLiteral("strong_duplicate_candidate"),
                                                                                                                 QStringLiteral("near_duplicate_candidate"),
                                                                                                                 QStringLiteral("possible_related_candidate"),
                                                                                                                 QStringLiteral("weak_duplicate_evidence")}}}});
    rankedProperties.insert(QStringLiteral("independent_signal_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 6}});
    rankedProperties.insert(QStringLiteral("available_weight"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}});
    const QJsonObject rankedPair{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), rankedProperties},
                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("first_bin_id"), QStringLiteral("second_bin_id")}},
                                 {QStringLiteral("additionalProperties"), false}};

    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("relevant_pairs"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                              {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 1000},
                                                                              {QStringLiteral("items"), referencePair}}},
                                {QStringLiteral("ranked_pairs"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                            {QStringLiteral("maxItems"), 1000},
                                                                            {QStringLiteral("items"), rankedPair}}},
                                {QStringLiteral("k"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                                   {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 1000}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("relevant_pairs"), QStringLiteral("ranked_pairs"), QStringLiteral("k")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("duplicate_ranking_evaluate");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Evaluate ranked duplicate/near-duplicate asset pairs against an explicit reference set. Pair identity is order-independent; metrics reuse retrieval precision/recall/AP/nDCG/rank agreement while fusion classifications and evidence coverage remain diagnostic metadata, never duplicate truth.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, toolHandler, error);
}
