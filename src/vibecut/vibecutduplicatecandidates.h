/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QFileInfo>

class QString;
class VibeCutToolSurface;

bool registerVibeCutDuplicateCandidateTools(VibeCutToolSurface &surface, QString *error = nullptr);
