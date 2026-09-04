/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuteditorialeval.h"

#include "vibecuteditorialcase.h"
#include "vibecuteditorialreview.h"
#include "vibecuttoolsurface.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace {
bool parseIds(const QJsonArray &input, QStringList &ids, QString *error, const QString &label)
{
    ids.clear();
    if (input.size() > 100) {
        if (error) *error = QStringLiteral("%1 candidate list exceeds the 100-item evaluation bound.").arg(label);
        return false;
    }
    QSet<QString> seen;
    for (const QJsonValue &value : input) {
        if (!value.isString()) {
            if (error) *error = QStringLiteral("%1 candidate list may contain strings only.").arg(label);
            return false;
        }
        const QString id = value.toString().trimmed();
        if (id.isEmpty() || id.size() > 1024 || seen.contains(id)) {
            if (error) *error = QStringLiteral("%1 candidate ids must be unique non-empty strings up to 1024 characters.").arg(label);
            return false;
        }
        seen.insert(id);
        ids.append(id);
    }
    return true;
}

QSet<QString> toSet(const QStringList &ids)
{
    QSet<QString> result;
    for (const QString &id : ids) result.insert(id);
    return result;
}

QJsonObject toolHandler(const QJsonObject &input)
{
    const QJsonValue expectedValue = input.value(QStringLiteral("expected_candidate_ids"));
    const QJsonValue actualValue = input.value(QStringLiteral("actual_candidate_ids"));
    if (!expectedValue.isArray() || !actualValue.isArray()) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("expected_candidate_ids and actual_candidate_ids must be arrays.")}};
    }
    QString error;
    QJsonObject result = evaluateVibeCutEditorialSelection(expectedValue.toArray(), actualValue.toArray(), &error);
    if (!error.isEmpty()) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    }
    result.insert(QStringLiteral("ok"), true);
    return result;
}
} // namespace

QJsonObject evaluateVibeCutEditorialSelection(const QJsonArray &expectedCandidateIds,
                                              const QJsonArray &actualCandidateIds,
                                              QString *error)
{
    if (error) error->clear();
    QStringList expected;
    QStringList actual;
    if (!parseIds(expectedCandidateIds, expected, error, QStringLiteral("Expected")) ||
        !parseIds(actualCandidateIds, actual, error, QStringLiteral("Actual"))) return {};

    const QSet<QString> expectedSet = toSet(expected);
    const QSet<QString> actualSet = toSet(actual);
    const QSet<QString> common = expectedSet & actualSet;
    const QSet<QString> missed = expectedSet - actualSet;
    const QSet<QString> unexpected = actualSet - expectedSet;

    const double precision = actual.isEmpty() ? (expected.isEmpty() ? 1.0 : 0.0)
                                               : static_cast<double>(common.size()) / actual.size();
    const double recall = expected.isEmpty() ? (actual.isEmpty() ? 1.0 : 0.0)
                                             : static_cast<double>(common.size()) / expected.size();
    const double f1 = precision + recall > 0.0 ? 2.0 * precision * recall / (precision + recall) : 0.0;

    QHash<QString, int> expectedPosition;
    QHash<QString, int> actualPosition;
    for (int i = 0; i < expected.size(); ++i) expectedPosition.insert(expected.at(i), i);
    for (int i = 0; i < actual.size(); ++i) actualPosition.insert(actual.at(i), i);

    QStringList commonInExpectedOrder;
    for (const QString &id : expected) if (common.contains(id)) commonInExpectedOrder.append(id);
    qint64 pairCount = 0;
    qint64 agreeingPairs = 0;
    for (int i = 0; i < commonInExpectedOrder.size(); ++i) {
        for (int j = i + 1; j < commonInExpectedOrder.size(); ++j) {
            const QString &a = commonInExpectedOrder.at(i);
            const QString &b = commonInExpectedOrder.at(j);
            ++pairCount;
            const bool expectedBefore = expectedPosition.value(a) < expectedPosition.value(b);
            const bool actualBefore = actualPosition.value(a) < actualPosition.value(b);
            if (expectedBefore == actualBefore) ++agreeingPairs;
        }
    }
    const double orderAgreement = pairCount > 0 ? static_cast<double>(agreeingPairs) / pairCount : 1.0;

    QStringList missedSorted = missed.values();
    QStringList unexpectedSorted = unexpected.values();
    std::sort(missedSorted.begin(), missedSorted.end());
    std::sort(unexpectedSorted.begin(), unexpectedSorted.end());
    QJsonArray missedJson;
    for (const QString &id : missedSorted) missedJson.append(id);
    QJsonArray unexpectedJson;
    for (const QString &id : unexpectedSorted) unexpectedJson.append(id);

    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("authority"), QStringLiteral("evaluation")},
                       {QStringLiteral("evaluation_semantics"), QStringLiteral("candidate_selection_and_order_agreement_against_explicit_reference_not_editorial_quality")},
                       {QStringLiteral("expected_count"), expected.size()},
                       {QStringLiteral("actual_count"), actual.size()},
                       {QStringLiteral("common_count"), common.size()},
                       {QStringLiteral("precision"), precision},
                       {QStringLiteral("recall"), recall},
                       {QStringLiteral("f1"), f1},
                       {QStringLiteral("exact_set_match"), missed.isEmpty() && unexpected.isEmpty()},
                       {QStringLiteral("exact_order_match"), expected == actual},
                       {QStringLiteral("common_order_pair_count"), pairCount},
                       {QStringLiteral("common_order_agreeing_pair_count"), agreeingPairs},
                       {QStringLiteral("common_order_pair_agreement"), orderAgreement},
                       {QStringLiteral("missed_candidate_ids"), missedJson},
                       {QStringLiteral("unexpected_candidate_ids"), unexpectedJson},
                       {QStringLiteral("quality_claim"), false}};
}

bool registerVibeCutEditorialEvalTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject candidateArray{{QStringLiteral("type"), QStringLiteral("array")},
                                     {QStringLiteral("maxItems"), 100},
                                     {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                           {QStringLiteral("minLength"), 1},
                                                                           {QStringLiteral("maxLength"), 1024}}}};
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("expected_candidate_ids"), candidateArray},
                                {QStringLiteral("actual_candidate_ids"), candidateArray}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("expected_candidate_ids"), QStringLiteral("actual_candidate_ids")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("editorial_selection_evaluate");
    policy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                          {QStringLiteral("description"), QStringLiteral("Compare an actual candidate-ID selection/order with an explicit human/golden reference using precision, recall, F1, exact-set/order and relative-order agreement metrics. This measures agreement only and never claims intrinsic editorial quality.")},
                                          {QStringLiteral("input_schema"), input}},
                              policy, toolHandler, error)) return false;
    if (!registerVibeCutEditorialReviewTools(surface, error)) return false;
    return registerVibeCutEditorialCaseTools(surface, error);
}
