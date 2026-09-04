/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

class VibeCutToolSurface;

/** Validate one B-roll opportunity against an exact current rough-cut context.
 * The proposer chooses only a canonical A-roll candidate id, bounded visual
 * search query and editorial purpose. Target geometry/provenance is resolved
 * from context and the result grants no edit authority. */
QJsonObject validateVibeCutBrollOpportunity(const QJsonObject &context,
                                            const QString &anchorCandidateId,
                                            const QString &query,
                                            const QString &purpose,
                                            quint64 currentRevision,
                                            QString *error = nullptr);

/** Build one reviewable B-roll placement proposal from a validated opportunity
 * and an exact completed visual-search result. The selected visual anchor must
 * come from that result. No source in/out range is invented: the proposal
 * exposes only the authoritative sampled source frame as a center/reference. */
QJsonObject buildVibeCutBrollPlacementProposal(const QJsonObject &opportunity,
                                               const QJsonObject &searchResult,
                                               const QString &selectedVisualAnchorId,
                                               quint64 currentRevision,
                                               QString *error = nullptr);

bool registerVibeCutBrollTools(VibeCutToolSurface &surface, QString *error = nullptr);
