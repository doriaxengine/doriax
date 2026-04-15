#!/usr/bin/env python

# /*
# (c) 2026 Eduardo Doria.
# */

import os
import sys
import subprocess
import click
import platform
import glob
import base64
import tempfile
import shutil
import re

def get_define(property):
    if property == 'Ult':
        return 'MATERIAL_UNLIT'
    elif property == 'Tex':
        return 'HAS_TEXTURE'
    elif property == 'Uv1':
        return 'HAS_UV_SET1'
    elif property == 'Uv2':
        return 'HAS_UV_SET2'
    elif property == 'Puc':
        return 'USE_PUNCTUAL'
    elif property == 'Shw':
        return 'USE_SHADOWS'
    elif property == 'Pcf':
        return 'USE_SHADOWS_PCF'
    elif property == 'Nor':
        return 'HAS_NORMALS'
    elif property == 'Nmp':
        return 'HAS_NORMAL_MAP'
    elif property == 'Tan':
        return 'HAS_TANGENTS'
    elif property == 'Ftx':
        return 'HAS_FONTATLAS_TEXTURE'
    elif property == 'Vc3':
        return 'HAS_VERTEX_COLOR_VEC3'
    elif property == 'Vc4':
        return 'HAS_VERTEX_COLOR_VEC4'
    elif property == 'Txr':
        return 'HAS_TEXTURERECT'
    elif property == 'Fog':
        return 'HAS_FOG'
    elif property == 'Ski':
        return 'HAS_SKINNING'
    elif property == 'Mta':
        return 'HAS_MORPHTARGET'
    elif property == 'Mnr':
        return 'HAS_MORPHNORMAL'
    elif property == 'Mtg':
        return 'HAS_MORPHTANGENT'
    elif property == 'Ter':
        return 'HAS_TERRAIN'
    elif property == 'Ist':
        return 'HAS_INSTANCING'
    else:
        sys.exit('Not found value for property: '+property)

def get_vert(shaderType):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(script_dir, "shaderlib", shaderType+'.vert')

def get_frag(shaderType):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(script_dir, "shaderlib", shaderType+'.frag')

def get_header_output(engine_root):
    return os.path.join(engine_root, "engine", "shaders")

def get_sbs_output(shader, lang, temp_dir):
    outpath = os.path.join(temp_dir, shader+"_"+lang)
    return outpath

