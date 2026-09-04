/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class VibeCutToolSurface;

/** Fixed subjective-review rubric for proposal evaluation. Review evidence is
 * human judgment, not ground truth, and never grants edit authority. */
QString vibeCutEditorialReviewRubricId();

/** Validate and normalize one blinded review record. */
QJsonObject validateVibeCutEditorialReview(const QJsonObject &review, QString *error = nullptr);

/** Aggregate 1..50 validated reviews for exactly one case/candidate/rubric.
 * Reports central tendency and disagreement without applying a pass/fail gate. */
QJsonObject aggregateVibeCutEditorialReviews(const QJsonArray &reviews, QString *error = nullptr);

bool registerVibeCutEditorialReviewTools(VibeCutToolSurface &surface, QString *error = nullptr);
