#!/bin/bash
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
python3 -m venv .venv
.venv/bin/python -m pip install -r companion/requirements.txt
mkdir -p build/mac
xcrun swiftc -O companion/chatterfly_key.swift -o build/mac/chatterfly-key
codesign --force --sign - --identifier cn.folotoy.passport-mic.key build/mac/chatterfly-key