def get_default_shaders():
    s =  "mesh_Uv1PucNorNmpTanVc4;"
    s += "mesh_Uv1PucNorNmpTanVc4Fog;"
    s += "mesh_Uv1PucNorNmpTanVc4Ski;"
    s += "mesh_Uv1PucNorNmpTanVc4FogSki;"
    s += "mesh_Uv1PucNorNmpTan;"
    s += "mesh_Uv1PucNorNmpTanFog;"
    s += "mesh_Uv1PucShwPcfNorNmpTanVc4;"
    s += "mesh_Uv1PucShwPcfNorNmpTanVc4Fog;"
    s += "mesh_Uv1PucShwPcfNorNmpTan;"
    s += "mesh_Uv1PucShwPcfNorNmpTanFog;"
    s += "mesh_Uv1PucNorVc4;"
    s += "mesh_Uv1PucNorVc4Fog;"
    s += "mesh_Uv1PucNor;"
    s += "mesh_Uv1PucNorFog;"
    s += "mesh_Uv1PucNorVc4Txr;"
    s += "mesh_Uv1PucNorVc4TxrIst;"
    s += "mesh_Uv1PucNorVc4Ist;"
    s += "mesh_Uv1PucShwPcfNorVc4;"
    s += "mesh_Uv1PucShwPcfNorVc4Ist;"
    s += "mesh_Uv1PucShwPcfNorVc4Fog;"
    s += "mesh_Uv1PucShwPcfNorVc4Txr;"
    s += "mesh_Uv1PucShwPcfNorVc4FogIst;"
    s += "mesh_Uv1PucShwPcfNorVc4TxrIst;"
    s += "mesh_Uv1PucShwPcfNor;"
    s += "mesh_Uv1PucShwPcfNorSki;"
    s += "mesh_Uv1PucShwPcfNorSkiIst;"
    s += "mesh_Uv1PucShwPcfNorNmpVc4;"
    s += "mesh_Uv1PucShwPcfNorFog;"
    s += "mesh_Uv1PucShwPcfNorFogIst;"
    s += "mesh_Uv1PucShwPcfNorFogSki;"
    s += "mesh_UltUv1Vc4;"
    s += "mesh_UltUv1Vc4Fog;"
    s += "mesh_Ult;"
    s += "mesh_UltUv1;"
    s += "mesh_UltVc4;"
    s += "mesh_Uv1PucNorTer;"
    s += "mesh_Uv1PucShwPcfNorTer;"
    s += "mesh_Uv1PucShwPcfNorFogTer;"
    s += "mesh_UltUv1Ter;"
    s += "mesh_UltSki;"
    s += "mesh_UltUv1Fog;"
    s += "mesh_UltUv1Ski;"
    s += "mesh_UltVc4Ski;"
    s += "mesh_UltUv1Vc4Ist;"
    s += "mesh_UltUv1Vc4Txr;"
    s += "mesh_UltUv1Vc4TxrIst;"
    s += "mesh_UltUv1Vc4TxrFog;"
    s += "mesh_UltMtaMnrMtg;"
    s += "mesh_PucNorVc4Ski;"
    s += "mesh_PucShwPcfNor;"
    s += "mesh_PucShwPcfNorSki;"
    s += "mesh_PucShwPcfNorFog;"
    s += "mesh_PucShwPcfNorFogSki;"
    s += "mesh_PucShwPcfNorVc4;"
    s += "mesh_PucShwPcfNorVc4Ski;"
    s += "mesh_PucShwPcfNorVc4Fog;"
    s += "mesh_PucShwPcfNorVc4Ist;"
    s += "mesh_PucShwPcfNorVc4FogSki;"
    s += "mesh_PucShwPcfNorTanMtaMnr;"
    s += "mesh_PucShwPcfNorTanMtaMnrMtg;"
    s += "mesh_PucShwPcfNorTanFogMtaMnr;"
    s += "mesh_PucShwPcfNorTanFogMtaMnrMtg;"
    s += "depth;"
    s += "depth_Tex;"
    s += "depth_TexIst;"
    s += "depth_Ski;"
    s += "depth_Mta;"
    s += "depth_MtaMnr;"
    s += "depth_MtaMnrMtg;"
    s += "depth_Ist;"
    s += "depth_SkiIst;"
    s += "depth_MtaIst;"
    s += "depth_MtaMnrIst;"
    s += "depth_MtaMnrMtgIst;"
    s += "depth_Ter;"
    s += "sky;"
    s += "ui_Vc4;"
    s += "ui_TexVc4;"
    s += "ui_Ftx;"
    s += "points_Vc4;"
    s += "points_TexVc4;"
    s += "points_TexVc4Txr;"
    s += "lines_Vc4"
    return s

def get_default_langs():
    l =  "glsl410;"
    l += "glsl300es;"
    l += "msl21ios;"
    l += "msl21macos;"
    l += "hlsl5"
    return l

def get_bin_exec():
    plt = platform.system()

    execname = "supershader"

    # Get the directory where this script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))
    binpath = os.path.join(script_dir, "bin")

    outpath = os.path.join(binpath, execname)

    if plt == "Windows":
        outpath = os.path.join(binpath, "windows", execname + ".exe")
    elif plt == "Linux":
        outpath = os.path.join(binpath, "linux", execname)
    elif plt == "Darwin":
        outpath = os.path.join(binpath, "macos", execname)
    else:
        print("Unidentified system")
        sys.exit(1)
    
    return outpath

