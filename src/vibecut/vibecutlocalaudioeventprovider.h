/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QString>

void ensureVibeCutLocalAudioEventProviderRegistered();

QString vibeCutAudioEventVenvDir();
QString vibeCutAudioEventPython();
QString vibeCutAudioEventScript();
bool vibeCutAudioEventDependenciesReady(QString *error = nullptr);
