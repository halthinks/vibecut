/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include "vibecutmediaindex.h"

#include <QJsonObject>
#include <QList>
#include <QString>

class VibeCutToolSurface;

/** Build a bounded revision-bound candidate context from canonical transcript
 * documents. The result is proposal context only; it contains no edit
 * authority and never invents source paths or frame ranges. */
QJsonObject buildVibeCutRoughCutContext(const QList<VibeCutMediaDocument> &documents,
                                        quint64 baseRevision,
                                        int maxCandidates = 200,
                                        int maxTextChars = 600,
                                        QString *error = nullptr);

/** Validate an ordered rough-cut proposal against one exact context and the
 * current project revision. Selected items are candidate IDs only; ranges and
 * provenance are resolved from context, never accepted from the proposer. */
QJsonObject validateVibeCutRoughCutProposal(const QJsonObject &context,
                                            const QJsonObject &proposal,
                                            quint64 currentRevision,
                                            QString *error = nullptr);

bool registerVibeCutRoughCutSynthesisTools(VibeCutToolSurface &surface, QString *error = nullptr);
