#!/usr/bin/env bash
# scripts/build.sh — Build everything: C++ engine, Python wheel, Docker images.
#
# Usage:
#   ./scripts/build.sh           # build C++ + install .so into api/
#   ./scripts/build.sh --docker  # also build Docker images
#   ./scripts/build.sh --test    # run C++ unit tests after build
#
# Requirements:
#   - CMake >= 3.18
#   - C++17-capable compiler (GCC 10+ or Clang 13+)
#   - Python 3.9+ with dev headers
#   - Docker (only for --docker flag)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/cpp-engine/build"
DOCKER=false
RUN_TESTS=false

# ── Parse flags ───────────────────────────────────────────────
for arg in "$@"; do
  case $arg in
    --docker) DOCKER=true ;;
    --test)   RUN_TESTS=true ;;
    *)        echo "Unknown flag: $arg"; exit 1 ;;
  esac
done

echo ""
echo "══════════════════════════════════════════"
echo "  QuantEngine Build Script"
echo "══════════════════════════════════════════"

# ── Step 1: Configure CMake ───────────────────────────────────
echo ""
echo "▶ Step 1/4  Configuring CMake (Release build)..."
cmake -B "$BUILD_DIR" \
      -S "$ROOT/cpp-engine" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$ROOT/api" \
      -Wno-dev

# ── Step 2: Compile ───────────────────────────────────────────
echo ""
echo "▶ Step 2/4  Compiling C++ engine ($(nproc) parallel jobs)..."
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

# ── Step 3: Install .so into api/ ─────────────────────────────
echo ""
echo "▶ Step 3/4  Installing quant_engine.so → api/"
cmake --install "$BUILD_DIR"

# Verify the .so is loadable
python3 -c "import sys; sys.path.insert(0, '$ROOT/api'); import quant_engine; print('  ✓ quant_engine imported successfully')"

# ── Step 4: Unit tests (optional) ────────────────────────────
if [ "$RUN_TESTS" = true ]; then
  echo ""
  echo "▶ Step 4/4  Running C++ unit tests..."
  cd "$BUILD_DIR"
  ctest --output-on-failure --parallel "$(nproc)"
  echo "  ✓ All tests passed"
else
  echo ""
  echo "▶ Step 4/4  Skipping tests (pass --test to run them)"
fi

# ── Docker images (optional) ──────────────────────────────────
if [ "$DOCKER" = true ]; then
  echo ""
  echo "▶ Building Docker images..."
  cd "$ROOT"
  REGISTRY="${DOCKER_REGISTRY:-your-registry}"
  TAG="${IMAGE_TAG:-latest}"

  docker build -f Dockerfile.api      -t "$REGISTRY/quant-api:$TAG"      .
  docker build -f Dockerfile.frontend -t "$REGISTRY/quant-frontend:$TAG"  .
  echo "  ✓ Images built: quant-api:$TAG, quant-frontend:$TAG"

  if [ "${DOCKER_PUSH:-false}" = "true" ]; then
    echo "▶ Pushing images..."
    docker push "$REGISTRY/quant-api:$TAG"
    docker push "$REGISTRY/quant-frontend:$TAG"
    echo "  ✓ Images pushed"
  fi
fi

echo ""
echo "══════════════════════════════════════════"
echo "  Build complete ✓"
echo "══════════════════════════════════════════"
echo ""
echo "  Next steps:"
echo "    1. Download data:   python scripts/download_data.py"
echo "    2. Start locally:   docker compose up"
echo "    3. Open UI:         http://localhost:5173"
echo "    4. API docs:        http://localhost:8000/docs"
echo "    5. Deploy to k8s:   ./scripts/deploy.sh"
echo ""
