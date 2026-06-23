#!/usr/bin/env bash
# scripts/deploy.sh — Build, push, and deploy to Kubernetes.
#
# Usage:
#   ./scripts/deploy.sh staging           # deploy to staging overlay
#   ./scripts/deploy.sh prod              # deploy to production overlay
#   ./scripts/deploy.sh prod --dry-run    # preview what would change
#
# Prerequisites:
#   - kubectl configured and pointed at the right cluster
#   - Docker registry credentials: docker login <registry>
#   - kustomize (ships with kubectl >= 1.14 as `kubectl kustomize`)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

ENV="${1:-staging}"
DRY_RUN=false
[[ "${2:-}" == "--dry-run" ]] && DRY_RUN=true

REGISTRY="${DOCKER_REGISTRY:-your-registry}"
TAG="${IMAGE_TAG:-$(git rev-parse --short HEAD 2>/dev/null || echo latest)}"

echo ""
echo "══════════════════════════════════════════"
echo "  QuantEngine Deploy → $ENV  (tag: $TAG)"
echo "══════════════════════════════════════════"

# ── Validate environment ──────────────────────────────────────
if [[ "$ENV" != "staging" && "$ENV" != "prod" ]]; then
  echo "Error: environment must be 'staging' or 'prod'"
  exit 1
fi

OVERLAY_DIR="$ROOT/k8s/overlays/$ENV"
if [[ ! -d "$OVERLAY_DIR" ]]; then
  echo "Creating overlay directory: $OVERLAY_DIR"
  mkdir -p "$OVERLAY_DIR"
fi

# ── Build & push Docker images ────────────────────────────────
echo ""
echo "▶ Building Docker images (tag: $TAG)..."
cd "$ROOT"
docker build -f Dockerfile.api      -t "$REGISTRY/quant-api:$TAG"      . --quiet
docker build -f Dockerfile.frontend -t "$REGISTRY/quant-frontend:$TAG"  . --quiet
echo "  ✓ Images built"

echo "▶ Pushing to registry..."
docker push "$REGISTRY/quant-api:$TAG"      --quiet
docker push "$REGISTRY/quant-frontend:$TAG"  --quiet
echo "  ✓ Pushed $REGISTRY/quant-api:$TAG"
echo "  ✓ Pushed $REGISTRY/quant-frontend:$TAG"

# ── Generate kustomization overlay for this env ───────────────
cat > "$OVERLAY_DIR/kustomization.yaml" <<EOF
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization

namespace: quant

resources:
  - ../../base

images:
  - name: your-registry/quant-api
    newName: $REGISTRY/quant-api
    newTag: "$TAG"
  - name: your-registry/quant-frontend
    newName: $REGISTRY/quant-frontend
    newTag: "$TAG"

patches:
  - patch: |-
      - op: replace
        path: /spec/replicas
        value: $([ "$ENV" = "prod" ] && echo 4 || echo 2)
    target:
      kind: Deployment
      name: api
EOF

echo "  ✓ Overlay written to $OVERLAY_DIR/kustomization.yaml"

# ── Apply to cluster ──────────────────────────────────────────
echo ""
if [ "$DRY_RUN" = true ]; then
  echo "▶ Dry-run: kubectl apply -k $OVERLAY_DIR"
  kubectl apply -k "$OVERLAY_DIR" --dry-run=client
  echo ""
  echo "  (No changes applied — remove --dry-run to deploy)"
else
  echo "▶ Applying manifests to cluster..."
  kubectl apply -k "$OVERLAY_DIR"

  echo ""
  echo "▶ Waiting for API rollout..."
  kubectl rollout status deployment/api -n quant --timeout=300s

  echo ""
  echo "▶ Waiting for frontend rollout..."
  kubectl rollout status deployment/frontend -n quant --timeout=120s

  echo ""
  echo "══════════════════════════════════════════"
  echo "  Deploy complete ✓"
  echo "══════════════════════════════════════════"
  echo ""
  echo "  Pods:"
  kubectl get pods -n quant
  echo ""
  echo "  Ingress:"
  kubectl get ingress -n quant
fi
