#!/usr/bin/env python3

# (c) Eduardo Doria and contributors
# SPDX-License-Identifier: MIT
# Based on Filip Stoklas code (http://forums.4fips.com/viewtopic.php?f=3&t=1201)

import argparse
import glob
import os
import shutil
import subprocess
import sys
import uuid

XCODE_PLATFORMS = ["macos-xcode", "ios-xcode"]

def fail(message):
    print("Error: " + message, file=sys.stderr)
    sys.exit(1)

def run(command):
    print("> " + " ".join(command), flush=True)
    result = subprocess.run(command)
    if result.returncode != 0:
        sys.exit(result.returncode)

def host_platform():
    if sys.platform == "win32":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return "linux"

def find_emcmake():
    # Mirrors Exporter::detectEmsdk, plus the older EMSCRIPTEN_ROOT and EMSCRIPTEN variables
    names = ("emcmake.exe", "emcmake.bat") if os.name == "nt" else ("emcmake",)
    for var in ("EMSDK", "EMSCRIPTEN_ROOT", "EMSCRIPTEN"):
        root = os.environ.get(var)
        if not root:
            continue
        for name in names:
            for path in (os.path.join(root, "upstream", "emscripten", name), os.path.join(root, name)):
                if os.path.isfile(path):
                    return path
    return shutil.which("emcmake")

def find_ninja():
    # On PATH, else the one of the Visual Studio C++ CMake tools
    ninja = shutil.which("ninja")
    if ninja:
        return ninja
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", ""), "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.isfile(vswhere):
        return None
    installs = subprocess.run([vswhere, "-products", "*", "-property", "installationPath"], capture_output=True, text=True).stdout
    for install in installs.splitlines():
        ninja = os.path.join(install, "Common7", "IDE", "CommonExtensions", "Microsoft", "CMake", "Ninja", "ninja.exe")
        if os.path.isfile(ninja):
            return ninja
    return None

def cached_generator(build_dir):
    # A configured build directory can't change generator
    try:
        with open(os.path.join(build_dir, "CMakeCache.txt"), encoding="utf-8", errors="replace") as cache:
            for line in cache:
                if line.startswith("CMAKE_GENERATOR:INTERNAL="):
                    return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return None

def pbx_string(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'

##
# Based on Steve Robinson comment https://gitlab.kitware.com/cmake/cmake/-/issues/19588
# https://gitlab.com/ssrobins/sdl2-example/blob/cmake_issues_19588/add_xcode_folder_reference.py
##
def add_folder_reference(project, folder_path, target):

    with open(project) as f:
        project_data = f.read()

    folder = os.path.basename(folder_path)

    id1 = str(uuid.uuid4().hex)
    id2 = str(uuid.uuid4().hex)

    pbxbuildfile_header = '/* Begin PBXBuildFile section */'
    project_data = project_data.replace(
        pbxbuildfile_header,
        '{0}\n\t\t{1} /* {2} in Resources */ = {{isa = PBXBuildFile; fileRef = {3} /* {2} */; }};'.format(pbxbuildfile_header, id1, folder, id2),
    )

    if folder.endswith(".xcassets"):
        last_known_file_type = "folder.assetcatalog"
    else:
        last_known_file_type = "folder"

    pbxfilereference_header = '/* Begin PBXFileReference section */'
    project_data = project_data.replace(
        pbxfilereference_header,
        '{0}\n\t\t{1} /* {2} */ = {{isa = PBXFileReference; lastKnownFileType = {3}; name = {4}; path = {5}; sourceTree = "<group>"; }};'.format(pbxfilereference_header, id2, folder, last_known_file_type, pbx_string(folder), pbx_string(folder_path))
    )

    # Get <RESOURCES_PBXGROUP_ID> for 'Resources' in <TARGET>:
    # <PBXGROUP_ID> /* <TARGET> */ = {
    #        isa = PBXGroup;
    #        children = (
    #            <RESOURCES_PBXGROUP_ID> /* Resources */,
    index_pbx_group = project_data.find('/* {0} */ = {{\n\t\t\tisa = PBXGroup;'.format(target))
    index_resources_child = project_data.find('/* Resources */', index_pbx_group)
    if index_pbx_group == -1 or index_resources_child == -1:
        print("Warning: no Resources group in %s, ignoring %s." % (target, folder), flush=True)
        return
    index_start_resources_pbxgroup = project_data.rfind('\t', 0, index_resources_child)
    resources_pbxgroup_id = project_data[index_start_resources_pbxgroup:index_resources_child].strip()

    pbxgroup_section = '{0} /* Resources */ = {{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = ('.format(resources_pbxgroup_id)
    project_data = project_data.replace(
        pbxgroup_section,
        '{0}\n\t\t\t\t{1} /* {2} */,'.format(pbxgroup_section, id2, folder)
    )

    pbxbuildphase_section = 'isa = PBXResourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = ('
    project_data = project_data.replace(
        pbxbuildphase_section,
        '{0}\n\t\t\t\t{1} /* {2} in Resources */,'.format(pbxbuildphase_section, id1, folder)
    )

    with open(project, "w") as f:
        f.write(project_data)

def build():
    parser = argparse.ArgumentParser(description="Configure and build a Doriax project with CMake.")
    parser.add_argument("--platform", "-p", type=str.lower, default=host_platform(), choices=["web", "linux", "windows", "macos", "macos-xcode", "ios-xcode"], help="Platform build type (default: %(default)s)")
    parser.add_argument("--doriax", "-e", default=os.path.dirname(os.path.abspath(__file__)), help="Doriax engine root path")
    parser.add_argument("--project", "-s", help="Source root path of project files (default: <doriax>/project)")
    parser.add_argument("--appname", "-a", help="Project target name (default: from CMakeLists.txt)")
    parser.add_argument("--output", "-o", help="Output directory (default: <doriax>/build/<platform>)")
    parser.add_argument("--build", "-b", action=argparse.BooleanOptionalAction, default=True, help="Build after configuring")
    parser.add_argument("--debug", "-d", action=argparse.BooleanOptionalAction, default=False, help="Build type Debug or Release")
    parser.add_argument("--graphic-backend", "-g", type=str.lower, choices=["glcore", "gles3", "metal", "d3d11", "vulkan"], help="Preferred graphic API")
    parser.add_argument("--generator", "-G", help="CMake generator (default: Ninja if installed, Visual Studio for windows)")
    parser.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 1, help="Parallel build jobs (default: %(default)s)")
    parser.add_argument("--define", "-D", action="append", default=[], metavar="VAR=VALUE", help="Extra CMake definition")
    parser.add_argument("--no-cpp-init", action="store_true", help="No call C++ init on project start")
    parser.add_argument("--no-lua-init", action="store_true", help="No call Lua on project start")
    parser.add_argument("--em-shell-file", help="Emscripten shell file")
    args = parser.parse_args()

    platform = args.platform
    build_type = "Debug" if args.debug else "Release"

    doriax_root = os.path.abspath(args.doriax)
    project_root = os.path.abspath(args.project or os.path.join(doriax_root, "project"))
    build_dir = os.path.abspath(args.output or os.path.join(doriax_root, "build", platform))

    required_host = "macos" if platform in XCODE_PLATFORMS else platform
    if platform != "web" and required_host != host_platform():
        fail("--platform %s must be built on %s" % (platform, required_host))
    if not shutil.which("cmake"):
        fail("cmake not found in PATH")

