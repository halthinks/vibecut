/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class VibeCutToolSurface;

/** Evaluate one ranked ID list against an explicit relevant-ID reference.
 * Metrics describe ranking/reference agreement, not intrinsic semantic truth. */
QJsonObject evaluateVibeCutRetrievalRanking(const QJsonArray &relevantIds,
                                            const QJsonArray &rankedIds,
                                            int k,
                                            QString *error = nullptr);

bool registerVibeCutRetrievalEvalTools(VibeCutToolSurface &surface, QString *error = nullptr);
