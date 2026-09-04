/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonObject>
#include <QString>

class VibeCutToolSurface;

/** Validate/normalize one frozen blinded editorial evaluation case manifest.
 * A case binds opaque candidate labels to exact proposal hashes and may carry
 * an explicitly sourced structural reference. It grants no execution authority. */
QJsonObject validateVibeCutEditorialCase(const QJsonObject &manifest, QString *error = nullptr);

bool registerVibeCutEditorialCaseTools(VibeCutToolSurface &surface, QString *error = nullptr);
