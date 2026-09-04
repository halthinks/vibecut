/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonObject>
#include <QString>

class VibeCutToolSurface;

/** Filter/fuse one current hybrid-search result back into the exact rough-cut
 * candidate universe. Output is derived ranking evidence only. */
QJsonObject rankVibeCutRoughCutObjectiveHits(const QJsonObject &context,
                                             const QJsonObject &hybridResult,
                                             int limit = 50,
                                             double minScore = 0.0,
                                             QString *error = nullptr);

bool registerVibeCutRoughCutRelevanceTools(VibeCutToolSurface &surface, QString *error = nullptr);
