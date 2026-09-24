#!/bin/bash
set -eo pipefail
PROJECTDIR=$(cd "$(dirname "$0")/../.." && pwd)
exec "$PROJECTDIR/tools/scripts/copy_pc_files.sh" "$1" \
  "$PROJECTDIR/tools/config/resource_paths.csv" "$2" "$PROJECTDIR"
