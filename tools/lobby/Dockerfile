# takedawn-lobby in a container: the lobby server for Takedawn's online play.
#
#   docker build -t takedawn-lobby tools/lobby
#   docker run -d --name takedawn-lobby --restart unless-stopped \
#       -p 21810:21810/tcp takedawn-lobby --public <your public address>
#
# With usage totals (counts only, in /stats; see docs/online.md):
#   docker run -d ... -v takedawn-lobby-stats:/stats takedawn-lobby \
#       --public <address> --stats-dir /stats
#
# Only TCP 21810 has to be reachable from the internet. Races run between
# the players' own copies of the game (peer to peer), not through here.
# musl, so the static program resolves a --public host name itself (a static
# glibc program needs the host's own glibc libraries for that).
FROM alpine:3.20 AS build
RUN apk add --no-cache gcc musl-dev
WORKDIR /src
COPY main.c aries_server.c aries_server.h stats_page.h admin_page.h ./
RUN gcc -O2 -Wall -static -o takedawn-lobby main.c aries_server.c -lpthread \
    && mkdir /stats

FROM scratch
COPY --from=build /src/takedawn-lobby /takedawn-lobby
COPY --from=build --chown=65534:65534 /stats /stats
EXPOSE 21810/tcp
USER 65534:65534
ENTRYPOINT ["/takedawn-lobby"]
