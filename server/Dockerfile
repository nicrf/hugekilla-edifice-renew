# edifice-lobby: game list, P2P signaling and STUN/TURN relay for Edifice.
# The build stage runs on the builder's own architecture and cross-compiles
# (Go needs no emulation); only the final stage is per target platform.
FROM --platform=$BUILDPLATFORM golang:1.23-alpine AS build
ARG TARGETOS TARGETARCH
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY *.go ./
# version and commit, given by the image workflow (docker build --build-arg ...)
ARG VERSION=dev COMMIT=
RUN CGO_ENABLED=0 GOOS=$TARGETOS GOARCH=$TARGETARCH go build -trimpath \
	-ldflags="-s -w -X main.version=$VERSION -X main.commit=$COMMIT" -o /edifice-lobby .

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /edifice-lobby /edifice-lobby
# HTTP API, signaling (TCP), STUN/TURN (UDP)
EXPOSE 8080/tcp 10000/tcp 3478/udp
ENTRYPOINT ["/edifice-lobby"]
