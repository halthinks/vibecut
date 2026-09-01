/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

class VibeCutToolSurface;
class QString;

bool registerVibeCutDeadLinkedTools(VibeCutToolSurface &surface, QString *error = nullptr);
