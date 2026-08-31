/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecutprojectrules.h"

#include <QFile>
#include <QTemporaryDir>

TEST_CASE("project rules load beside the kdenlive project", "[vibecut][rules]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString projectPath = dir.filePath(QStringLiteral("interview.kdenlive"));
    QFile project(projectPath);
    REQUIRE(project.open(QIODevice::WriteOnly));
    project.close();

    QFile rules(dir.filePath(VibeCutProjectRules::fileName()));
    REQUIRE(rules.open(QIODevice::WriteOnly));
    rules.write("always prefer DeepFilterNet\nnever touch track 3\n");
    rules.close();

    QString error;
    const QString loaded = VibeCutProjectRules::loadForProjectUrl(QUrl::fromLocalFile(projectPath), &error);
    CHECK(error.isEmpty());
    CHECK(loaded.contains(QStringLiteral("DeepFilterNet")));
    CHECK(loaded.contains(QStringLiteral("track 3")));
}

TEST_CASE("project rules are appended beneath non-overridable base rules", "[vibecut][rules]")
{
    const QString prompt = VibeCutProjectRules::appendToSystemPrompt(QStringLiteral("BASE"), QStringLiteral("prefer short cuts"));
    CHECK(prompt.startsWith(QStringLiteral("BASE")));
    CHECK(prompt.contains(QStringLiteral("never override"), Qt::CaseInsensitive));
    CHECK(prompt.contains(QStringLiteral("<project_rules>")));
    CHECK(prompt.contains(QStringLiteral("prefer short cuts")));
}

TEST_CASE("oversized project rules fail closed", "[vibecut][rules]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("large.kdenlive"));
    QFile project(projectPath);
    REQUIRE(project.open(QIODevice::WriteOnly));
    project.close();

    QFile rules(dir.filePath(VibeCutProjectRules::fileName()));
    REQUIRE(rules.open(QIODevice::WriteOnly));
    rules.write(QByteArray(VibeCutProjectRules::MaxRulesBytes + 1, 'x'));
    rules.close();

    QString error;
    CHECK(VibeCutProjectRules::loadForProjectUrl(QUrl::fromLocalFile(projectPath), &error).isEmpty());
    CHECK_FALSE(error.isEmpty());
}
