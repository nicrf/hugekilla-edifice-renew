# edifice-server: the Edifice dedicated server (src/platform/dedicated.h), with no
# window, renderer nor audio: GameNetworkingSockets (P2P Internet games through the
# lobby, its TURN relay) and libdatachannel (browser players over WebRTC) from vcpkg,
# at the commit of the releases. The build context is the repository root:
#   docker build -f server/edifice-server.Dockerfile -t edifice-server .
# Each platform builds natively (the image workflow runs amd64 and arm64 on their own
# runners): vcpkg's libraries don't cross-compile here, and emulated they take hours.
FROM debian:bookworm AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
		build-essential cmake ninja-build git curl ca-certificates zip unzip tar pkg-config \
		python3 linux-libc-dev libcurl4-openssl-dev \
	&& rm -rf /var/lib/apt/lists/*
# vcpkg at VCPKG_COMMIT of .github/workflows/release.yml (the image workflow passes it)
ARG VCPKG_COMMIT=9a5dfef5d1a0438a7d24856a85bd60f309d355e9
ENV VCPKG_ROOT=/opt/vcpkg VCPKG_DISABLE_METRICS=1 VCPKG_FORCE_SYSTEM_BINARIES=1
RUN git init -q $VCPKG_ROOT && cd $VCPKG_ROOT \
	&& git fetch -q --depth 1 https://github.com/microsoft/vcpkg $VCPKG_COMMIT \
	&& git checkout -q FETCH_HEAD && ./bootstrap-vcpkg.sh -disableMetrics
# the machine's triplet, static libraries in release only (half the build time)
RUN arch=$(uname -m | sed 's/x86_64/x64/;s/aarch64/arm64/') && mkdir -p /opt/triplets \
	&& printf 'set(VCPKG_TARGET_ARCHITECTURE %s)\nset(VCPKG_CRT_LINKAGE dynamic)\nset(VCPKG_LIBRARY_LINKAGE static)\nset(VCPKG_CMAKE_SYSTEM_NAME Linux)\nset(VCPKG_BUILD_TYPE release)\n' \
		"$arch" > /opt/triplets/edifice-linux.cmake
WORKDIR /src
# the libraries alone first: their layer stays cached while vcpkg.json doesn't change
COPY vcpkg.json ./
RUN $VCPKG_ROOT/vcpkg install --x-install-root=/opt/vcpkg_installed --triplet=edifice-linux \
		--overlay-triplets=/opt/triplets --x-feature=gns --x-feature=webrtc --clean-after-build
COPY CMakeLists.txt ./
COPY cmake cmake
COPY src src
COPY tests tests
COPY data data
COPY licenses licenses
COPY lgpl-3.0.txt license.txt ./
# the server alone (EDIFICE_CLIENT=OFF: no SDL, no renderer)
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DEDIFICE_CLIENT=OFF -DEDIFICE_DEDICATED=ON \
		-DEDIFICE_GNS=ON -DEDIFICE_WEBRTC=ON \
		-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
		-DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed \
		-DVCPKG_TARGET_TRIPLET=edifice-linux -DVCPKG_OVERLAY_TRIPLETS=/opt/triplets \
	&& cmake --build build --target edifice-server \
	&& strip build/edifice-server
# what the server reads of data/: the settings files, the structures and models, and
# the ground clouds (the land's scenery is placed after them); not the textures,
# sounds nor the Ultra graphics (a few MB instead of 38). The licenses: the game's
# (LGPL v3), its third-party code's, and those of the vcpkg libraries linked in.
RUN mkdir -p /out/data/textures/clouds /out/licenses/vcpkg \
	&& cp build/edifice-server /out/ \
	&& cp data/*.ini /out/data/ \
	&& cp -R data/Structures data/objects /out/data/ \
	&& cp data/textures/clouds/landcloud.bmp /out/data/textures/clouds/ \
	&& cp -R licenses/. lgpl-3.0.txt license.txt /out/licenses/ \
	&& for d in /opt/vcpkg_installed/*/share/*/; do \
		if [ -f "$d/copyright" ]; then cp "$d/copyright" "/out/licenses/vcpkg/$(basename "$d").txt"; fi; \
	done

FROM debian:bookworm-slim
# libcurl (the lobby, HTTPS), the CA certificates it checks the lobby with
RUN apt-get update && apt-get install -y --no-install-recommends libcurl4 ca-certificates \
	&& rm -rf /var/lib/apt/lists/* \
	&& useradd --system --uid 10001 --user-group --home-dir /var/lib/edifice --create-home edifice
COPY --from=build /out/ /opt/edifice/
# net.ini, sv.ini and the saved maps (maps/): a volume to keep them
ENV HK_USERDIR=/var/lib/edifice/
VOLUME /var/lib/edifice
USER edifice
WORKDIR /var/lib/edifice
# the server's UDP port (HK_PORT, 27020) and the players' ports after it (plain UDP:
# one each, from HK_PORT+2); GameNetworkingSockets and WebRTC pick their own
EXPOSE 27020-27053/udp
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
	CMD ["/opt/edifice/edifice-server", "--healthcheck"]
ENTRYPOINT ["/opt/edifice/edifice-server"]
