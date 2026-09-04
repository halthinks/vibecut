/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class VibeCutToolSurface;

/** Canonical order-independent identity for one pair of distinct asset IDs. */
QString vibeCutDuplicatePairId(const QString &firstBinId, const QString &secondBinId, QString *error = nullptr);

/** Evaluate ranked duplicate/near-duplicate pair candidates against an explicit
 * reference set. Pair order is canonicalized; metrics measure agreement with
 * the supplied reference and never claim duplicate truth. */
QJsonObject evaluateVibeCutDuplicateRanking(const QJsonArray &relevantPairs,
                                            const QJsonArray &rankedPairs,
                                            int k,
                                            QString *error = nullptr);

bool registerVibeCutDuplicateEvalTools(VibeCutToolSurface &surface, QString *error = nullptr);
