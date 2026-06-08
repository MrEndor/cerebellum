# syntax=docker/dockerfile:1
FROM node:26.3.0-alpine AS builder

WORKDIR /app

COPY ui/package.json ui/package-lock.json ./

RUN --mount=type=cache,target=/root/.npm \
    npm ci

COPY ui/ .

RUN npm run build

FROM nginx:1.30.2-alpine

RUN rm -f /etc/nginx/conf.d/default.conf
COPY nginx/templates/cerebellum.conf.template /etc/nginx/templates/cerebellum.conf.template
COPY nginx/tls/cerebellum-tls.conf.template /etc/nginx/tls.conf.template
COPY --chmod=0755 nginx/docker-entrypoint.d/10-tls.envsh /docker-entrypoint.d/10-tls.envsh
COPY --from=builder /app/dist /usr/share/nginx/html
