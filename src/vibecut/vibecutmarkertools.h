/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once
#include <QString>
class VibeCutToolSurface;
bool registerVibeCutMarkerTools(VibeCutToolSurface &surface, QString *error = nullptr);
