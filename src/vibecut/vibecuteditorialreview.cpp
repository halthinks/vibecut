/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuteditorialreview.h"

#include "vibecuttoolsurface.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
const QString kRubric = QStringLiteral("VibeCutEditorialReview-v1");
const QStringList kCriteria{
    QStringLiteral("objective_relevance"),
    QStringLiteral("narrative_coherence"),
    QStringLiteral("pacing_fit"),
    QStringLiteral("source_fidelity"),
    QStringLiteral("overall_preference"),
};

bool boundedString(const QJsonObject &object, const QString &key, int maxLength, QString &value, QString *error)
{
    if (!object.value(key).isString()) {
        if (error) *error = QStringLiteral("%1 must be a string.").arg(key);
        return false;
    }
    value = object.value(key).toString().trimmed();
    if (value.isEmpty() || value.size() > maxLength) {
        if (error) *error = QStringLiteral("%1 must contain 1..%2 characters.").arg(key).arg(maxLength);
        return false;
    }
    return true;
}

bool sha256Field(const QJsonObject &object, const QString &key, QString &value, QString *error)
{
    if (!object.value(key).isString()) {
        if (error) *error = QStringLiteral("%1 must be a SHA-256 hex string.").arg(key);
        return false;
    }
    value = object.value(key).toString().trimmed().toLower();
    if (value.size() != 64) {
        if (error) *error = QStringLiteral("%1 must contain exactly 64 hexadecimal characters.").arg(key);
        return false;
    }
    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        const bool digit = code >= '0' && code <= '9';
        const bool lowerHex = code >= 'a' && code <= 'f';
        if (!digit && !lowerHex) {
            if (error) *error = QStringLiteral("%1 must contain hexadecimal characters only.").arg(key);
            return false;
        }
    }
    return true;
}

bool scoreValue(const QJsonObject &scores, const QString &criterion, int &score, QString *error)
{
    const QJsonValue value = scores.value(criterion);
    if (!value.isDouble()) {
        if (error) *error = QStringLiteral("Review score '%1' must be an integer 1..5.").arg(criterion);
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < 1.0 || number > 5.0) {
        if (error) *error = QStringLiteral("Review score '%1' must be an integer 1..5.").arg(criterion);
        return false;
    }
    score = static_cast<int>(number);
    return true;
}

bool supportedTask(const QString &task)
{
    return task == QLatin1String("rough_cut") || task == QLatin1String("highlight") || task == QLatin1String("broll");
}

QJsonObject metric(const QList<int> &values)
{
    if (values.isEmpty()) return QJsonObject{{QStringLiteral("count"), 0}, {QStringLiteral("available"), false}};
    long double sum = 0.0L;
    for (int value : values) sum += value;
    const double mean = static_cast<double>(sum / values.size());
    long double variance = 0.0L;
    for (int value : values) {
        const long double delta = static_cast<long double>(value) - mean;
        variance += delta * delta;
    }
    variance /= values.size();
    const double stddev = std::sqrt(static_cast<double>(variance));
    return QJsonObject{{QStringLiteral("count"), values.size()},
                       {QStringLiteral("available"), true},
                       {QStringLiteral("mean"), mean},
                       {QStringLiteral("stddev"), stddev},
                       {QStringLiteral("min"), *std::min_element(values.begin(), values.end())},
                       {QStringLiteral("max"), *std::max_element(values.begin(), values.end())},
                       {QStringLiteral("scale_min"), 1},
                       {QStringLiteral("scale_max"), 5}};
}

QJsonObject validateTool(const QJsonObject &input)
{
    if (!input.value(QStringLiteral("review")).isObject()) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("review must be an object.")}};
    }
    QString error;
    QJsonObject result = validateVibeCutEditorialReview(input.value(QStringLiteral("review")).toObject(), &error);
    if (!error.isEmpty()) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject aggregateTool(const QJsonObject &input)
{
    if (!input.value(QStringLiteral("reviews")).isArray()) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("reviews must be an array.")}};
    }
    QString error;
    QJsonObject result = aggregateVibeCutEditorialReviews(input.value(QStringLiteral("reviews")).toArray(), &error);
    if (!error.isEmpty()) return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    result.insert(QStringLiteral("ok"), true);
    return result;
}
} // namespace

