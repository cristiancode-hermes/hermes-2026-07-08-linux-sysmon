# Stage 1: Build con GCC 14
FROM gcc:14-bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copiar fuentes primero (cache layers)
COPY CMakeLists.txt CMakePresets.json vcpkg.json ./
COPY include/ ./include/
COPY src/     ./src/
COPY tests/   ./tests/

# Configurar y compilar
RUN cmake --preset debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    && cmake --build --preset debug

# Stage 2: Runtime mínimo (~20 MB)
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/debug/linux-sysmon /usr/local/bin/linux-sysmon

ENTRYPOINT ["linux-sysmon"]
CMD ["--help"]
