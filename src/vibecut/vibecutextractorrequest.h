/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonObject>
#include <QString>

/** Resolve a provider-neutral extractor request against authoritative Kdenlive
 * bin/source state. Requires request.bin_id and enriches it with source_id,
 * source_path, source_fingerprint, fps, duration_frames, has_audio/video and
 * validated start/end frame bounds. Provider-specific optional fields are
 * preserved under parameters.
 */
bool normalizeVibeCutExtractorRequest(const QString &capability,
                                      const QJsonObject &request,
                                      QJsonObject &normalized,
                                      QString *error = nullptr);