####
## Executing CMake command
####
    cmake_command = [
        "cmake",
        "-S", doriax_root,
        "-B", build_dir,
        "-DCMAKE_BUILD_TYPE=" + build_type,
        "-DPROJECT_ROOT=" + project_root,
        ]

    generator = args.generator
    if platform in XCODE_PLATFORMS:
        generator = "Xcode"
    elif generator is None and platform == "web" and host_platform() == "windows":
        # emcmake needs a generator on Windows: the build directory's, else Ninja, which
        # emcmake looks for only on PATH, where Visual Studio does not put its own
        generator = cached_generator(build_dir)
        ninja = find_ninja() if generator is None else None
        if ninja:
            generator = "Ninja"
            cmake_command.append("-DCMAKE_MAKE_PROGRAM=" + ninja)
    elif generator is None and platform != "windows" and shutil.which("ninja"):
        # A configured build directory can't change generator
        if not os.path.isfile(os.path.join(build_dir, "CMakeCache.txt")):
            generator = "Ninja"
    if generator:
        cmake_command.extend(["-G", generator])

    if platform == "web":
        emcmake = find_emcmake()
        if not emcmake:
            fail("Emscripten not found, set EMSDK or add emcmake to PATH")
        cmake_command.insert(0, emcmake)
        if args.em_shell_file:
            cmake_command.append('-DEM_ADDITIONAL_LINK_FLAGS=--shell-file "' + os.path.abspath(args.em_shell_file) + '"')

    if platform == "ios-xcode":
        cmake_command.extend(["-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_SYSROOT=iphoneos"])

    if args.appname:
        cmake_command.append("-DAPP_NAME=" + args.appname)
    if args.graphic_backend:
        cmake_command.append("-DGRAPHIC_BACKEND=" + args.graphic_backend)
    if args.no_cpp_init:
        cmake_command.append("-DNO_CPP_INIT=1")
    if args.no_lua_init:
        cmake_command.append("-DNO_LUA_INIT=1")
    cmake_command.extend("-D" + define for define in args.define)

    run(cmake_command)

####
## Adding folder references to Xcode app bundle
####
    # Non-Metal macOS builds are not bundles, assets and lua are copied next to the executable
    if platform == "ios-xcode" or (platform == "macos-xcode" and args.graphic_backend in (None, "metal")):
        xcode_project = glob.glob(os.path.join(glob.escape(build_dir), "*.xcodeproj"))[0]
        target = os.path.splitext(os.path.basename(xcode_project))[0]

        for folder in ("assets", "lua"):
            folder_path = os.path.join(project_root, folder)
            if os.path.exists(folder_path):
                add_folder_reference(os.path.join(xcode_project, "project.pbxproj"), folder_path, target)
            else:
                print("Warning: %s does not exist, ignoring." % (folder_path), flush=True)

####
## Executing CMake build command
####
    if args.build:
        cmake_build_command = [
            "cmake",
            "--build", build_dir,
            "--config", build_type,
            "--parallel", str(args.jobs),
            ]
        if platform == "ios-xcode":
            cmake_build_command.extend(["--", "-sdk", "iphonesimulator"])

        run(cmake_build_command)

        # <App>.html lacks the project's web settings
        pages = glob.glob(os.path.join(glob.escape(build_dir), "*.export.html")) if platform == "web" else []
        if pages:
            # The newest one, as a build with an earlier --appname can leave its page
            page = os.path.basename(max(pages, key=os.path.getmtime))
            if page.lower() != "index.export.html":
                page = "index.html"
            print("Web build ready: serve %s and open %s" % (build_dir, page), flush=True)


if __name__ == '__main__':
    # piped output on Windows uses the ANSI code page, which cannot print every path
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    build()
