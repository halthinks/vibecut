/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

class VibeCutToolSurface;

/** Compare 2..5 candidate-ID rough-cut alternatives against one exact context
 * and objective-relevance result. The score is a fixed transparent editorial
 * comparison heuristic, never a quality probability and never edit authority. */
QJsonObject compareVibeCutRoughCutAlternatives(const QJsonObject &context,
                                               const QJsonObject &objectiveRanking,
                                               const QJsonArray &alternatives,
                                               quint64 currentRevision,
                                               QString *error = nullptr);

bool registerVibeCutRoughCutAlternativeTools(VibeCutToolSurface &surface, QString *error = nullptr);