QString vibeCutEditorialReviewRubricId()
{
    return kRubric;
}

QJsonObject validateVibeCutEditorialReview(const QJsonObject &review, QString *error)
{
    if (error) error->clear();
    if (review.value(QStringLiteral("schema_version")).toInt(-1) != 1) {
        if (error) *error = QStringLiteral("Editorial review schema_version must be 1.");
        return {};
    }
    if (review.value(QStringLiteral("rubric_id")).toString() != kRubric) {
        if (error) *error = QStringLiteral("Editorial review rubric_id must be %1.").arg(kRubric);
        return {};
    }
    if (!review.value(QStringLiteral("blind")).isBool() || !review.value(QStringLiteral("blind")).toBool(false)) {
        if (error) *error = QStringLiteral("Editorial review v1 requires blind=true.");
        return {};
    }

    QString caseId;
    QString candidateId;
    QString reviewerId;
    QString taskType;
    QString contextSha;
    QString proposalId;
    if (!boundedString(review, QStringLiteral("case_id"), 128, caseId, error) ||
        !boundedString(review, QStringLiteral("candidate_id"), 128, candidateId, error) ||
        !boundedString(review, QStringLiteral("reviewer_id"), 128, reviewerId, error) ||
        !boundedString(review, QStringLiteral("task_type"), 32, taskType, error) ||
        !sha256Field(review, QStringLiteral("context_sha256"), contextSha, error) ||
        !sha256Field(review, QStringLiteral("proposal_id"), proposalId, error)) return {};
    if (!supportedTask(taskType)) {
        if (error) *error = QStringLiteral("task_type must be rough_cut, highlight, or broll.");
        return {};
    }
    if (!review.value(QStringLiteral("scores")).isObject()) {
        if (error) *error = QStringLiteral("Editorial review requires a scores object.");
        return {};
    }
    const QJsonObject scores = review.value(QStringLiteral("scores")).toObject();
    QJsonObject normalizedScores;
    for (const QString &criterion : kCriteria) {
        int score = 0;
        if (!scoreValue(scores, criterion, score, error)) return {};
        normalizedScores.insert(criterion, score);
    }
    for (auto it = scores.constBegin(); it != scores.constEnd(); ++it) {
        if (!kCriteria.contains(it.key())) {
            if (error) *error = QStringLiteral("Unknown editorial review score criterion '%1'.").arg(it.key());
            return {};
        }
    }

    QString notes;
    if (review.contains(QStringLiteral("notes"))) {
        if (!review.value(QStringLiteral("notes")).isString()) {
            if (error) *error = QStringLiteral("notes must be a string when supplied.");
            return {};
        }
        notes = review.value(QStringLiteral("notes")).toString().trimmed();
        if (notes.size() > 2000) {
            if (error) *error = QStringLiteral("notes may contain at most 2000 characters.");
            return {};
        }
    }

    QJsonObject normalized{{QStringLiteral("schema_version"), 1},
                           {QStringLiteral("rubric_id"), kRubric},
                           {QStringLiteral("authority"), QStringLiteral("human_review")},
                           {QStringLiteral("review_semantics"), QStringLiteral("subjective_blinded_editorial_rating_bound_to_exact_proposal_not_ground_truth")},
                           {QStringLiteral("blind"), true},
                           {QStringLiteral("case_id"), caseId},
                           {QStringLiteral("candidate_id"), candidateId},
                           {QStringLiteral("reviewer_id"), reviewerId},
                           {QStringLiteral("task_type"), taskType},
                           {QStringLiteral("context_sha256"), contextSha},
                           {QStringLiteral("proposal_id"), proposalId},
                           {QStringLiteral("scores"), normalizedScores},
                           {QStringLiteral("quality_ground_truth"), false},
                           {QStringLiteral("mutation_authority"), QStringLiteral("none")}};
    if (!notes.isEmpty()) normalized.insert(QStringLiteral("notes"), notes);
    return normalized;
}

