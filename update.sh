#!/bin/bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 URL" >&2
  exit 1
fi

readonly url=$1

rm -rf ./.gitignore ./APPLE_LICENSE ./compile_stubs ./doc ./ld64.xcodeproj ./src ./unit-tests
curl -L --fail "$url" -o /tmp/ld.tar.gz
tar xf /tmp/ld.tar.gz --strip-components=1 -C .
