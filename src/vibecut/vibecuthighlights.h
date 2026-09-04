/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

class VibeCutToolSurface;

/** Build one proposal-only highlight/short candidate sequence from an exact
 * rough-cut context and completed objective ranking. Selection is deterministic
 * and budgeted; ranges/provenance come only from the canonical context. */
QJsonObject buildVibeCutHighlightProposal(const QJsonObject &context,
                                          const QJsonObject &objectiveRanking,
                                          const QString &format,
                                          int maxSegments,
                                          qint64 maxTotalFrames,
                                          double minRelevance,
                                          bool preserveSourceOrder,
                                          quint64 currentRevision,
                                          QString *error = nullptr);

bool registerVibeCutHighlightTools(VibeCutToolSurface &surface, QString *error = nullptr);
