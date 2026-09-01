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
    libX11-devel \
    libXcursor-devel \
    libXi-devel \
    libXinerama-devel \
    libXrandr-devel \
    mesa-libGL-devel \
    ninja-build \
    vulkan-devel

# Build and install into a directory with CMake.
CMD cmake \
    -B /opt/builddir \
    -D CMAKE_BUILD_TYPE=Release \
    -G 'Ninja' && \
    cmake --build /opt/builddir --config Release -j$(nproc) && \
    DESTDIR="/opt/installdir" cmake --build /opt/builddir --target=install