def create_c_header(engine_root, temp_dir):
    filesshaderlist = {}

    for file in list(glob.glob(os.path.join(temp_dir, '*.sbs'))):
        shadername = os.path.splitext(os.path.basename(file))[0]

        shadersplit = shadername.split('_')
        lang = shadersplit[len(shadersplit)-1]

        filesshaderlist.setdefault(lang,[]).append(file)

    for lang in filesshaderlist:
        new_shaderslist = []
        headerpath = os.path.join(get_header_output(engine_root), lang+".h")

        print("Updating:", headerpath)

        # Read existing header file if it exists
        existing_content = ""
        existing_shaders = set()
        all_existing_shader_names = []
        if os.path.exists(headerpath):
            with open(headerpath, "r") as headerfile:
                existing_content = headerfile.read()

            # Extract existing shader names using regex
            shader_pattern = r'static const std::string (\w+) = '
            all_existing_shader_names = re.findall(shader_pattern, existing_content)
            existing_shaders = set(all_existing_shader_names)

        # Prepare new shader data
        new_shaders = {}
        filesshaderlist[lang].sort()

        for file in filesshaderlist[lang]:
            with open(file, "rb") as sbsfile:
                encodedShader = base64.b64encode(sbsfile.read()).decode("utf-8")

            shadername = os.path.splitext(os.path.basename(file))[0]
            new_shaderslist.append(shadername)

            stringSize = 100
            encodedShaderChunks = [encodedShader[i:i+stringSize] for i in range(0, len(encodedShader), stringSize)]

            shader_definition = "static const std::string " + shadername + " = "
            for encodedShaderChunk in encodedShaderChunks:
                shader_definition += "\n    \"" + encodedShaderChunk + "\""
            shader_definition += ";"

            new_shaders[shadername] = shader_definition

        # Create final list of all shaders (existing + new, avoiding duplicates)
        final_shaders_list = []
        # Add existing shaders that are not being replaced
        for existing_shader in all_existing_shader_names:
            if existing_shader not in new_shaders:
                final_shaders_list.append(existing_shader)
        # Add new/updated shaders
        final_shaders_list.extend(new_shaderslist)
        # Sort the final list
        final_shaders_list.sort()

        # Update or create header file
        if os.path.exists(headerpath) and existing_content:
            # Update existing file
            updated_content = existing_content

            # Replace existing shaders
            for shadername, shader_def in new_shaders.items():
                if shadername in existing_shaders:
                    # Replace existing shader definition
                    old_pattern = r'static const std::string ' + re.escape(shadername) + r' = (?:\n\s*"[^"]*")*;'
                    updated_content = re.sub(old_pattern, shader_def, updated_content, flags=re.MULTILINE)
                else:
                    # Add new shader before the getBase64Shader function
                    insertion_point = updated_content.find("std::string getBase64Shader(std::string name) {")
                    if insertion_point != -1:
                        updated_content = (updated_content[:insertion_point] + 
                                         shader_def + "\n\n" + 
                                         updated_content[insertion_point:])

            # Update the getBase64Shader function - find the complete function
            function_pattern = r'std::string getBase64Shader\(std::string name\) \{.*?\n\}'
            function_match = re.search(function_pattern, updated_content, re.DOTALL)

            if function_match:
                # Generate new function with ALL shaders (existing + new)
                new_function = "std::string getBase64Shader(std::string name) {"
                first = True
                for shadername in final_shaders_list:
                    new_function += "\n"
                    if first:
                        new_function += "    if (name == \"" + shadername + "\") {"
                    else:
                        new_function += "    } else if (name == \"" + shadername + "\") {"
                    new_function += "\n        return " + shadername + ";"
                    first = False

                if not first:
                    new_function += "\n    }"
                new_function += "\n    return \"\";\n}"

                # Replace the entire function
                updated_content = re.sub(function_pattern, new_function, updated_content, flags=re.DOTALL)

            # Write updated content
            with open(headerpath, "w") as headerfile:
                headerfile.write(updated_content)

        else:
            # Create new file (original behavior)
            with open(headerpath, "w") as headerfile:
                headerfile.write("#ifndef SHADER_"+lang+"_h\n")
                headerfile.write("#define SHADER_"+lang+"_h\n\n")
                headerfile.write("#include <string>\n\n")

                for shadername in new_shaderslist:
                    headerfile.write(new_shaders[shadername] + "\n")

                headerfile.write("\n")
                first = True
                headerfile.write("std::string getBase64Shader(std::string name) {")
                for shadername in new_shaderslist:
                    headerfile.write("\n")
                    if first:
                        headerfile.write("    if (name == \"" + shadername + "\") {")
                    else:
                        headerfile.write("    } else if (name == \"" + shadername + "\") {")
                    headerfile.write("\n        return " + shadername + ";")
                    first = False

                if not first:
                    headerfile.write("\n    }")

                headerfile.write("\n    return \"\";\n}\n\n")
                headerfile.write("#endif //SHADER_"+lang+"_h\n")