QJsonObject aggregateVibeCutEditorialReviews(const QJsonArray &reviews, QString *error)
{
    if (error) error->clear();
    if (reviews.isEmpty() || reviews.size() > 50) {
        if (error) *error = QStringLiteral("Editorial review aggregation requires 1..50 reviews.");
        return {};
    }

    QString caseId;
    QString candidateId;
    QString taskType;
    QString contextSha;
    QString proposalId;
    QSet<QString> reviewerIds;
    QHash<QString, QList<int>> values;
    QJsonArray normalized;
    for (const QJsonValue &value : reviews) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Every editorial review must be an object.");
            return {};
        }
        QString validationError;
        const QJsonObject review = validateVibeCutEditorialReview(value.toObject(), &validationError);
        if (!validationError.isEmpty()) {
            if (error) *error = validationError;
            return {};
        }
        const QString thisCase = review.value(QStringLiteral("case_id")).toString();
        const QString thisCandidate = review.value(QStringLiteral("candidate_id")).toString();
        const QString thisTask = review.value(QStringLiteral("task_type")).toString();
        const QString thisContext = review.value(QStringLiteral("context_sha256")).toString();
        const QString thisProposal = review.value(QStringLiteral("proposal_id")).toString();
        const QString reviewer = review.value(QStringLiteral("reviewer_id")).toString();
        if (caseId.isEmpty()) {
            caseId = thisCase;
            candidateId = thisCandidate;
            taskType = thisTask;
            contextSha = thisContext;
            proposalId = thisProposal;
        } else if (caseId != thisCase || candidateId != thisCandidate || taskType != thisTask ||
                   contextSha != thisContext || proposalId != thisProposal) {
            if (error) *error = QStringLiteral("All reviews in one aggregate must target the same case, candidate, task, context_sha256 and proposal_id.");
            return {};
        }
        if (reviewerIds.contains(reviewer)) {
            if (error) *error = QStringLiteral("Duplicate reviewer_id '%1' in one aggregate.").arg(reviewer);
            return {};
        }
        reviewerIds.insert(reviewer);
        const QJsonObject scores = review.value(QStringLiteral("scores")).toObject();
        for (const QString &criterion : kCriteria) values[criterion].append(scores.value(criterion).toInt());
        normalized.append(review);
    }

    QJsonObject metrics;
    for (const QString &criterion : kCriteria) metrics.insert(criterion, metric(values.value(criterion)));
    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("rubric_id"), kRubric},
                       {QStringLiteral("authority"), QStringLiteral("human_review_aggregate")},
                       {QStringLiteral("aggregate_semantics"), QStringLiteral("subjective_blinded_editorial_review_summary_bound_to_exact_proposal_not_ground_truth_or_automatic_gate")},
                       {QStringLiteral("case_id"), caseId},
                       {QStringLiteral("candidate_id"), candidateId},
                       {QStringLiteral("task_type"), taskType},
                       {QStringLiteral("context_sha256"), contextSha},
                       {QStringLiteral("proposal_id"), proposalId},
                       {QStringLiteral("review_count"), normalized.size()},
                       {QStringLiteral("criteria"), QJsonArray::fromStringList(kCriteria)},
                       {QStringLiteral("metrics"), metrics},
                       {QStringLiteral("blind_review_required"), true},
                       {QStringLiteral("quality_ground_truth"), false},
                       {QStringLiteral("automatic_execution_gate"), false},
                       {QStringLiteral("mutation_authority"), QStringLiteral("none")}};
}

bool registerVibeCutEditorialReviewTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject scoreProperties{
        {QStringLiteral("objective_relevance"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 5}}},
        {QStringLiteral("narrative_coherence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 5}}},
        {QStringLiteral("pacing_fit"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 5}}},
        {QStringLiteral("source_fidelity"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 5}}},
        {QStringLiteral("overall_preference"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 5}}},
    };
    const QJsonObject shaSchema{{QStringLiteral("type"), QStringLiteral("string")},
                                {QStringLiteral("minLength"), 64}, {QStringLiteral("maxLength"), 64},
                                {QStringLiteral("pattern"), QStringLiteral("^[0-9A-Fa-f]{64}$")}};
    const QJsonObject reviewSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject{
                                       {QStringLiteral("schema_version"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("const"), 1}}},
                                       {QStringLiteral("rubric_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("const"), kRubric}}},
                                       {QStringLiteral("blind"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("const"), true}}},
                                       {QStringLiteral("case_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 128}}},
                                       {QStringLiteral("candidate_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 128}}},
                                       {QStringLiteral("reviewer_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 128}}},
                                       {QStringLiteral("task_type"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("rough_cut"), QStringLiteral("highlight"), QStringLiteral("broll")}}}},
                                       {QStringLiteral("context_sha256"), shaSchema},
                                       {QStringLiteral("proposal_id"), shaSchema},
                                       {QStringLiteral("scores"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                              {QStringLiteral("properties"), scoreProperties},
                                                                              {QStringLiteral("required"), QJsonArray::fromStringList(kCriteria)},
                                                                              {QStringLiteral("additionalProperties"), false}}},
                                       {QStringLiteral("notes"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 2000}}}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("schema_version"), QStringLiteral("rubric_id"), QStringLiteral("blind"),
                                                                           QStringLiteral("case_id"), QStringLiteral("candidate_id"), QStringLiteral("reviewer_id"),
                                                                           QStringLiteral("task_type"), QStringLiteral("context_sha256"), QStringLiteral("proposal_id"),
                                                                           QStringLiteral("scores")}},
                                   {QStringLiteral("additionalProperties"), false}};

    VibeCutToolPolicy validatePolicy;
    validatePolicy.name = QStringLiteral("editorial_review_validate");
    validatePolicy.risk = VibeCutToolRisk::ReadOnly;
    if (!surface.registerTool(QJsonObject{{QStringLiteral("name"), validatePolicy.name},
                                          {QStringLiteral("description"), QStringLiteral("Validate one blinded VibeCutEditorialReview-v1 human review record bound to an exact context SHA-256 and proposal ID. Ratings are subjective review evidence, not ground truth and not execution authority.")},
                                          {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                                       {QStringLiteral("properties"), QJsonObject{{QStringLiteral("review"), reviewSchema}}},
                                                                                       {QStringLiteral("required"), QJsonArray{QStringLiteral("review")}},
                                                                                       {QStringLiteral("additionalProperties"), false}}}},
                              validatePolicy, validateTool, error)) return false;

    VibeCutToolPolicy aggregatePolicy;
    aggregatePolicy.name = QStringLiteral("editorial_review_aggregate");
    aggregatePolicy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), aggregatePolicy.name},
                                            {QStringLiteral("description"), QStringLiteral("Aggregate 1..50 blinded reviews for one exact proposal/context under VibeCutEditorialReview-v1, reporting means and disagreement without pass/fail or automatic execution authority.")},
                                            {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                                         {QStringLiteral("properties"), QJsonObject{{QStringLiteral("reviews"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 50}, {QStringLiteral("items"), reviewSchema}}}}},
                                                                                         {QStringLiteral("required"), QJsonArray{QStringLiteral("reviews")}},
                                                                                         {QStringLiteral("additionalProperties"), false}}}},
                                aggregatePolicy, aggregateTool, error);
}
