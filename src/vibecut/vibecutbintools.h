/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include "doc/docundostack.hpp"

#include <QString>

class VibeCutToolSurface;
bool registerVibeCutBinTools(VibeCutToolSurface &surface, QString *error = nullptr);
