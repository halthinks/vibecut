/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QString>

class VibeCutPlanRuntime;
class VibeCutToolSurface;

bool registerVibeCutPlanTools(VibeCutToolSurface &surface, VibeCutPlanRuntime *runtime, QString *error = nullptr);
