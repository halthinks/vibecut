/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutprojectmemory.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

TEST_CASE("project memory loads bounded versioned sidecar beside project", "[vibecut][memory]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("edit.kdenlive"));
    QFile project(projectPath);
    REQUIRE(project.open(QIODevice::WriteOnly));
    project.close();

    QFile memory(dir.filePath(VibeCutProjectMemory::fileName()));
    REQUIRE(memory.open(QIODevice::WriteOnly));
    memory.write("{\"version\":1,\"entries\":[{\"id\":\"one\",\"text\":\"Keep intro short\",\"source\":\"user\"}]}");
    memory.close();

    QString error;
    const QJsonArray entries = VibeCutProjectMemory::loadForProjectUrl(QUrl::fromLocalFile(projectPath), &error);
    CHECK(error.isEmpty());
    REQUIRE(entries.size() == 1);
    CHECK(entries.first().toObject().value(QStringLiteral("text")).toString() == QStringLiteral("Keep intro short"));
}

TEST_CASE("project memory rejects malformed and oversized sidecars", "[vibecut][memory]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("edit.kdenlive"));
    QFile project(projectPath);
    REQUIRE(project.open(QIODevice::WriteOnly));
    project.close();

    const QString memoryPath = dir.filePath(VibeCutProjectMemory::fileName());
    QFile memory(memoryPath);
    REQUIRE(memory.open(QIODevice::WriteOnly));
    memory.write("not json");
    memory.close();

    QString error;
    CHECK(VibeCutProjectMemory::loadForProjectUrl(QUrl::fromLocalFile(projectPath), &error).isEmpty());
    CHECK_FALSE(error.isEmpty());

    REQUIRE(memory.open(QIODevice::WriteOnly | QIODevice::Truncate));
    memory.write(QByteArray(VibeCutProjectMemory::MaxBytes + 1, 'x'));
    memory.close();
    error.clear();
    CHECK(VibeCutProjectMemory::loadForProjectUrl(QUrl::fromLocalFile(projectPath), &error).isEmpty());
    CHECK(error.contains(QStringLiteral("exceeds")));
}
