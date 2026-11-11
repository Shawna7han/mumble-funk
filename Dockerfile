# syntax=docker/dockerfile:1
FROM ubuntu:24.04 AS build
WORKDIR /src
# Dependencies are cached independently of source changes and the build job count.
RUN set -eu; export DEBIAN_FRONTEND=noninteractive; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build pkg-config git ca-certificates python3 \
      qtbase5-dev qtbase5-dev-tools libqt5sql5-sqlite libboost-dev libssl-dev \
      libopus-dev libprotobuf-dev protobuf-compiler libprotoc-dev libcap-dev \
      libavahi-compat-libdnssd-dev libzeroc-ice-dev; \
    rm -rf /var/lib/apt/lists/*
ARG FUNK_BUILD_JOBS=2
COPY . .
# The optional CA and temporary combined bundle never enter the runtime image.
RUN --mount=type=secret,id=proxy_ca,required=false \
    set -eu; \
    case "$FUNK_BUILD_JOBS" in ''|*[!0-9]*) echo 'FUNK_BUILD_JOBS must be 1..16' >&2; exit 1;; esac; \
    test "$FUNK_BUILD_JOBS" -ge 1; test "$FUNK_BUILD_JOBS" -le 16; \
    if test -f /run/secrets/proxy_ca; then \
      cat /etc/ssl/certs/ca-certificates.crt /run/secrets/proxy_ca > /tmp/build-ca.pem; \
      export GIT_SSL_CAINFO=/tmp/build-ca.pem; \
    fi; \
    python3 scripts/docker/fetch-submodules.py; \
    cmake -S . -B /build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_NUMBER=939 \
      -Dclient=OFF -Dserver=ON -Dtests=ON -Dplugins=OFF -Doverlay=OFF \
      -Dlto=OFF -DCMAKE_UNITY_BUILD=OFF \
      -DCMAKE_PROJECT_mumble_INCLUDE=/src/scripts/docker/tests/register.cmake; \
    cmake --build /build --parallel "$FUNK_BUILD_JOBS"; \
    ctest --test-dir /build --output-on-failure; \
    python3 -m unittest discover -s scripts/docker/tests -p 'test_*.py'; \
    mkdir -p /out; cp /build/mumble-server /out/; \
    strip /out/mumble-server; \
    rm -rf /var/lib/apt/lists/* /tmp/build-ca.pem

FROM ubuntu:24.04 AS runtime
RUN set -eu; export DEBIAN_FRONTEND=noninteractive; \
    apt-get update; \
    apt-get install -y --no-install-recommends python3 ca-certificates \
      libqt5core5t64 libqt5network5t64 libqt5sql5t64 libqt5dbus5t64 libqt5xml5t64 \
      libqt5sql5-sqlite libopus0 libssl3t64 libprotobuf32t64 libcap2 \
      libavahi-compat-libdnssd1 libzeroc-ice3.7t64; \
    groupadd --gid 10001 mumble-funk; \
    useradd --uid 10001 --gid 10001 --no-create-home --home-dir /var/lib/mumble-funk \
      --shell /usr/sbin/nologin mumble-funk; \
    install -d -m 0750 -o 10001 -g 10001 /etc/mumble-funk /var/lib/mumble-funk \
      /var/lib/mumble-funk/audio /var/log/mumble-funk; \
    rm -rf /var/lib/apt/lists/*
COPY --from=build /out/mumble-server /usr/local/bin/mumble-server
COPY scripts/docker/runtime.py /usr/local/bin/funk-container
COPY --chmod=0644 scripts/funk/audio.py scripts/funk/audio-preview/audio_control.py /usr/local/lib/mumble-funk/
COPY --chmod=0644 assets/audio/busy-tone.wav assets/audio/squelch-open.wav assets/audio/squelch-close.wav assets/audio/radio-noise.wav assets/audio/LICENSE.md /usr/local/share/mumble-funk/audio/
COPY --chmod=0644 LICENSE /usr/share/doc/mumble-funk/LICENSE
COPY 3rdPartyLicenses /usr/share/doc/mumble-funk/third-party
COPY --from=build /src/3rdparty/speexdsp/COPYING /usr/share/doc/mumble-funk/SpeexDSP-COPYING
RUN chmod 0755 /usr/local/bin/funk-container /usr/local/lib/mumble-funk \
      /usr/local/share/mumble-funk /usr/local/share/mumble-funk/audio; \
    chmod -R a+rX /usr/share/doc/mumble-funk; \
    ! ldd /usr/local/bin/mumble-server | grep 'not found'
ENV PYTHONDONTWRITEBYTECODE=1
USER 10001:10001
WORKDIR /var/lib/mumble-funk
EXPOSE 64739/tcp 64739/udp
STOPSIGNAL SIGTERM
HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
    CMD ["python3", "/usr/local/bin/funk-container", "health"]
ENTRYPOINT ["python3", "/usr/local/bin/funk-container"]
CMD ["serve"]
