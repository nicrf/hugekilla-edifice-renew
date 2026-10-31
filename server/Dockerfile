# edifice-lobby: game list, P2P signaling and STUN/TURN relay for Edifice.
FROM golang:1.23-alpine AS build
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY *.go ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /edifice-lobby .

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /edifice-lobby /edifice-lobby
# HTTP API, signaling (TCP), STUN/TURN (UDP)
EXPOSE 8080/tcp 10000/tcp 3478/udp
ENTRYPOINT ["/edifice-lobby"]
