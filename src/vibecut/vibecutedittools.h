/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include <QString>
class VibeCutToolSurface;

/** Register core undoable timeline-edit primitives backed by Kdenlive's own
 * TimelineModel request APIs. */
bool registerVibeCutEditTools(VibeCutToolSurface &surface, QString *error = nullptr);
