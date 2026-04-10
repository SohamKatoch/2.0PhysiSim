# Build and run PhysiSim in Linux. The binary embeds PHYSISIM_SHADER_DIR=/app/build/shaders — keep that layout.
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    glslang-tools \
    libvulkan-dev \
    libwayland-dev \
    libx11-dev \
    libxcursor-dev \
    libxi-dev \
    libxinerama-dev \
    libxkbcommon-dev \
    libxrandr-dev \
    pkg-config \
    wayland-protocols \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

# --- runtime ---
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libvulkan1 \
    mesa-vulkan-drivers \
    libwayland-client0 \
    libx11-6 \
    libxcursor1 \
    libxi6 \
    libxinerama1 \
    libxkbcommon0 \
    libxrandr2 \
    x11-utils \
    xvfb \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/build/physisim /app/build/physisim
COPY --from=builder /app/build/shaders /app/build/shaders
COPY docker/entrypoint.sh /usr/local/bin/physisim-entrypoint.sh
RUN chmod +x /usr/local/bin/physisim-entrypoint.sh

# HTTP API (override with PHYSISIM_IPC_PORT / publish in compose)
ENV PHYSISIM_IPC_PORT=17500 \
    PHYSISIM_IPC_HOST=0.0.0.0

EXPOSE 17500

ENTRYPOINT ["/usr/local/bin/physisim-entrypoint.sh"]
CMD ["--ipc-port", "17500"]
