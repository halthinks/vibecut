/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QString>

QString vibeCutSemanticVenvDir();
QString vibeCutSemanticPython();
QString vibeCutSemanticScript();
QString vibeCutSemanticRequirements();
bool vibeCutSemanticDependenciesReady(QString *error = nullptr);
