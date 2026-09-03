/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QString>

/** Register VibeCut-owned built-in extractor providers exactly once. */
void ensureVibeCutBuiltinExtractorProvidersRegistered();

QString vibeCutPyannoteVenvDir();
QString vibeCutPyannotePython();
QString vibeCutPyannoteScript();
QString vibeCutPyannoteToken(QString *error = nullptr);
bool vibeCutStorePyannoteToken(const QString &token, QString *error = nullptr);
bool vibeCutPyannoteDependenciesReady(QString *error = nullptr);
