#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
awk '
BEGIN { printf "/* Generated from version.c; SPDX-License-Identifier: GPL-2.0-only */\npackage org.openbgi.port;\nfinal class EngineProfiles { static final String[] NAMES = {\"auto\""; count=0 }
/kDefaultProfiles\[GEN_COUNT[^]]*\] = \{/ { inside=1; next }
inside && /^};/ { exit }
inside && /\{"/ { name=$0; sub(/^[^"]*"/,"",name); sub(/".*/,"",name); printf ",\"%s\"",name; count++ }
END { print "}; }"; if(count==0)exit 1 }
' "$root/src/core/version.c" > "$1"
