/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QString>

QString vibeCutVisionVenvDir();
QString vibeCutVisionPython();
QString vibeCutVisionRequirements();
bool vibeCutVisionDependenciesReady(QString *error = nullptr);
