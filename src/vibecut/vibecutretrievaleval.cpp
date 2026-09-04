/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutretrievaleval.h"

#include "vibecutduplicateeval.h"
#include "vibecuttoolsurface.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kMaxRankedItems = 2000;

bool parseIds(const QJsonArray &array, const QString &label, bool requireNonEmpty,
              QStringList &ids, QString *error)
{
    ids.clear();
    if ((requireNonEmpty && array.isEmpty()) || array.size() > kMaxRankedItems) {
        if (error) *error = QStringLiteral("%1 must contain %2..%3 IDs.").arg(label).arg(requireNonEmpty ? 1 : 0).arg(kMaxRankedItems);
        return false;
    }
    QSet<QString> seen;
    for (const QJsonValue &value : array) {
        if (!value.isString()) {
            if (error) *error = QStringLiteral("%1 may contain strings only.").arg(label);
            return false;
        }
        const QString id = value.toString().trimmed();
        if (id.isEmpty() || id.size() > 1024 || seen.contains(id)) {
            if (error) *error = QStringLiteral("%1 IDs must be unique non-empty strings up to 1024 characters.").arg(label);
            return false;
        }
        seen.insert(id);
        ids.append(id);
    }
    return true;
}

bool exactK(const QJsonValue &value, int &k, QString *error)
{
    if (!value.isDouble()) {
        if (error) *error = QStringLiteral("k must be an integer 1..%1.").arg(kMaxRankedItems);
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < 1.0 || number > kMaxRankedItems) {
        if (error) *error = QStringLiteral("k must be an integer 1..%1.").arg(kMaxRankedItems);
        return false;
    }
    k = static_cast<int>(number);
    return true;
}

QJsonObject toolHandler(const QJsonObject &input)
{
    if (!input.value(QStringLiteral("relevant_ids")).isArray() || !input.value(QStringLiteral("ranked_ids")).isArray()) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("relevant_ids and ranked_ids must be arrays.")}};
    }
    int k = 0;
    QString error;
    if (!exactK(input.value(QStringLiteral("k")), k, &error)) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    }
    QJsonObject result = evaluateVibeCutRetrievalRanking(input.value(QStringLiteral("relevant_ids")).toArray(),
                                                         input.value(QStringLiteral("ranked_ids")).toArray(), k, &error);
    if (!error.isEmpty()) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    result.insert(QStringLiteral("ok"), true);
    return result;
}
} // namespace

