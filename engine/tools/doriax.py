#!/usr/bin/env python

# /*
# Based on Filip Stoklas code (http://forums.4fips.com/viewtopic.php?f=3&t=1201)
# (c) 2020 Eduardo Doria.
# */

import os
import sys
import subprocess
import stat
import click
import uuid
from shutil import rmtree

def resolve_path(rel_path):
    return os.path.abspath(os.path.join(os.path.dirname(__file__), rel_path))

def rmtree_silent(root):
    def remove_readonly_handler(fn, root, excinfo):
        if fn is os.rmdir:
            if os.path.isdir(root): # if exists
                os.chmod(root, stat.S_IWRITE) # make writable
                os.rmdir(root)
        elif fn is os.remove:
            if os.path.isfile(root): # if exists
                os.chmod(root, stat.S_IWRITE) # make writable
                os.remove(root)
    rmtree(root, onerror=remove_readonly_handler)

def makedirs_silent(root):
    try:
        os.makedirs(root)
    except OSError: # mute if exists
        pass

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
        '{0}\n\t\t{1} /* {2} */ = {{isa = PBXFileReference; lastKnownFileType = {3}; name = {2}; path = {4}; sourceTree = "<group>"; }};'.format(pbxfilereference_header, id2, folder, last_known_file_type, folder_path)
    )
    
    # Get <RESOURCES_PBXGROUP_ID> for 'Resources' in <TARGET>:
    # <PBXGROUP_ID> /* <TARGET> */ = {
    #        isa = PBXGroup;
    #        children = (
    #            <RESOURCES_PBXGROUP_ID> /* Resources */,
    index_pbx_group = project_data.find('/* {0} */ = {{\n\t\t\tisa = PBXGroup;'.format(target))
    index_resources_child = project_data.find('/* Resources */', index_pbx_group)
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


def create_build_dir(name):
    build_dir = resolve_path(name)

    if not os.path.isdir(build_dir):
        rmtree_silent(build_dir)
    makedirs_silent(build_dir)
    os.chdir(build_dir)

    return build_dir

@click.command()
@click.option('--platform', '-p', required=True, type=click.Choice(['web', 'linux', 'windows', 'macos', 'macos-xcode', 'ios-xcode'], case_sensitive=False), help="Plataform build type")
@click.option('--project', '-s', default='../project', type=click.Path(), help="Source root path of project files")
@click.option('--appname', '-a', default='doriax-project', help="Project target name")
@click.option('--output', '-o', type=click.Path(), help="Output directory")
@click.option('--build/--no-build', '-b', default=False, help="Build or no build generated Xcode project")
@click.option('--debug/--no-debug', '-d', default=False, help="Build type Debug or Release")
@click.option('--graphic-backend', '-g', type=click.Choice(['glcore', 'gles3', 'metal', 'd3d11'], case_sensitive=False), help="Preferred graphic API")
@click.option('--app-backend', '-m', type=click.Choice(['emscripten', 'android', 'sokol', 'glfw', 'apple'], case_sensitive=False), help="Preferred application API")
@click.option('--no-cpp-init', is_flag=True, help="No call C++ init on project start")
@click.option('--no-lua-init', is_flag=True, help="No call Lua on project start")
@click.option('--em-shell-file', type=click.Path(), help="Emscripten shell file")
def build(platform, project, appname, output, build, debug, graphic_backend, app_backend, no_lua_init, no_cpp_init, em_shell_file):

    projectRoot = os.path.abspath(project)

    cmake_generator = ""
    source_path = projectRoot
    cmake_definitions = []

    build_config = []
    native_build_config = []

    build_type = "Release"
    if debug:
        build_type = "Debug"

    if output==None:
        build_dir = create_build_dir(os.path.join("build",platform))
    else:
        build_dir = create_build_dir(output)

    if graphic_backend!=None:
        cmake_definitions.append("-DGRAPHIC_BACKEND="+graphic_backend)

    if app_backend!=None:
        cmake_definitions.append("-DAPP_BACKEND="+app_backend)

    if no_cpp_init:
        cmake_definitions.append("-DNO_CPP_INIT=1")
    if no_lua_init:
        cmake_definitions.append("-DNO_LUA_INIT=1")

