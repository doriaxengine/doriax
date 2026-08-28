FROM fedora:latest

WORKDIR /opt/workdir

# Disable weak dependencies to reduce image size.
RUN echo "install_weak_deps=False" >> /etc/dnf/dnf.conf 
RUN dnf install -y \
    python3 \
    clang \
    cmake \
    wayland-devel \
    curl-devel \
    dbus-devel \
    doxygen \
    libX11-devel \
    libXcursor-devel \
    libXi-devel \
    libXinerama-devel \
    libXrandr-devel \
    mesa-libGL-devel \
    ninja-build \
    vulkan-devel \
    vulkan-headers

# Build and install into a directory with CMake.
CMD cmake \
    -B build/containers/fedora \
    -D CMAKE_BUILD_TYPE=Release \
    -G 'Ninja' && \
    cmake --build build/containers/fedora --config Release -j$(nproc) && \
    DESTDIR="/opt/installdir" cmake --build build/containers/fedora --target=install
