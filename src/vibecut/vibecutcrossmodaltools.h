/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <limits>

class QString;
class VibeCutToolSurface;

bool registerVibeCutCrossModalTools(VibeCutToolSurface &surface, QString *error = nullptr);
