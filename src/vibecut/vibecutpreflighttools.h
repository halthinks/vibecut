/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonObject>

class VibeCutToolSurface;
class QString;

QJsonObject vibeCutProjectPreflight();
bool registerVibeCutPreflightTools(VibeCutToolSurface &surface, QString *error = nullptr);
