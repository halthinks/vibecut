/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonObject>

class QString;
class VibeCutToolSurface;

/** Deterministically fuse bounded duplicate/near-duplicate evidence components.
 * Component values are similarities/evidence scores, not probabilities.
 */
QJsonObject fuseVibeCutDuplicateSignals(const QJsonObject &components);

bool registerVibeCutDuplicateFusionTools(VibeCutToolSurface &surface, QString *error = nullptr);
