/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include "vibecutcontracts.h"

#include <QHash>
#include <QString>

class VibeCutPolicyOverrides
{
public:
    static QString fileName();
    static QHash<QString, VibeCutToolPolicy> applyCurrent(const QHash<QString, VibeCutToolPolicy> &basePolicies, QString *error = nullptr);
};
