# syntax=docker/dockerfile:1

# canonical: stack_web_image -- the built browser client and the static server that serves it.
#
# The second image of the runner-lane stack (deploy/stack/stack.toml extra_images), built by the
# host from this repository's root as the build context. The simulation server serves only /api,
# so this container serves everything else; deploy/stack/serve.json routes `/` here and `/api` to
# the server, both on loopback inside the sidecar's network namespace.
# Build:  docker build -f deploy/web/Dockerfile -t blob-royale-web .

FROM node:22.23.4-trixie-slim@sha256:817bd9409161864cc85fcda5febd87a26f2d47c9fe5af355d327aeddfa11602c AS build
WORKDIR /web
COPY frontend-react/package.json frontend-react/package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY frontend-react ./
RUN npm run build

FROM nginxinc/nginx-unprivileged:1.29-alpine@sha256:0c79d56aee561a1d81c63f00eee5fb5fe29279560cdc55e91425133104c7fbe6
LABEL org.opencontainers.image.title="Blob Royale web client" \
      org.opencontainers.image.description="The built browser client behind a read-only static server" \
      org.opencontainers.image.source="https://github.com/scshafe/blob-royale"
COPY --chown=0:0 deploy/web/nginx.conf /etc/nginx/nginx.conf
COPY --from=build --chown=0:0 /web/dist /usr/share/nginx/html
USER 65532:65532
EXPOSE 8081/tcp
