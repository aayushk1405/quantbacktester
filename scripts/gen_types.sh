#!/usr/bin/env bash
# scripts/gen_types.sh
# Auto-generates TypeScript types from the FastAPI OpenAPI spec.
#
# This keeps frontend/src/utils/types.ts in sync with the Pydantic
# models in api/models.py — no manual type duplication needed.
#
# Usage:
#   ./scripts/gen_types.sh              # API must be running on localhost:8000
#   API_URL=http://staging ./scripts/gen_types.sh
#
# Requirements:
#   npm install -g openapi-typescript   (run once)

set -euo pipefail

API_URL="${API_URL:-http://localhost:8000}"
OUT="frontend/src/utils/generated_types.ts"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo ""
echo "▶ Fetching OpenAPI spec from $API_URL/openapi.json ..."
curl -sf "$API_URL/openapi.json" -o /tmp/quant_openapi.json

echo "▶ Generating TypeScript types → $OUT ..."
npx openapi-typescript /tmp/quant_openapi.json \
    --output "$ROOT/$OUT" \
    --immutable-types \
    --export-type

echo "✓ Types written to $OUT"
echo ""
echo "  Import in your components:"
echo "    import type { components } from '../utils/generated_types';"
echo "    type BacktestResponse = components['schemas']['BacktestResponse'];"
echo ""
