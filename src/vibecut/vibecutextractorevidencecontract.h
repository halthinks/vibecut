/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include "vibecutmediaevidence.h"

#include <QString>

/** Validate capability-specific provider evidence before it reaches the
 * canonical media-evidence persistence sink. The generic evidence ledger
 * validates provenance/ranges/confidence; this layer prevents a provider from
 * smuggling capability-incompatible semantics into otherwise valid records.
 *
 * requestedStartFrame/requestedEndFrame are the authoritative normalized
 * source bounds dispatched to the provider. */
bool validateVibeCutExtractorEvidenceContract(const QString &capability,
                                              int requestedStartFrame,
                                              int requestedEndFrame,
                                              const QList<VibeCutMediaEvidenceRecord> &records,
                                              QString *error = nullptr);
