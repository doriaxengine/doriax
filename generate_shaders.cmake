# Convert the list string back to a CMake list
string(REPLACE " " ";" SHADER_FILES "${SHADER_FILES}")

# Make sure the output directory exists
get_filename_component(OUTPUT_DIR "${SHADERS_HEADER}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

# Start writing the header file with a guard
file(WRITE "${SHADERS_HEADER}" "#pragma once\n\n#include <string>\n#include <unordered_map>\n\nnamespace doriax::editor {\n\n")

# Process each shader file
foreach(shader_file IN LISTS SHADER_FILES)
    # Get the relative path with respect to the base directory
    file(RELATIVE_PATH shader_rel_path "${SHADERS_DIR}" "${shader_file}")
    file(TO_CMAKE_PATH "${shader_rel_path}" shader_rel_path) # Ensure forward slashes

    # Create a valid C++ variable name by replacing /, \, and . with _
    set(cpp_var_name "${shader_rel_path}")
    string(REPLACE "/" "_" cpp_var_name "${cpp_var_name}")
    string(REPLACE "\\" "_" cpp_var_name "${cpp_var_name}")
    string(REPLACE "." "_" cpp_var_name "${cpp_var_name}")

    # Read the shader file content
    file(READ "${shader_file}" shader_content)

    # Escape special characters for C++ string literal
    string(REPLACE "\\" "\\\\" shader_content "${shader_content}")
    string(REPLACE "\"" "\\\"" shader_content "${shader_content}")
    string(REPLACE "\n" "\\n\"\n\"" shader_content "${shader_content}")

    # Write the C++ variable to the header file
    file(APPEND "${SHADERS_HEADER}" "const std::string ${cpp_var_name} = \"${shader_content}\";\n\n")
endforeach()

# Add the map for easy lookup by relative path
file(APPEND "${SHADERS_HEADER}" "const std::unordered_map<std::string, std::string> shaderMap = {\n")

foreach(shader_file IN LISTS SHADER_FILES)
    # Get the relative path for the map key
    file(RELATIVE_PATH shader_rel_path "${SHADERS_DIR}" "${shader_file}")
    file(TO_CMAKE_PATH "${shader_rel_path}" shader_rel_path)

    # Construct the variable name as before
    set(cpp_var_name "${shader_rel_path}")
    string(REPLACE "/" "_" cpp_var_name "${cpp_var_name}")
    string(REPLACE "\\" "_" cpp_var_name "${cpp_var_name}")
    string(REPLACE "." "_" cpp_var_name "${cpp_var_name}")

    # Append entry to the shader map
    file(APPEND "${SHADERS_HEADER}" "    {\"${shader_rel_path}\", ${cpp_var_name}},\n")
endforeach()

file(APPEND "${SHADERS_HEADER}" "};\n\n}\n")
