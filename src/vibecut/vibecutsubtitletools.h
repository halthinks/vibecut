/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QString>

class VibeCutToolSurface;

/** Register read-only subtitle intelligence tools on @p surface. */
bool registerVibeCutSubtitleTools(VibeCutToolSurface &surface, QString *error = nullptr);
