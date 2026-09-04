/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

/** Compare one candidate-ID selection/order against an explicit reference.
 * Metrics measure agreement only; they do not claim intrinsic editorial
 * quality and are intended for golden fixtures and blinded-review references. */
QJsonObject evaluateVibeCutEditorialSelection(const QJsonArray &expectedCandidateIds,
                                              const QJsonArray &actualCandidateIds,
                                              QString *error = nullptr);
