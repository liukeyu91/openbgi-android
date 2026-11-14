#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
"${JAVAC:-javac}" -encoding UTF-8 -d "$out" "$root/tools/android/SaveBackup.java" "$root/tools/android/SaveBackupTest.java" "$root/tools/android/SaveRestore.java" "$root/tools/android/SaveRestoreTest.java" "$root/tools/android/GameDataLock.java"
"${JAVA:-java}" -cp "$out" org.openbgi.port.SaveBackupTest
"${JAVA:-java}" -cp "$out" org.openbgi.port.SaveRestoreTest
