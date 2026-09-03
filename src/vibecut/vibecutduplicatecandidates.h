/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

class QString;
class VibeCutToolSurface;

bool registerVibeCutDuplicateCandidateTools(VibeCutToolSurface &surface, QString *error = nullptr);
