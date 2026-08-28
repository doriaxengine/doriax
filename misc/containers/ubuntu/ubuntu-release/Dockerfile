FROM ubuntu:latest

WORKDIR /opt/workdir

RUN apt-get update -y
RUN apt-get install -y \
    python3 \
    clang \
    cmake \
    libwayland-dev \
    libcurl4-openssl-dev \
    libdbus-1-dev \
    doxygen \
    libx11-dev \
    libxcursor-dev \
    libxi-dev \
    libxinerama-dev \
    libxrandr-dev \
    libgl-dev \
    ninja-build \
    libvulkan-dev

# Build and install into a directory with CMake.
CMD cmake \
    -B /opt/builddir \
    -D CMAKE_BUILD_TYPE=Release \
    -G 'Ninja' && \
    cmake --build /opt/builddir --config Release -j$(nproc) && \
    DESTDIR="/opt/installdir" cmake --build /opt/builddir --target=install