####
## Preparing web (Emscripten) environment
####
    if (platform == "web"):

        emscripten = ""
        if "EMSCRIPTEN_ROOT" in os.environ:
            emscripten = os.path.expandvars("$EMSCRIPTEN_ROOT")
        elif "EMSCRIPTEN" in os.environ:
            emscripten = os.path.expandvars("$EMSCRIPTEN")
        else:
            print ("Not found EMSCRIPTEN_ROOT or EMSCRIPTEN environment variable")
            sys.exit(os.EX_CONFIG)

        from sys import platform as sys_platform
        if sys_platform == "linux" or sys_platform == "linux2" or sys_platform == "darwin":
            cmake_generator = "Unix Makefiles"
        elif sys_platform == "win32":
            cmake_generator = "MinGW Makefiles"
        
        cmake_definitions.extend([
            "-DCMAKE_BUILD_TYPE="+build_type,
            "-DCMAKE_TOOLCHAIN_FILE="+emscripten+"/cmake/Modules/Platform/Emscripten.cmake"
        ])

        if em_shell_file!=None:
            cmake_definitions.extend([
                '-DEM_ADDITIONAL_LINK_FLAGS=--shell-file "' + em_shell_file + '"'
            ])

####
## Preparing Visual Studio (Windows) environment
####
    if (platform == "windows"):

        #cmake_generator = "Visual Studio 16 2019"
        cmake_generator = "Visual Studio 17 2022" #TODO: make this editable

        build_config = ["--config", build_type]
        
####
## Preparing Ninja (Linux, MacOS) environment
####
    if (platform == "linux" or platform == "macos"):

        cmake_generator = "Ninja"

        build_config = ["--config", build_type]

####
## Preparing macOS (Xcode generator) environment
####
    if (platform == "macos-xcode"):

        cmake_generator = "Xcode"

        build_config = ["--config", build_type]

    #    cmake_generator = "Xcode"
    #    system_name = "macOS"
    #    OSX_SDK="macosx"
    #    build_sdk = "macosx"
#
    #    build_config = ["--config", build_type]
    #    native_build_config = ["-sdk", build_sdk]
    #    cmake_definitions.extend([
    #        "-DCMAKE_SYSTEM_NAME="+system_name,
    #        "-DCMAKE_OSX_SYSROOT="+OSX_SDK
    #    ])

####
## Preparing ios (Apple) environment
####
    if (platform == "ios-xcode"):

        cmake_generator = "Xcode"
        system_name = "iOS"
        OSX_SDK="iphoneos"
        build_sdk = "iphonesimulator"

        build_config = ["--config", build_type]
        native_build_config = ["-sdk", build_sdk]
        cmake_definitions.extend([
            "-DCMAKE_SYSTEM_NAME="+system_name,
            "-DCMAKE_OSX_SYSROOT="+OSX_SDK
        ])


####
## Executing CMake command
####
    cmake_command = [
        "cmake",
        "-DPROJECT_ROOT="+projectRoot,
        "-DAPP_NAME="+appname,
        "-G", cmake_generator,
        source_path,
        ]
    cmake_command.extend(cmake_definitions)

    subprocess.run(cmake_command).check_returncode()

####
## Adding folder reference to iOS project
####
    if (platform == "ios-xcode" or platform == "macos-xcode"):
        xcode_project = os.path.join(appname+".xcodeproj", "project.pbxproj")
        assets_path = os.path.join(projectRoot, "assets")
        lua_path = os.path.join(projectRoot, "lua")

        if os.path.exists(assets_path):
            add_folder_reference(xcode_project, assets_path, appname)
        else:
            print("Warning: %s does not exist, ignoring." % (assets_path), flush=True)

        if os.path.exists(lua_path):
            add_folder_reference(xcode_project, lua_path, appname)
        else:
            print("Warning: %s does not exist, ignoring." % (lua_path), flush=True)

####
## Executing CMake build command
####
    if build:
        cmake_build_command = [
            "cmake",
            "--build", build_dir
            ]
        cmake_build_command.extend(build_config)
        cmake_build_command.append("--")
        cmake_build_command.extend(native_build_config)

        subprocess.run(cmake_build_command).check_returncode()


if __name__ == '__main__':
    build()