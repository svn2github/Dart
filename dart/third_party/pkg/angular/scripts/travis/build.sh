#!/bin/bash

set -e
. ./scripts/env.sh

./scripts/generate-expressions.sh
./scripts/analyze.sh

./node_modules/jasmine-node/bin/jasmine-node ./scripts/changelog/

./node_modules/jasmine-node/bin/jasmine-node playback_middleware/spec/ &&
  node "node_modules/karma/bin/karma" start karma.conf \
    --reporters=junit,dots --port=8765 --runner-port=8766 \
    --browsers=Dartium,ChromeNoSandbox --single-run --no-colors --no-color

./scripts/generate-documentation.sh