QJsonObject evaluateVibeCutRetrievalRanking(const QJsonArray &relevantIds,
                                            const QJsonArray &rankedIds,
                                            int k,
                                            QString *error)
{
    if (error) error->clear();
    if (k < 1 || k > kMaxRankedItems) {
        if (error) *error = QStringLiteral("k must be 1..%1.").arg(kMaxRankedItems);
        return {};
    }
    QStringList relevant;
    QStringList ranked;
    if (!parseIds(relevantIds, QStringLiteral("Relevant reference"), true, relevant, error) ||
        !parseIds(rankedIds, QStringLiteral("Ranked result"), false, ranked, error)) return {};

    QSet<QString> relevantSet;
    for (const QString &id : relevant) relevantSet.insert(id);
    QSet<QString> foundTotal;
    int hitsAtK = 0;
    double averagePrecisionNumerator = 0.0;
    double dcg = 0.0;
    int firstRelevantRank = -1;
    const int inspectCount = qMin(k, ranked.size());
    for (int i = 0; i < ranked.size(); ++i) {
        const QString &id = ranked.at(i);
        if (!relevantSet.contains(id)) continue;
        foundTotal.insert(id);
        const int rank = i + 1;
        if (firstRelevantRank < 0) firstRelevantRank = rank;
        if (rank <= k) {
            ++hitsAtK;
            averagePrecisionNumerator += static_cast<double>(hitsAtK) / rank;
            dcg += 1.0 / std::log2(static_cast<double>(rank) + 1.0);
        }
    }

    const int idealHits = qMin(k, relevant.size());
    double idcg = 0.0;
    for (int rank = 1; rank <= idealHits; ++rank) {
        idcg += 1.0 / std::log2(static_cast<double>(rank) + 1.0);
    }
    const double precisionAtK = static_cast<double>(hitsAtK) / k;
    const double recallAtK = static_cast<double>(hitsAtK) / relevant.size();
    const double averagePrecisionAtK = idealHits > 0 ? averagePrecisionNumerator / idealHits : 0.0;
    const double ndcgAtK = idcg > 0.0 ? dcg / idcg : 0.0;
    const double reciprocalRank = firstRelevantRank > 0 ? 1.0 / firstRelevantRank : 0.0;
    const double totalRecall = static_cast<double>(foundTotal.size()) / relevant.size();

    QStringList missing;
    for (const QString &id : relevant) if (!foundTotal.contains(id)) missing.append(id);
    std::sort(missing.begin(), missing.end());
    QJsonArray missingJson;
    for (const QString &id : missing) missingJson.append(id);

    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("evaluation")},
                       {QStringLiteral("evaluation_semantics"), QStringLiteral("ranked_retrieval_agreement_against_explicit_relevance_reference_not_semantic_truth_or_editorial_quality")},
                       {QStringLiteral("k"), k},
                       {QStringLiteral("max_supported_ranked_items"), kMaxRankedItems},
                       {QStringLiteral("relevant_count"), relevant.size()},
                       {QStringLiteral("ranked_count"), ranked.size()},
                       {QStringLiteral("returned_within_k"), inspectCount},
                       {QStringLiteral("relevant_hits_at_k"), hitsAtK},
                       {QStringLiteral("precision_at_k"), precisionAtK},
                       {QStringLiteral("precision_at_k_denominator"), QStringLiteral("requested_k_including_unfilled_slots")},
                       {QStringLiteral("recall_at_k"), recallAtK},
                       {QStringLiteral("hit_rate_at_k"), hitsAtK > 0 ? 1.0 : 0.0},
                       {QStringLiteral("average_precision_at_k"), averagePrecisionAtK},
                       {QStringLiteral("ndcg_at_k_binary_relevance"), ndcgAtK},
                       {QStringLiteral("first_relevant_rank"), firstRelevantRank},
                       {QStringLiteral("reciprocal_rank"), reciprocalRank},
                       {QStringLiteral("relevant_found_anywhere_count"), foundTotal.size()},
                       {QStringLiteral("recall_over_full_ranked_list"), totalRecall},
                       {QStringLiteral("missing_relevant_ids"), missingJson},
                       {QStringLiteral("quality_claim"), false}};
}

bool registerVibeCutRetrievalEvalTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject idArray{{QStringLiteral("type"), QStringLiteral("array")},
                              {QStringLiteral("maxItems"), kMaxRankedItems},
                              {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                    {QStringLiteral("minLength"), 1},
                                                                    {QStringLiteral("maxLength"), 1024}}}};
    QJsonObject relevantArray = idArray;
    relevantArray.insert(QStringLiteral("minItems"), 1);
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("relevant_ids"), relevantArray},
                                {QStringLiteral("ranked_ids"), idArray},
                                {QStringLiteral("k"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                                   {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), kMaxRankedItems}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("relevant_ids"), QStringLiteral("ranked_ids"), QStringLiteral("k")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("retrieval_ranking_evaluate");
    policy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                          {QStringLiteral("description"), QStringLiteral("Evaluate a ranked retrieval/duplicate-candidate ID list of up to 2,000 items against an explicit relevance reference using precision@k, recall@k, AP@k, binary nDCG@k, reciprocal rank and full-list recall. Metrics measure reference agreement only, not semantic truth or editorial quality.")},
                                          {QStringLiteral("input_schema"), input}},
                              policy, toolHandler, error)) return false;
    return registerVibeCutDuplicateEvalTools(surface, error);
}