@click.command()
@click.option('--shaders', '-s', default=get_default_shaders(), help="Target shader type, seperated by ';'")
@click.option('--langs', '-l', default=get_default_langs(), required=True, help="Target shader language, seperated by ';'")
@click.option('--engine-root', '-r', default='', type=click.Path(), help="Source root path of Doriax")
@click.option('--verbose/--no-verbose', '-v', default=False, help="Output more information")
@click.option('--max-lights', '-ml', default=6, type=int, help="Value of MAX_LIGHTS macro")
@click.option('--max-shadowsmap', default=6, type=int, help="Value of MAX_SHADOWSMAP macro")
@click.option('--max-shadowscubemap', default=1, type=int, help="Value of MAX_SHADOWSCUBEMAP macro")
@click.option('--max-shadowcascades', default=4, type=int, help="Value of MAX_SHADOWCASCADES macro")
def generate(shaders, langs, engine_root, verbose, max_lights, max_shadowsmap, max_shadowscubemap, max_shadowcascades):

    if (engine_root == ''):
        engine_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    shadersList = [x.strip() for x in shaders.split(';')]
    langsList = [x.strip() for x in langs.split(';')]

    # Create temporary directory with specific name
    temp_base = tempfile.gettempdir()
    temp_dir = os.path.join(temp_base, "supershader_cache")

    # Remove existing directory if it exists and create fresh one
    if os.path.exists(temp_dir):
        shutil.rmtree(temp_dir, ignore_errors=True)
    os.makedirs(temp_dir)

    print(f"Using temporary directory: {temp_dir}")

    try:
        for lang in langsList:

            for shader in shadersList:

                splitShader = shader.split('_')
                shaderType = splitShader[0]
                properties = ''
                if len(splitShader) >= 2:
                    properties = splitShader[1]

                defines = ''
                defines += 'MAX_LIGHTS='+str(max_lights)
                defines += ';MAX_SHADOWSMAP='+str(max_shadowsmap)
                defines += ';MAX_SHADOWSCUBEMAP='+str(max_shadowscubemap)
                defines += ';MAX_SHADOWCASCADES='+str(max_shadowcascades)
                defines += ';MAX_BONES='+str(70)

                while properties != '':
                    if len(defines) > 0:
                        defines += ';'
                    defines += get_define(properties[:3])
                    properties = properties[3:]

                vert = get_vert(shaderType)
                frag = get_frag(shaderType)
                output = get_sbs_output(shader, lang, temp_dir)

                print('Generating', shader, 'for', lang)

                if verbose:
                    print(get_bin_exec(), "--lang", lang, "--vert", vert, "--frag", frag, "--output", output, "--output-type", "binary", "--defines", "'"+defines+"'")
                command = subprocess.run([get_bin_exec(), "--lang", lang, "--vert", vert, "--frag", frag, "--output", output, "--output-type", "binary", "--defines", defines], capture_output=True)

                sys.stdout.buffer.write(command.stdout)
                sys.stderr.buffer.write(command.stderr)
                if command.returncode != 0:
                    sys.exit(command.returncode)

        create_c_header(engine_root, temp_dir)

    finally:
        # Clean up temporary directory
        print(f"Cleaning up temporary directory: {temp_dir}")
        shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == '__main__':
    generate()