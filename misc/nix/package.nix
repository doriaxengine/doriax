{
  llvmPackages,
  cmake,
  ninja,
  pkg-config,
  curl,
  dbus,
  libGL,
  libx11,
  libxcursor,
  libxi,
  libxinerama,
  vulkan-headers,
  vulkan-loader,
  python3,
  libxrandr,
  wayland,
  wayland-scanner,
  libffi,
  systemdLibs,
}:

llvmPackages.stdenv.mkDerivation (finalAttrs: {
  pname = "doriax";
  version = "v0.7.0";

  src = ../../.;

  nativeBuildInputs = [
    llvmPackages.clang
    cmake
    ninja
    pkg-config
  ];

  buildInputs = [
    curl
    dbus
    libGL
    libx11
    libxcursor
    libxi
    libxinerama
    vulkan-headers
    vulkan-loader
    python3
    libxrandr
    wayland
    wayland-scanner
    libffi
    systemdLibs
  ];
})
