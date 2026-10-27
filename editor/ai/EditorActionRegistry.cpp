// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "EditorActionRegistry.h"

#include "AiPathUtils.h"
#include "util/CxxStandards.h"

#include <cctype>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace doriax::editor::ai {

namespace {

Json objectSchema(std::initializer_list<std::pair<const char*, Json>> properties,
                  std::initializer_list<const char*> required = {}) {
    Json props = Json::object();
    for (const auto& item : properties) {
        props[item.first] = item.second;
    }
    Json req = Json::array();
    for (const char* key : required) {
        req.push_back(key);
    }
    return {
        {"type", "object"},
        {"additionalProperties", false},
        {"properties", props},
        {"required", req}
    };
}

Json objectSchemaFromProperties(Json properties, std::initializer_list<const char*> required = {}) {
    Json req = Json::array();
    for (const char* key : required) {
        req.push_back(key);
    }
    return {
        {"type", "object"},
        {"additionalProperties", false},
        {"properties", std::move(properties)},
        {"required", req}
    };
}

Json stringSchema(const std::string& description) {
    return {{"type", "string"}, {"description", description}};
}

Json numberSchema(const std::string& description) {
    return {{"type", "number"}, {"description", description}};
}

Json integerSchema(const std::string& description) {
    return {{"type", "integer"}, {"description", description}};
}

Json boolSchema(const std::string& description) {
    return {{"type", "boolean"}, {"description", description}};
}

Json vector2Schema(const std::string& description) {
    return {
        {"type", "object"},
        {"description", description},
        {"additionalProperties", false},
        {"properties", {
            {"x", numberSchema("X value")},
            {"y", numberSchema("Y value")}
        }}
    };
}

Json vector3Schema(const std::string& description) {
    return {
        {"type", "object"},
        {"description", description},
        {"additionalProperties", false},
        {"properties", {
            {"x", numberSchema("X value")},
            {"y", numberSchema("Y value")},
            {"z", numberSchema("Z value")}
        }}
    };
}

Json vector4Schema(const std::string& description) {
    return {
        {"type", "object"},
        {"description", description},
        {"additionalProperties", false},
        {"properties", {
            {"x", numberSchema("X value")},
            {"y", numberSchema("Y value")},
            {"z", numberSchema("Z value")},
            {"w", numberSchema("W value")}
        }}
    };
}

Json quaternionSchema(const std::string& description) {
    return {
        {"type", "object"},
        {"description", description},
        {"additionalProperties", false},
        {"properties", {
            {"w", numberSchema("W value")},
            {"x", numberSchema("X value")},
            {"y", numberSchema("Y value")},
            {"z", numberSchema("Z value")}
        }}
    };
}

Json propertyValueFields(std::initializer_list<std::pair<const char*, Json>> extra = {}) {
    Json fields = Json::object({
        {"bool_value", boolSchema("Boolean property value")},
        {"int_value", integerSchema("Integer/enum property value")},
        {"number_value", numberSchema("Float/double property value")},
        {"string_value", stringSchema("String property value. The asset-path property filename takes a project-relative path inside the assets directory (get_project_summary reports it as assets_dir), or an empty string to clear it. On a Font property it sets the main font and keeps the fallbacks. An Ease property takes an ease name such as SINE_IN_OUT")},
        {"font_paths", {
            {"type", "array"},
            {"description", "Font property value: project-relative font paths inside the assets directory, main font first and fallback fonts after it. Slots not listed are cleared"},
            {"items", {{"type", "string"}}}
        }},
        {"vector2_value", vector2Schema("Vector2 property value")},
        {"vector3_value", vector3Schema("Vector3 or Color3 property value")},
        {"vector4_value", vector4Schema("Vector4 or Color4 property value")},
        {"quat_value", quaternionSchema("Quaternion property value")},
        {"texture_path", stringSchema("Project-relative texture/resource path for Texture properties, inside the assets directory reported by get_project_summary as assets_dir. For .svg sources an optional '?svgScale=N' suffix sets the rasterization scale (e.g. 'ui/icon.svg?svgScale=2'). A cubemap (Sky or ReflectionProbe texture) takes one cross-layout image or six faces joined by '|', ordered +X|-X|+Y|-Y|+Z|-Z")},
        {"entity_value", integerSchema("Entity id for Entity or EntityReference properties")},
        {"entity_scene_id", integerSchema("Scene id for EntityReference properties, only for an entity in another scene. Omit for the same scene")},
        {"bursts_value", {
            {"type", "array"},
            {"description", "Particles bursts: the whole list, one entry per burst"},
            {"items", objectSchema({
                {"time", numberSchema("Seconds after the emitter starts")},
                {"min_count", integerSchema("Fewest particles the burst emits")},
                {"max_count", integerSchema("Most particles the burst emits")}
            })}
        }},
        {"gradient_value", {
            {"type", "object"},
            {"description", "Particles colorGradient: color stops over the particle life"},
            {"additionalProperties", false},
            {"properties", {
                {"use_srgb", boolSchema("Colors are sRGB")},
                {"stops", {
                    {"type", "array"},
                    {"items", objectSchema({
                        {"time", numberSchema("Particle life from 0 to 1")},
                        {"color", vector3Schema("Stop color")}
                    })}
                }}
            }}
        }}
    });
    for (const auto& item : extra) {
        fields[item.first] = item.second;
    }
    return fields;
}

const std::vector<ToolDefinition>& cachedTools() {
    static const std::vector<ToolDefinition> definitions = {
        {
            "get_editor_guidance",
            "Return the editor's complete working rules: the server instructions in full, plus the rules for physics, Lua and C++ scripts, shaders and project builds. Call it once before your first change in a session, since some clients show only the start of the server instructions, or none of them.",
            objectSchema({}),
            true,
            true
        },
        {
            "get_project_summary",
            "Read a compact summary of the current project, selected scene, selected entities, resource roots, and cxx_standard (" + cxxStandardList() + ").",
            objectSchema({}),
            true
        },
        {
            "search_resources",
            "Search project files by name and optional file type. Omit query (or pass an empty one) to browse: it then lists project files, narrowed by type when given. This only reads project paths.",
            objectSchema({
                {"query", stringSchema("Case-insensitive filename search text. Omit to list files instead of filtering")},
                {"type", stringSchema("Optional type: model, image, material, scene, script, audio, font, any")},
                {"max_results", integerSchema("Maximum number of paths to return, 1-50")}
            }),
            true
        },
        {
            "list_scene_entities",
            "List entities in a scene by id/name/component summary.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")}
            }),
            true
        },
        {
            "inspect_entity",
            "Read one entity's name, selection state, hierarchy data, components, and optionally all supported property values. For a mesh entity it also returns mesh_bounds: the measured local AABB (local_size/local_center) plus the accumulated world_scale and the resulting world_size. Use local_size to size anything expressed in mesh-local units, such as collision shapes.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"include_properties", boolSchema("When true, include supported component property values")}
            }),
            true
        },
        {
            "inspect_component",
            "Read supported property names, types, and current values for one entity component. For the Mesh component it also returns mesh_bounds with the measured local AABB and world size.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"component", stringSchema("Component name, e.g. Transform, Mesh, Camera, TerrainComponent")}
            }, {"component"}),
            true
        },
        {
            "list_component_types",
            "List component names accepted by add_component, remove_component, inspect_component, and set_component_property.",
            objectSchema({}),
            true
        },
        {
            "search_engine_api",
            "Search authoritative Doriax engine API symbols generated from Lua bindings and C++ headers. Use this before writing scripts or engine API code.",
            objectSchema({
                {"query", stringSchema("API symbol, class, method, property, or behavior to search for, e.g. Mesh setColor")},
                {"parent", stringSchema("Optional class/enum parent filter, e.g. Mesh")},
                {"max_results", integerSchema("Maximum number of symbols to return, 1-50")}
            }, {"query"}),
            true
        },
        {
            "create_entity",
            "Create an undoable entity in the selected scene using an allowed editor entity type.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"name", stringSchema("Entity display name")},
                {"type", stringSchema("Allowed type: empty, object, box, plane, wall, mirror, sphere, cylinder, capsule, torus, image, sprite, tilemap, text, button, scrollbar, progressbar, textedit, panel, polygon, container, point_light, directional_light, spot_light, light_2d, occluder_2d, joint2d, joint3d, body2d, body3d, sky, fog, camera, sound, sound_3d, animation, sprite_animation, position_action, rotation_action, scale_action, color_action, alpha_action, translate_tracks, rotate_tracks, scale_tracks, model, particles, points, lines, mesh_polygon, terrain, reflection_probe, water, water_exclusion. light_2d = 2D radius-falloff light (Light2DComponent); occluder_2d = 2D shadow caster (Occluder2DComponent); water_exclusion = box volume keeping water out (WaterExclusionComponent). To keep water out of a boat, add a WaterExclusion component to the boat itself instead: its default Hull shape wraps the boat's mesh.")},
                {"parent_id", integerSchema("Optional parent entity id for transform entities. For animation and action types it is the entity they animate")},
                {"position", vector3Schema("Optional local position")}
            }, {"name", "type"}),
            false
        },
        {
            "set_entity_transform",
            "Set an entity transform through the undoable command system.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"position", vector3Schema("Optional local position")},
                {"rotation", quaternionSchema("Optional local rotation quaternion")},
                {"scale", vector3Schema("Optional local scale")}
            }),
            false
        },
        {
            "rename_entity",
            "Rename an entity through the undoable command system.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"new_name", stringSchema("New entity name")}
            }, {"new_name"}),
            false
        },
        {
            "delete_entity",
            "Delete an entity through the undoable command system.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")}
            }),
            false
        },
        {
            "duplicate_entity",
            "Duplicate one entity through the undoable command system.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")}
            }),
            false
        },
        {
            "reparent_entity",
            "Move a transform entity under a new parent or scene root through MoveEntityOrderCmd.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id to move")},
                {"entity_name", stringSchema("Entity name to move, used only when entity_id is omitted")},
                {"parent_id", integerSchema("New parent entity id. Use 0 or omit for scene root")},
                {"parent_name", stringSchema("New parent entity name, used only when parent_id is omitted")}
            }),
            false
        },
        {
            "move_entity_order",
            "Move an entity relative to another entity in hierarchy order.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id to move")},
                {"entity_name", stringSchema("Entity name to move, used only when entity_id is omitted")},
                {"target_id", integerSchema("Target entity id")},
                {"target_name", stringSchema("Target entity name, used only when target_id is omitted")},
                {"insertion", stringSchema("before, after, child_first, or child_last")}
            }, {"insertion"}),
            false
        },
        {
            "select_entities",
            "Replace the editor selection for a scene. Supports one entity, multiple entity_ids/entity_names, or clear.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Single entity id to select")},
                {"entity_name", stringSchema("Single entity name to select, used only when entity_id is omitted")},
                {"entity_ids", {
                    {"type", "array"},
                    {"description", "Multiple entity ids to select"},
                    {"items", {{"type", "integer"}}}
                }},
                {"entity_names", {
                    {"type", "array"},
                    {"description", "Multiple entity names to select"},
                    {"items", {{"type", "string"}}}
                }},
                {"clear", boolSchema("When true, clear selection")}
            }),
            false
        },
        {
            "add_component",
            "Add a component to an entity through AddComponentCmd.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"component", stringSchema("Component name")}
            }, {"component"}),
            false
        },
        {
            "remove_component",
            "Remove a component from an entity through RemoveComponentCmd.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"component", stringSchema("Component name")}
            }, {"component"}),
            false
        },
        {
            "add_body3d_shape",
            "Add a Body3DComponent to an existing 3D entity when needed, then append one collision shape to that SAME entity's body. Use this for a model/mesh that needs physics; do not create a separate body entity. Existing body shapes are preserved. Sizes default to the mesh's measured bounds, which is the reliable way to match the visible geometry; only pass explicit sizes for axes that must deliberately differ from the mesh. The result reports fitted_to_mesh so you can tell a measured shape from one left at its defaults. convex_hull is exact and cheap for boxy level geometry.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Model or mesh entity id that receives the Body3DComponent")},
                {"entity_name", stringSchema("Model or mesh entity name, used only when entity_id is omitted")},
                {"body_type", stringSchema("Optional body type: static, kinematic, or dynamic. A new body defaults to static when omitted")},
                {"motion_quality", stringSchema("Optional motion quality: discrete or linear_cast")},
                {"shape_type", stringSchema("Collision shape: box, sphere, capsule, tapered_capsule, cylinder, convex_hull, or mesh. convex_hull and mesh take their geometry from the entity's mesh and ignore all size arguments; convex_hull is exact and cheap for boxy level geometry, mesh suits static concave geometry")},
                {"fit_to_mesh", boolSchema("Size the shape from the entity mesh's local bounds and centre it on them. Defaults to true; any explicit size argument overrides the fitted value for that axis. Set false to keep the shape defaults")},
                {"position", vector3Schema("Optional shape local position in entity-local units. Overrides the centre chosen by fit_to_mesh")},
                {"rotation", quaternionSchema("Optional shape local rotation")},
                {"width", numberSchema("Box width in mesh-local units, i.e. before the entity's transform scale, which the engine applies itself; defaults to 1")},
                {"height", numberSchema("Box height in mesh-local units; defaults to 1")},
                {"depth", numberSchema("Box depth in mesh-local units; defaults to 1")},
                {"radius", numberSchema("Sphere, capsule, or cylinder radius in mesh-local units; defaults to 1")},
                {"half_height", numberSchema("Capsule or cylinder half-height in mesh-local units; defaults to 0.5. A capsule is 2*(half_height+radius) tall")},
                {"top_radius", numberSchema("Tapered capsule top radius in mesh-local units; defaults to 0.5")},
                {"bottom_radius", numberSchema("Tapered capsule bottom radius in mesh-local units; defaults to 0.5")},
                {"density", numberSchema("Shape density in kg/m3 for dynamic bodies; defaults to 1000 (water). Mass is volume * density, so the default makes a radius-1 sphere weigh about 4189 kg and a 1x1x1 box 1000 kg. Pick a density that yields the mass the behaviour needs, so scripted forces stay plausible")}
            }, {"shape_type"}),
            false
        },
        {
            "add_body2d_shape",
            "Add a Body2DComponent to an existing 2D entity when needed, then append one collision shape to that SAME entity's body. Use this for a sprite, tilemap, or 2D mesh that needs physics; do not create a separate body entity. Existing body shapes are preserved.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Sprite, tilemap, or 2D mesh entity id that receives the Body2DComponent")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"body_type", stringSchema("Optional body type: static, kinematic, or dynamic. A new body defaults to static when omitted")},
                {"shape_type", stringSchema("Collision shape: box, circle, capsule, segment, polygon, or chain")},
                {"center", vector2Schema("Optional local center for a box or circle; defaults to 0,0")},
                {"width", numberSchema("Box width; required for box")},
                {"height", numberSchema("Box height; required for box")},
                {"radius", numberSchema("Circle or capsule radius; required for circle and capsule")},
                {"point_a", vector2Schema("First local point for capsule or segment")},
                {"point_b", vector2Schema("Second local point for capsule or segment")},
                {"points", {
                    {"type", "array"},
                    {"description", "Local points for polygon (at least 3) or chain (at least 4). A chain collides on one side only: counter-clockwise points collide outside, clockwise inside (hollow containers)"},
                    {"items", vector2Schema("Point")}
                }},
                {"loop", boolSchema("Whether a chain closes back to its first point; defaults false. An open chain doesn't collide on its first and last edges")},
                {"density", numberSchema("Shape density; defaults to 1, NOT the 1000 that 3D bodies default to. 2D mass is area * density, with sizes given in points and scaled to metres by pointsToMeterScale2D (64 by default), so a 100x100 box at the default density weighs about 2.4 kg")},
                {"friction", numberSchema("Shape friction; defaults to 0.6")},
                {"restitution", numberSchema("Shape restitution/bounce, 0-1; defaults to 0")}
            }, {"shape_type"}),
            false
        },
        {
            "set_component_property",
            "Set one supported component property through PropertyCmd. Use the typed value field matching the property type.",
            objectSchemaFromProperties(propertyValueFields({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"component", stringSchema("Component name")},
                {"property", stringSchema("Property path from inspect_component, e.g. position or submeshes[0].material.baseColorTexture")}
            }), {"component", "property"}),
            false
        },
        {
            "set_texture_settings",
            "Set sampler settings and/or SVG scale of a Texture component property; only the provided fields change, the texture source is kept. Filters: nearest, linear, nearest_mipmap_nearest, nearest_mipmap_linear, linear_mipmap_nearest, linear_mipmap_linear (mag_filter accepts only nearest or linear). Wraps: repeat, mirrored_repeat, clamp_to_edge, clamp_to_border.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"component", stringSchema("Component name")},
                {"property", stringSchema("Texture property path from inspect_component, e.g. texture or submeshes[0].material.baseColorTexture")},
                {"min_filter", stringSchema("Minification filter")},
                {"mag_filter", stringSchema("Magnification filter: nearest or linear")},
                {"wrap_u", stringSchema("Wrap mode along U")},
                {"wrap_v", stringSchema("Wrap mode along V")},
                {"svg_scale", numberSchema("Rasterization scale for .svg sources (e.g. 2.0 = twice the intrinsic size)")}
            }, {"component", "property"}),
            false
        },
        {
            "add_project_scene",
            "Add an existing .scene file found in the project folder to the project scene list, closed. A .scene file that is not listed is not built, exported, opened, or usable as a child scene. Scenes made with create_scene are already listed; use delete_scene to take one off the list.",
            objectSchema({
                {"scene_path", stringSchema("Safe project-relative .scene path")}
            }, {"scene_path"}),
            false
        },
        {
            "create_scene",
            "Create a new 2D, 3D, or UI scene in the project and select it. The previously selected scene closes, so reopen it with open_scene before changing it again.",
            objectSchema({
                {"name", stringSchema("Scene name")},
                {"type", stringSchema("Scene type: 3d, 2d, or ui")}
            }, {"name", "type"}),
            false
        },
        {
            "rename_scene",
            "Rename a scene through SceneNameCmd.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"new_name", stringSchema("New scene name")}
            }, {"new_name"}),
            false
        },
        {
            "save_scene",
            "Save one scene. Omit scene_id to save the selected scene. A scene that was never saved needs path, or the editor asks the user with a save dialog instead.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"path", stringSchema("Project-relative .scene path for a scene that has no file yet, e.g. scenes/Level1.scene")}
            }),
            false
        },
        {
            "save_all_scenes",
            "Save all project scenes.",
            objectSchema({}),
            false
        },
        {
            "set_start_scene",
            "Set the project startup scene.",
            objectSchema({
                {"scene_id", integerSchema("Scene id to use as startup scene")}
            }, {"scene_id"}),
            false
        },
        {
            "set_scene_property",
            "Set a supported scene-level property such as background_color, global_illumination_color, ssao_enabled, ssr_enabled, ambient_light_2d_color/ambient_light_2d_intensity (2D light ambient), shadows_quality (3D) and shadows_2d_quality (2D) as int_value 0=none/1=low/2=medium/3=high. Physics gravity in m/s^2: physics_gravity_2d (vector2_value, Box2D world) and physics_gravity_3d (vector3_value, Jolt world), default (0, -9.81). Fixed game resolution (main scene renders at that size and is upscaled to the view): fixed_resolution_enabled (bool_value), fixed_resolution_width/fixed_resolution_height (int_value, pixels), fixed_resolution_filter (int_value 0=nearest, 1=linear). Also sets a scene's default custom shader per type via default_mesh_shader/default_ui_shader/default_sky_shader/default_points_shader/default_lines_shader/default_water_shader as string_value (project-relative fork base path, empty to reset to built-in) - used by every component of that type whose own customShader is empty (priority: component customShader > scene default > built-in). Use fork_shader with shader_type (no entity) to create the fork first.",
            objectSchemaFromProperties(propertyValueFields({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"property", stringSchema("Scene property name")}
            }), {"property"}),
            false
        },
        {
            "control_play_mode",
            "Start, pause, resume, or stop play mode for a scene.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"action", stringSchema("start, pause, resume, or stop")}
            }, {"action"}),
            false
        },
        {
            "add_child_scene",
            "Add a child scene reference through AddChildSceneCmd. The child draws on top of the parent with its own camera, which is where a HUD or an overlay menu belongs.",
            objectSchema({
                {"scene_id", integerSchema("Parent scene id. Omit to use selected scene")},
                {"child_scene_id", integerSchema("Child scene id")},
                {"start_active", boolSchema("Whether the child scene starts active. Defaults true")}
            }, {"child_scene_id"}),
            false
        },
        {
            "remove_child_scene",
            "Remove a child scene reference through RemoveChildSceneCmd.",
            objectSchema({
                {"scene_id", integerSchema("Parent scene id. Omit to use selected scene")},
                {"child_scene_id", integerSchema("Child scene id")}
            }, {"child_scene_id"}),
            false
        },
        {
            "set_child_scene_start_active",
            "Set a child scene's Start active flag through SetChildSceneStartActiveCmd.",
            objectSchema({
                {"scene_id", integerSchema("Parent scene id. Omit to use selected scene")},
                {"child_scene_id", integerSchema("Child scene id")},
                {"start_active", boolSchema("New Start active value")}
            }, {"child_scene_id", "start_active"}),
            false
        },
        {
            "load_child_scene_inline",
            "Load or unload a child scene inline for editing context.",
            objectSchema({
                {"child_scene_id", integerSchema("Child scene id")},
                {"load", boolSchema("true to load inline, false to unload")}
            }, {"child_scene_id", "load"}),
            false
        },
        {
            "create_terrain_heightmap",
            "Create or replace an undoable 16-bit heightmap for a terrain entity. Use mode 'middle' for the editor default flat midpoint map, 'flat' for a custom constant height, 'random_noise' for random heights, or 'fractal_noise' for smoother terrain.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Terrain entity id")},
                {"entity_name", stringSchema("Terrain entity name, used only when entity_id is omitted")},
                {"resolution", integerSchema("Square heightmap resolution, 2-2048. Defaults to 512")},
                {"mode", stringSchema("flat, middle, random_noise, or fractal_noise. Defaults to middle")},
                {"seed", integerSchema("Optional deterministic seed for noise modes")},
                {"base_height", numberSchema("Normalized base height 0-1. Defaults to 0.5 for middle/flat")},
                {"amplitude", numberSchema("Noise amplitude 0-1. Defaults to 0.35")},
                {"frequency", numberSchema("Noise frequency. Defaults to 4.0")},
                {"octaves", integerSchema("Fractal octave count, 1-8. Defaults to 4")}
            }),
            false
        },
        {
            "import_project_model",
            "Import an existing project-relative glTF/GLB/OBJ/FBX model into a 3D scene using ModelLoadCmd. An FBX is converted to glTF behind the scenes and still referenced by its .fbx path.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"model_path", stringSchema("Project-relative model path inside the assets directory (get_project_summary reports it as assets_dir), e.g. assets/models/dog.glb")},
                {"entity_name", stringSchema("Name for the created model entity")},
                {"position", vector3Schema("Optional drop position")}
            }, {"model_path"}),
            false
        },
        {
            "reload_project_model",
            "Reload a model entity after its file changed on disk, through ModelLoadCmd. Mesh children follow the new file's nodes and keep their settings by node name.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Model entity id")},
                {"entity_name", stringSchema("Model entity name, used only when entity_id is omitted")}
            }),
            false
        },
        {
            "create_folder",
            "Create a folder inside the project through CreateDirCmd.",
            objectSchema({
                {"parent_path", stringSchema("Safe project-relative parent directory. Empty means project root")},
                {"name", stringSchema("New folder name")}
            }, {"name"}),
            false
        },
        {
            "rename_resource",
            "Rename or move one project resource through RenameFileCmd, updating project references where supported.",
            objectSchema({
                {"path", stringSchema("Safe project-relative file or directory path")},
                {"new_name", stringSchema("New file or directory name, not a full path")}
            }, {"path", "new_name"}),
            false
        },
        {
            "delete_resource",
            "Delete one project resource through DeleteFileCmd, moving it to the project trash where supported.",
            objectSchema({
                {"path", stringSchema("Safe project-relative file or directory path")}
            }, {"path"}),
            false
        },
        {
            "import_resource_file",
            "Copy an existing local file into the project through CopyFileCmd. No shell is used.",
            objectSchema({
                {"source_path", stringSchema("Absolute local file path to import")},
                {"target_dir", stringSchema("Safe project-relative destination directory, or . for the project root")}
            }, {"source_path", "target_dir"}),
            false
        },
        {
            "link_material",
            "Link a .material file to a mesh submesh through LinkMaterialCmd.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Mesh entity id")},
                {"entity_name", stringSchema("Mesh entity name, used only when entity_id is omitted")},
                {"material_path", stringSchema("Safe project-relative .material path")},
                {"submesh_index", integerSchema("Submesh index. Defaults to 0")}
            }, {"material_path"}),
            false
        },
        {
            "unlink_material",
            "Unlink a mesh submesh from a shared material file through UnlinkMaterialCmd.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Mesh entity id")},
                {"entity_name", stringSchema("Mesh entity name, used only when entity_id is omitted")},
                {"submesh_index", integerSchema("Submesh index. Defaults to 0")}
            }),
            false
        },
        {
            "create_material_file",
            "Create a .material file from a mesh submesh and link it back to that submesh.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Mesh entity id")},
                {"entity_name", stringSchema("Mesh entity name, used only when entity_id is omitted")},
                {"submesh_index", integerSchema("Submesh index. Defaults to 0")},
                {"target_dir", stringSchema("Safe project-relative directory for the material file, or . for the project root")}
            }, {"target_dir"}),
            false
        },
        {
            "set_terrain_textures",
            "Assign terrain height/blend maps and painted layer maps through undoable property commands. Every path must be inside the assets directory (get_project_summary reports it as assets_dir). A layer blends only its color until layer_pbr turns it into a PBR layer, which brings its own normal, roughness, metallic and occlusion.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Terrain entity id")},
                {"entity_name", stringSchema("Terrain entity name, used only when entity_id is omitted")},
                {"heightmap_path", stringSchema("Optional project-relative heightmap texture path")},
                {"blendmap_path", stringSchema("Optional project-relative blendmap texture path")},
                {"blendmap_index", integerSchema("Blend map slot blendmap_path fills, 0-2: slot 0 weights layers 0-2, slot 1 layers 3-5, slot 2 layers 6-8. Defaults to 0")},
                {"detail_red_path", stringSchema("Optional project-relative color texture path for layer 1")},
                {"detail_green_path", stringSchema("Optional project-relative color texture path for layer 2")},
                {"detail_blue_path", stringSchema("Optional project-relative color texture path for layer 3")},
                {"layer_index", integerSchema("Painted layer to edit, 0-8. Required by the layer_* fields")},
                {"layer_pbr", boolSchema("True gives the layer its own surface maps")},
                {"layer_color_path", stringSchema("Optional project-relative layer base color texture path")},
                {"layer_normal_path", stringSchema("Optional project-relative layer normal map path")},
                {"layer_roughness_path", stringSchema("Optional project-relative layer roughness map path, read from green")},
                {"layer_metallic_path", stringSchema("Optional project-relative layer metallic map path, read from blue")},
                {"layer_occlusion_path", stringSchema("Optional project-relative layer occlusion map path, read from red")},
                {"layer_height_path", stringSchema("Optional project-relative layer height map path, used only for blending")},
                {"layer_uv_scale", vector2Schema("Optional layer tiling, multiplying the terrain's detail tiling")},
                {"layer_normal_strength", numberSchema("Optional layer normal map strength")},
                {"layer_roughness_factor", numberSchema("Optional layer roughness factor")}
            }),
            false
        },
        {
            "set_editor_camera",
            "Move the editor's Scene view camera of a 3D scene: place it at position and aim it at target, to frame what you want to inspect. It changes only the editor view, never the scene or its cameras.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"position", vector3Schema("Camera position in world space")},
                {"target", vector3Schema("World point the camera looks at")}
            }, {"position", "target"}),
            true
        },
        {
            "set_terrain_foliage_layer",
            "Add, change or remove one foliage layer of a terrain as one undoable step. A foliage layer scatters a single-mesh model (the geometry must sit on the file's root node) where its density map is painted; instances are resolved at runtime around the camera. Omit layer_index to append a new layer; give it to change that layer, or with remove to delete it. Fields left out keep their value (or the default on a new layer). Paths must be inside the assets directory.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Terrain entity id")},
                {"entity_name", stringSchema("Terrain entity name, used only when entity_id is omitted")},
                {"layer_index", integerSchema("Foliage layer to change or remove. Omit to append a new layer")},
                {"remove", boolSchema("True removes the layer at layer_index")},
                {"mesh_path", stringSchema("Project-relative glTF/GLB/OBJ model to scatter")},
                {"density_map_path", stringSchema("Project-relative grayscale density map, stretched over the whole terrain")},
                {"density", numberSchema("Instances per square world unit where the density map is white")},
                {"min_scale", numberSchema("Smallest random uniform scale")},
                {"max_scale", numberSchema("Largest random uniform scale")},
                {"rotation_jitter", numberSchema("Share of a full turn of random yaw, 0-1")},
                {"align_to_normal", numberSchema("0 stands instances upright, 1 lays them along the surface")},
                {"min_slope", numberSchema("Lowest ground slope in degrees")},
                {"max_slope", numberSchema("Steepest ground slope in degrees")},
                {"min_height", numberSchema("Lowest ground height, normalized against the terrain maxHeight")},
                {"max_height", numberSchema("Highest ground height, normalized against the terrain maxHeight")},
                {"draw_distance", numberSchema("Distance from the camera where instances stop drawing")},
                {"min_draw_distance", numberSchema("Distance from the camera inside which instances are skipped (0 = none). A copy of a layer with a simpler mesh, min_draw_distance near the original's draw_distance and a longer draw_distance becomes its far level of detail")},
                {"seed", integerSchema("Random seed of the scatter")},
                {"cast_shadows", boolSchema("Whether the instances cast shadows")}
            }),
            false
        },
        {
            "create_terrain_blendmap",
            "Create an undoable RGB terrain blendmap texture with constant channel values.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Terrain entity id")},
                {"entity_name", stringSchema("Terrain entity name, used only when entity_id is omitted")},
                {"resolution", integerSchema("Square blendmap resolution, 2-2048. Defaults to terrain heightmap setting")},
                {"red", numberSchema("Red channel 0-1")},
                {"green", numberSchema("Green channel 0-1")},
                {"blue", numberSchema("Blue channel 0-1")}
            }),
            false
        },
        {
            "add_terrain_collision",
            "Add Body3DComponent to a terrain entity and configure a height-field shape when supported.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Terrain entity id")},
                {"entity_name", stringSchema("Terrain entity name, used only when entity_id is omitted")}
            }),
            false
        },
        {
            "check_cpp_build_tools",
            "Check whether this machine has what C++ scripts need to build: CMake and a C++ compiler this editor can load, from the choice in Editor Settings > Desktop. It only finds the tools; the first play after writing a C++ script proves they compile.",
            objectSchema({}),
            true
        },
        {
            "create_script",
            "Create a Lua or C++ script file from a minimal template, attach it to an entity, and update ScriptComponent. Keep the language a project's scripts already use; in one with none yet, prefer C++ once check_cpp_build_tools finds the tools. Lua scripts are returned tables with init(), self.scene, and self.entity. C++ scripts use flat headers (\"Mesh.h\", \"ScriptBase.h\"); for mesh/cube behavior prefer cpp_subclass extending Mesh and unregister engine events in destructors.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"type", stringSchema("lua, cpp_subclass, or cpp_script_class")},
                {"class_name", stringSchema("Class/module name")},
                {"target_dir", stringSchema("Safe project-relative target directory. Defaults to scripts")}
            }, {"type", "class_name"}),
            false
        },
        {
            "attach_script",
            "Attach an existing Lua or C++ script to an entity ScriptComponent as a new entry. Use it only for a script the entity does not have yet; to rename or repoint a script it already has, use update_script_entry.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"type", stringSchema("lua or cpp")},
                {"class_name", stringSchema("Class/module name")},
                {"path", stringSchema("Safe project-relative .lua or .cpp path")},
                {"header_path", stringSchema("Safe project-relative header path for C++ scripts")}
            }, {"type", "class_name", "path"}),
            false
        },
        {
            "update_script_entry",
            "Rename the class of a script an entity already has, or repoint it at other files. Use this after renaming a script file or its class; it edits the attachment in place instead of adding a second one. Entries come from inspect_component on ScriptComponent.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"index", integerSchema("Script entry index")},
                {"class_name", stringSchema("Current class name of the entry, used when index is omitted")},
                {"path", stringSchema("Current source or header path of the entry, used when index and class_name are omitted")},
                {"new_class_name", stringSchema("New class/module name")},
                {"new_path", stringSchema("New safe project-relative .lua or .cpp path")},
                {"new_header_path", stringSchema("New safe project-relative header path, C++ entries only")}
            }),
            false
        },
        {
            "remove_script_entry",
            "Detach one script from an entity ScriptComponent. The script files are kept; delete them with delete_resource when they are no longer used.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"index", integerSchema("Script entry index")},
                {"class_name", stringSchema("Class name of the entry, used when index is omitted")},
                {"path", stringSchema("Source or header path of the entry, used when index and class_name are omitted")}
            }),
            false
        },
        {
            "update_script_file",
            "Replace an existing project script file with complete content and refresh attached ScriptComponent properties. Lua content must use Doriax table modules, not Dori.Script or editor property paths. For Mesh/Shape color use Shape(self.scene, self.entity) and setColor(1,0,0,1) or setColor(Vector4(1,0,0,1)). Choose update timing correctly in both Lua and C++: onUpdate is for variable-frame non-physics logic, while continuous forces and torques must use onFixedUpdate and must not be multiplied by delta time. Size forces from the body's real mass, not from a guessed magnitude: 3D mass is shape volume * density (default density 1000 kg/m3), while 2D mass is shape area * density (default density 1), so never carry the 3D default over to a 2D body. C++ content must use flat quoted headers (\"Mesh.h\", not <core/Mesh.h>), cpp_subclass + setColor for mesh entities, REGISTER_ENGINE_EVENT with the appropriate update event, and matching destructor cleanup with UNREGISTER_ENGINE_EVENT.",
            objectSchema({
                {"path", stringSchema("Existing safe project-relative script path. Allowed extensions: .lua, .cpp, .h, .hpp")},
                {"content", stringSchema("Complete replacement file contents")},
                {"reason", stringSchema("Short reason shown in the approval preview")}
            }, {"path", "content"}),
            false
        },
        {
            "create_source_file",
            "Create a NEW C++ source or header that belongs to no entity: a helper class, a shared utility, or any library code scripts include. It never touches a ScriptComponent, so use create_script instead when the class is a script an entity runs. A source only reaches the build when it lives under a project script directory (get_project_summary reports script_dirs) or when an attached script includes it; the result says which case applies. Change the file afterwards with update_script_file.",
            objectSchema({
                {"path", stringSchema("New safe project-relative file path. Allowed extensions: .h, .hpp, .cpp")},
                {"content", stringSchema("Complete file contents")},
                {"reason", stringSchema("Short reason shown in the approval preview")}
            }, {"path", "content"}),
            false
        },
        {
            "set_project_script_dirs",
            "Replace the project C++ script directories. Each one becomes an include directory, and every source under it is compiled without a script component referencing it. Read the current list from get_project_summary (script_dirs) and pass the complete new list: entries it reported can be passed back verbatim, omitting one removes that root, and an empty list clears them all. A root you ADD must be a project-relative directory that already exists (create it with create_folder first); the project root itself and directories outside the project are added by the user in Project Settings.",
            objectSchema({
                {"directories", {
                    {"type", "array"},
                    {"description", "Complete list of safe project-relative directories to use as C++ script roots"},
                    {"items", {{"type", "string"}}}
                }}
            }, {"directories"}),
            false
        },
        {
            "set_project_asset_dirs",
            "Change the project's assets and Lua directories (Project Settings > Directories); a field left out keeps its value. Asset paths in scenes are relative to the assets directory and require() names to the Lua directory, and an export ships what is inside them. The editor moves every file the project references into the new directory, keeping its layout, and rewrites the references in all scenes, bundles and materials, saving them. Files nothing references, such as ones scripts load by path or Lua modules loaded with require, stay where they are. Read the current values from get_project_summary (assets_dir, lua_dir).",
            objectSchema({
                {"assets_dir", stringSchema("Project-relative assets directory, e.g. assets, or . for the project root. Created when missing")},
                {"lua_dir", stringSchema("Project-relative Lua directory, e.g. scripts, or . for the project root. Created when missing")}
            }),
            false
        },
        {
            "set_project_cxx_standard",
            "Set the project's C++ language standard (" + cxxStandardList() + "). The next play or export builds with it, and an export compiles the engine with it too. Read the current value from get_project_summary (cxx_standard). Do not edit CMAKE_CXX_STANDARD in the generated CMakeLists.txt.",
            objectSchema({
                // Gemini rejects integer enums.
                {"cxx_standard", integerSchema("C++ standard year: " + cxxStandardList())}
            }, {"cxx_standard"}),
            false
        },
        {
            "update_project_build_file",
            "Write ProjectBuild.cmake at the project root: the optional CMake hook carrying extra include directories, libraries, and compile definitions. The generated CMakeLists.txt is rewritten on every build, so it is never the place for custom build settings and the user must never be asked to edit it. Attach settings to ${DORIAX_TARGET} instead of a literal target name (it differs between the editor and exported builds) and use ${DORIAX_SCRIPTS_DIR} as the root of script paths. This replaces the whole file, so read the current one with read_resource_file first and keep the settings it already has.",
            objectSchema({
                {"content", stringSchema("Complete replacement contents of ProjectBuild.cmake")},
                {"reason", stringSchema("Short reason shown in the approval preview")}
            }, {"content"}),
            false
        },
        {
            "create_bundle_from_entity",
            "Create a .bundle from an entity hierarchy through CreateEntityBundleCmd. The hierarchy becomes the bundle's first instance; place more copies, in this scene or others, with import_bundle_instance.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Root entity id")},
                {"entity_name", stringSchema("Root entity name, used only when entity_id is omitted")},
                {"bundle_path", stringSchema("Safe project-relative .bundle output path")}
            }, {"bundle_path"}),
            false
        },
        {
            "import_bundle_instance",
            "Import a .bundle file as a scene instance through ImportEntityBundleCmd. It returns instance_root_id, the new entity holding the bundle's members: move, rotate or rename the instance through it.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"bundle_path", stringSchema("Safe project-relative .bundle path")},
                {"parent_id", integerSchema("Optional parent entity id")}
            }, {"bundle_path"}),
            false
        },
        {
            "add_entity_to_bundle",
            "Insert an entity into an existing bundle through AddEntityToBundleCmd.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"bundle_root_id", integerSchema("Bundle root/parent entity id")}
            }, {"bundle_root_id"}),
            false
        },
        {
            "remove_entity_from_bundle",
            "Remove a bundle member entity from its bundle through RemoveEntityFromBundleCmd.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")}
            }),
            false
        },
        {
            "make_bundle_component_unique",
            "Convert a shared bundle component into a local override through ComponentToBundleLocalCmd.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"component", stringSchema("Component name")}
            }, {"component"}),
            false
        },
        {
            "revert_bundle_component",
            "Revert a local bundle component override back to the shared bundle through ComponentToBundleSharedCmd.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"component", stringSchema("Component name")}
            }, {"component"}),
            false
        },
        {
            "set_standalone_bundle",
            "Add or remove a .bundle in the project standalone list. A bundle no scene instantiates is only built and registered while it is listed, so scripts can spawn it by name with BundleManager. Bundles a scene instantiates are always built and are not listed.",
            objectSchema({
                {"bundle_path", stringSchema("Safe project-relative .bundle path")},
                {"standalone", boolSchema("true to list the bundle, false to remove it. Default true")}
            }, {"bundle_path"}),
            false
        },
        {
            "export_project",
            "Run the editor export pipeline for the selected graphic backends.",
            objectSchema({
                {"target_dir", stringSchema("Output directory. Must be outside or separate from project source")},
                {"backends", stringSchema("Comma-separated graphic backends the shaders are compiled for: opengl, opengles, d3d11, metal-macos, metal-ios, vulkan, all")},
                {"start_scene_id", integerSchema("Optional startup scene id override")}
            }, {"target_dir"}),
            false
        },
        {
            "set_export_shaders",
            "Edit the shader variants every export adds to or leaves out of what the saved scenes use (project.yaml export.shaderAdditions/shaderExclusions, the overrides the Export window lists), and return both lists. An export ships only the shaders of each scene's saved state, so a variant that appears only at runtime, such as a script toggling Scene.ssaoEnabled or ssrEnabled, must be added here or its meshes fail to load in the exported game. A spec is type or type:properties with a decimal or 0x property mask, as scene files and the shaders command write them: mesh:4184, ssao, gbuffer:12. A forked shader adds its project-relative base path after @: mesh:4184@shaders/myMesh. A built-in lit mesh variant (property bit 3) ships with its bit 28 twin, so every field applies to both. Call with no arguments to read the lists.",
            objectSchema({
                {"add", {{"type", "array"}, {"description", "Specs to add to the additions (taken out of the exclusions)"}, {"items", {{"type", "string"}}}}},
                {"remove", {{"type", "array"}, {"description", "Specs to take out of the additions"}, {"items", {{"type", "string"}}}}},
                {"exclude", {{"type", "array"}, {"description", "Specs no export should ship (taken out of the additions)"}, {"items", {{"type", "string"}}}}},
                {"include", {{"type", "array"}, {"description", "Specs to take out of the exclusions"}, {"items", {{"type", "string"}}}}},
                {"clear", boolSchema("Empty both lists before applying the other fields")}
            }),
            false
        },
        {
            "generate_shaders",
            "Generate selected shaders using the editor shader builder.",
            objectSchema({
                {"target_dir", stringSchema("Output directory")},
                {"backends", stringSchema("Comma-separated graphic backends to compile for: opengl, opengles, d3d11, metal-macos, metal-ios, vulkan, all")},
                {"format", stringSchema("binary, header, or json")}
            }, {"target_dir"}),
            false
        },
        {
            "fork_shader",
            "Fork a built-in shader into a project-relative directory as one undoable step. Give entity_id/entity_name to fork a single component's shader (points its customShader at the fork); with shader_type depth on a MeshComponent it forks the depth shader instead (points customDepthShader at the fork): shadow maps, plus SSAO/post-process depth while SSR is off (with SSR on they read the built-in G-buffer) - needed when the mesh fork displaces vertices or discards, so shadows follow; with shader_type underwater on a WaterComponent it forks the fade a camera below the surface sees (points customUnderwaterShader at the fork). Omit entity_id/entity_name and give shader_type instead to fork a scene-wide default shader for that type (points the scene's default_<type>_shader at the fork, used by every component of that type without its own customShader - see set_scene_property). Creates <name>.vert and <name>.frag directly in that directory. With fork_includes the fork is placed in its own <directory>/<name>/ folder together with an includes/ copy of the engine .glsl files it uses, so those edits override the engine library only for this fork. This is the correct way to start a custom shader; do not set customShader or a scene default_*_shader to files that do not exist.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id. Omit both this and entity_name, and give shader_type, to fork a scene default instead")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"component", stringSchema("Renderable component name (MeshComponent, UIComponent, PointsComponent, LinesComponent, SkyComponent). Omit to auto-detect the entity's renderable component. Ignored when forking a scene default")},
                {"shader_type", stringSchema("mesh, ui, sky, points, lines, or water. Required when entity_id/entity_name are omitted, to fork a scene-wide default shader for that type; with an entity only depth (fork a mesh's depth shader) or underwater (fork a water's underwater shader) is meaningful")},
                {"directory", stringSchema("Optional safe project-relative destination directory; defaults to shaders")},
                {"fork_includes", boolSchema("Also copy the built-in transitive .glsl includes so they can be edited; this moves the fork into its own <name>/ directory")}
            }),
            false
        },
        {
            "write_shader_file",
            "Write a project-relative custom shader source file (.vert, .frag, or .glsl). Use after fork_shader to edit entry points, private copied includes, or project-root-relative shared includes. Read the file first and keep ALL original texture and sampler declarations (uniform sampler2D/samplerCube/etc. and their #ifdef guards) plus the uniform blocks, #version, #include lines and output variable - the engine binds textures by fixed slot, so removing or renaming a declaration breaks the bindings. The only block you may add is the fork's own u_vs_customParams / u_fs_customParams (u_fs_postParams in a post-process pass), whose members are set with set_shader_uniform. Unmodified #include \"includes/...\" resolves private fork copies first, then project-root overrides, then the engine library.",
            objectSchema({
                {"path", stringSchema("Existing or new safe project-relative path. Allowed extensions: .vert, .frag, .glsl")},
                {"content", stringSchema("Complete replacement file contents")},
                {"reason", stringSchema("Short reason shown in the approval preview")}
            }, {"path", "content"}),
            false
        },
        {
            "inspect_shader_uniforms",
            "List the custom uniforms of a forked shader: the members its u_vs_customParams / u_fs_customParams blocks declare (or u_fs_postParams for a post-process pass), each with its GLSL type, whether the engine writes it (time, resolution), and the stored value. Give entity_id/entity_name (component optional, auto-detected) for a Mesh/UI/Points/Lines/Sky/Water component, or pass_index for a scene post-process pass. The engine reflects the compiled shader, so members appear once the fork has built; a fork that declares no block lists none. Use before set_shader_uniform to learn the member names.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id of a component with a custom shader")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"component", stringSchema("Renderable component name (MeshComponent, UIComponent, PointsComponent, LinesComponent, SkyComponent). Omit to auto-detect")},
                {"pass_index", integerSchema("Post-process pass index (0 = first pass of the scene chain), instead of an entity")}
            }),
            true
        },
        {
            "set_shader_uniform",
            "Set or remove one custom uniform value of a forked shader, by member name, as one undoable step. Values live on the component (shaderUniforms) or on the post-process pass and are uploaded to the u_vs_customParams / u_fs_customParams block (u_fs_postParams for a pass) member of the same name; a value is kept as a Vector4, so give number_value for float/int members, vector2_value/vector3_value/vector4_value for vec2/vec3/vec4 (int members truncate). Give entity_id/entity_name (component optional) for a Mesh/UI/Points/Lines/Sky/Water component, or pass_index for a scene post-process pass. time and resolution are written by the engine and cannot be set. Set remove=true to drop the value (the member reads zero again). Names the shader does not declare are stored but unused; use inspect_shader_uniforms to see the declared members.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id of a component with a custom shader")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"component", stringSchema("Renderable component name (MeshComponent, UIComponent, PointsComponent, LinesComponent, SkyComponent). Omit to auto-detect")},
                {"pass_index", integerSchema("Post-process pass index (0 = first pass of the scene chain), instead of an entity")},
                {"name", stringSchema("Uniform member name as declared in the shader block, e.g. amplitude or tint")},
                {"number_value", numberSchema("Value for a float or int member")},
                {"vector2_value", vector2Schema("Value for a vec2 member")},
                {"vector3_value", vector3Schema("Value for a vec3 member")},
                {"vector4_value", vector4Schema("Value for a vec4 member")},
                {"remove", boolSchema("Remove the stored value instead of setting one")}
            }, {"name"}),
            false
        },
        {
            "search_curated_assets",
            "Search curated model sources. Sketchfab search is metadata-only without user OAuth; Poly Haven API requires a unique User-Agent and may need licensing for commercial API use.",
            objectSchema({
                {"provider", stringSchema("polyhaven or sketchfab")},
                {"query", stringSchema("Search text")},
                {"max_results", integerSchema("Maximum number of assets to return, 1-12")}
            }, {"provider", "query"}),
            true
        },
        {
            "download_curated_asset",
            "Download a reviewed direct model URL into project assets, write attribution metadata, and optionally import it into the scene. A .gltf also downloads the buffers and textures it references. Archives are rejected in v1 unless an extractor is added.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"provider", stringSchema("Asset provider name")},
                {"asset_id", stringSchema("Provider asset id")},
                {"title", stringSchema("Asset title")},
                {"author", stringSchema("Author name")},
                {"license", stringSchema("License name or URL")},
                {"source_url", stringSchema("Provider asset page URL")},
                {"download_url", stringSchema("Direct HTTPS URL for a .glb, .gltf, .obj, or .fbx file")},
                {"slug", stringSchema("Filesystem-safe asset slug")},
                {"entity_name", stringSchema("Optional imported entity name")},
                {"position", vector3Schema("Optional import position")}
            }, {"provider", "asset_id", "title", "source_url", "download_url"}),
            false
        },
        {
            "read_resource_file",
            "Read a text project resource such as scripts, materials, scenes, shaders, or config files.",
            objectSchema({
                {"path", stringSchema("Safe project-relative file path")}
            }, {"path"}),
            true
        },
        {
            "inspect_scene",
            "Read scene metadata, child scenes, main camera, and supported scene properties.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")}
            }),
            true
        },
        {
            "set_main_camera",
            "Set or clear the scene main camera through SetMainCameraCmd.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Camera entity id")},
                {"entity_name", stringSchema("Camera entity name, used only when entity_id is omitted")},
                {"clear", boolSchema("When true, clear the main camera")}
            }),
            false
        },
        {
            "open_scene",
            "Open a .scene file from the project and select it in the editor.",
            objectSchema({
                {"scene_path", stringSchema("Safe project-relative .scene path")},
                {"close_previous", boolSchema("When true, close the previously selected scene tab")}
            }, {"scene_path"}),
            false
        },
        {
            "select_scene",
            "Select an already-open scene tab in the editor.",
            objectSchema({
                {"scene_id", integerSchema("Scene id to select")}
            }, {"scene_id"}),
            false
        },
        {
            "regenerate_mesh_geometry",
            "Replace an entity's mesh with a generated primitive through MeshChangeCmd. Rejected for geometry rebuilt on load: a model file, sprite, tilemap, mesh polygon, or terrain. Every parameter not given falls back to its default rather than keeping the mesh's current value.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Mesh entity id")},
                {"entity_name", stringSchema("Mesh entity name, used only when entity_id is omitted")},
                {"geometry_type", stringSchema("plane, box, sphere, cylinder, capsule, torus, or wall")},
                {"plane_width", numberSchema("Plane width")},
                {"plane_depth", numberSchema("Plane depth")},
                {"plane_tiles", integerSchema("Plane tile count")},
                {"wall_width", numberSchema("Wall width")},
                {"wall_height", numberSchema("Wall height")},
                {"wall_tiles", integerSchema("Wall tile count")},
                {"box_width", numberSchema("Box width")},
                {"box_height", numberSchema("Box height")},
                {"box_depth", numberSchema("Box depth")},
                {"box_tiles", integerSchema("Box tile count")},
                {"sphere_radius", numberSchema("Sphere radius")},
                {"sphere_slices", integerSchema("Sphere slices")},
                {"sphere_stacks", integerSchema("Sphere stacks")},
                {"cylinder_base_radius", numberSchema("Cylinder base radius")},
                {"cylinder_top_radius", numberSchema("Cylinder top radius")},
                {"cylinder_height", numberSchema("Cylinder height")},
                {"cylinder_slices", integerSchema("Cylinder slices")},
                {"cylinder_stacks", integerSchema("Cylinder stacks")},
                {"capsule_base_radius", numberSchema("Capsule base radius")},
                {"capsule_top_radius", numberSchema("Capsule top radius")},
                {"capsule_height", numberSchema("Capsule height")},
                {"capsule_slices", integerSchema("Capsule slices")},
                {"capsule_stacks", integerSchema("Capsule stacks")},
                {"torus_radius", numberSchema("Torus radius")},
                {"torus_ring_radius", numberSchema("Torus ring radius")},
                {"torus_sides", integerSchema("Torus sides")},
                {"torus_rings", integerSchema("Torus rings")}
            }),
            false
        },
        {
            "delete_scene",
            "Remove a saved scene from the project through project command history. Cannot remove the last scene.",
            objectSchema({
                {"scene_id", integerSchema("Scene id to remove")}
            }, {"scene_id"}),
            false
        },
        {
            "save_project",
            "Save the project file and project metadata. A temporary project (get_project_summary path under the system temp folder) is lost unless saved with path, which moves it there.",
            objectSchema({
                {"path", stringSchema("Absolute directory to move a temporary project to; it must be empty or not exist yet")},
                {"name", stringSchema("New project name, used with path")}
            }),
            false
        },
        {
            "set_project_settings",
            "Change project display, loading and web page settings; fields left out keep their value. The canvas is the game's coordinate space in points (2D scenes and UI are laid out on it) and scaling_mode fits it to the window: fitwidth, fitheight, letterbox (the whole canvas stays visible), crop, stretch, or native. The loading scene is shown by SceneManager while loadScene switches scenes. The web fields shape the page of a web export.",
            objectSchema({
                {"canvas_width", integerSchema("Canvas width in points")},
                {"canvas_height", integerSchema("Canvas height in points")},
                {"scaling_mode", stringSchema("fitwidth, fitheight, letterbox, crop, stretch, or native")},
                {"texture_strategy", stringSchema("none, fit, or resize. fit pads and resize scales non-power-of-two textures to a power of two. The editor applies it when the project is reopened")},
                {"window_width", integerSchema("Desktop window width in pixels")},
                {"window_height", integerSchema("Desktop window height in pixels")},
                {"vsync", boolSchema("Synchronize Play mode and supported desktop builds to the display refresh rate")},
                {"loading_scene_id", integerSchema("Scene shown while a scene loads, 0 for none")},
                {"loading_delay", numberSchema("Seconds the loading scene covers the old scene before it is replaced")},
                {"loading_timeout", numberSchema("Seconds without load progress before a load stops waiting")},
                {"async_loading", boolSchema("Load files on worker threads while the loading scene is shown")},
                {"web_resize_canvas", boolSchema("Web export: the canvas fills the browser window")},
                {"web_hide_emscripten_ui", boolSchema("Web export: hide the Emscripten logo, status and console around the canvas")}
            }),
            false
        },
        {
            "create_project",
            "Create a new project in an empty or new directory and open it in place of the current one, which must have no unsaved changes. It starts with an unsaved 3D scene named New Scene, and with its assets and Lua directories at the project root: give them their own folders with set_project_asset_dirs before adding files, so exports ship only the game's files. Later calls wait until it has loaded.",
            objectSchema({
                {"path", stringSchema("Absolute directory for the project; it must be empty or not exist yet")},
                {"name", stringSchema("Project name. Defaults to the directory name")}
            }, {"path"}),
            false,
            true
        },
        {
            "open_project",
            "Open an existing project directory (one holding project.yaml) in place of the current one, which must have no unsaved changes. Later calls wait until it has loaded.",
            objectSchema({
                {"path", stringSchema("Absolute project directory")}
            }, {"path"}),
            false,
            true
        },
        {
            "copy_resource",
            "Copy a project file or directory to another project directory through CopyFileCmd.",
            objectSchema({
                {"source_path", stringSchema("Safe project-relative source file or directory path")},
                {"target_dir", stringSchema("Safe project-relative destination directory, or . for the project root")}
            }, {"source_path", "target_dir"}),
            false
        },
        {
            "update_material_file",
            "Validate and replace an existing .material file with complete YAML content through project command history. Texture paths inside the file are stored relative to the project assets directory, so keep the form read_resource_file returned.",
            objectSchema({
                {"path", stringSchema("Existing safe project-relative .material path")},
                {"content", stringSchema("Complete replacement file contents")},
                {"reason", stringSchema("Short reason shown in the approval preview")}
            }, {"path", "content"}),
            false
        },
        {
            "set_component_properties",
            "Set multiple supported component properties atomically through MultiPropertyCmd.",
            objectSchemaFromProperties(propertyValueFields({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"properties", {
                    {"type", "array"},
                    {"description", "Property changes. Each item needs component, property, and one typed value field."},
                    {"items", objectSchemaFromProperties(propertyValueFields({
                        {"component", stringSchema("Component name")},
                        {"property", stringSchema("Property path")}
                    }), {"component", "property"})}
                }}
            }), {"properties"}),
            false
        },
        {
            "add_tilemap_tile",
            "Add a tile instance to a TilemapComponent using a tile rect index.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Tilemap entity id")},
                {"entity_name", stringSchema("Tilemap entity name, used only when entity_id is omitted")},
                {"rect_index", integerSchema("Tile rect index in tilesRect")},
                {"position", vector2Schema("Optional tile position")},
                {"width", numberSchema("Optional tile width override")},
                {"height", numberSchema("Optional tile height override")}
            }, {"rect_index"}),
            false
        },
        {
            "remove_tilemap_tile",
            "Remove a tile from a TilemapComponent by tile index.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Tilemap entity id")},
                {"entity_name", stringSchema("Tilemap entity name, used only when entity_id is omitted")},
                {"tile_index", integerSchema("Tile index to remove")}
            }, {"tile_index"}),
            false
        },
        {
            "duplicate_tilemap_tile",
            "Duplicate a tile in a TilemapComponent by tile index.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Tilemap entity id")},
                {"entity_name", stringSchema("Tilemap entity name, used only when entity_id is omitted")},
                {"tile_index", integerSchema("Tile index to duplicate")}
            }, {"tile_index"}),
            false
        },
        {
            "add_animation_action",
            "Add an action frame to an AnimationComponent timeline.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Animation entity id")},
                {"entity_name", stringSchema("Animation entity name, used only when entity_id is omitted")},
                {"start_time", numberSchema("Action start time")},
                {"duration", numberSchema("Action duration. 0 or omitted = auto (the action's own duration)")},
                {"track", integerSchema("Timeline track index")},
                {"action_entity_id", integerSchema("Action entity id")},
                {"action_entity_name", stringSchema("Action entity name, used only when action_entity_id is omitted")}
            }),
            false
        },
        {
            "remove_animation_action",
            "Remove an action frame from an AnimationComponent by index.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Animation entity id")},
                {"entity_name", stringSchema("Animation entity name, used only when entity_id is omitted")},
                {"action_index", integerSchema("Action index to remove")}
            }, {"action_index"}),
            false
        },
        {
            "set_keyframe_times",
            "Set or append keyframe times on a KeyframeTracksComponent.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id with KeyframeTracksComponent")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"times", {
                    {"type", "array"},
                    {"description", "Complete replacement keyframe time list"},
                    {"items", {{"type", "number"}}}
                }},
                {"append", boolSchema("When true, append time to existing keyframes")},
                {"time", numberSchema("Time value used with append=true")}
            }),
            false
        },
        {
            "set_keyframe_easing",
            "Set per-segment easing on a KeyframeTracksComponent. Segment i eases the interpolation from key i to key i+1; unset segments are LINEAR.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Entity id with KeyframeTracksComponent")},
                {"entity_name", stringSchema("Entity name, used only when entity_id is omitted")},
                {"segment", integerSchema("Segment index (key i to key i+1), used with ease")},
                {"ease", stringSchema("Ease name (e.g. LINEAR, QUAD_IN_OUT, BOUNCE_OUT, ELASTIC_OUT)")},
                {"easings", {
                    {"type", "array"},
                    {"description", "Complete replacement easing list, one ease name per segment"},
                    {"items", {{"type", "string"}}}
                }}
            }),
            false
        },
        {
            "set_keyframe_track",
            "Replace every key of a keyframe track entity in one undo step: its times, one value per time for its Translate/Rotate/Scale tracks and optional per-segment easings. Values are the target's local position, rotation and scale, or offsets from its starting pose when KeyframeTracksComponent.relative is set.",
            objectSchema({
                {"scene_id", integerSchema("Scene id. Omit to use the selected scene")},
                {"entity_id", integerSchema("Track entity id with KeyframeTracksComponent")},
                {"entity_name", stringSchema("Track entity name, used only when entity_id is omitted")},
                {"times", {
                    {"type", "array"},
                    {"description", "Key times in seconds, ascending"},
                    {"items", {{"type", "number"}}}
                }},
                {"positions", {
                    {"type", "array"},
                    {"description", "TranslateTracks values, one local position per time"},
                    {"items", vector3Schema("Local position")}
                }},
                {"rotations", {
                    {"type", "array"},
                    {"description", "RotateTracks values as quaternions, one per time"},
                    {"items", quaternionSchema("Local rotation")}
                }},
                {"rotations_euler", {
                    {"type", "array"},
                    {"description", "RotateTracks values as Euler angles in degrees, one per time. Keys turn the shortest way, so a full turn needs at least three segments (0, 120, 240, 360)"},
                    {"items", vector3Schema("Local rotation in degrees")}
                }},
                {"scales", {
                    {"type", "array"},
                    {"description", "ScaleTracks values, one local scale per time"},
                    {"items", vector3Schema("Local scale")}
                }},
                {"easings", {
                    {"type", "array"},
                    {"description", "Ease name per segment (key i to key i+1), e.g. SINE_IN_OUT. Missing entries are LINEAR, omit it to keep the current easings"},
                    {"items", {{"type", "string"}}}
                }}
            }, {"times"}),
            false
        },
        {
            "undo_editor",
            "Undo the last editor command in scene or project scope.",
            objectSchema({
                {"scope", stringSchema("scene or project. Defaults to scene")},
                {"scene_id", integerSchema("Scene id for scene scope. Omit to use the selected scene")}
            }),
            false
        },
        {
            "redo_editor",
            "Redo the last undone editor command in scene or project scope.",
            objectSchema({
                {"scope", stringSchema("scene or project. Defaults to scene")},
                {"scene_id", integerSchema("Scene id for scene scope. Omit to use the selected scene")}
            }),
            false
        },
        {
            "search_engine_source",
            "Grep the authoritative Doriax engine source (the real C++ headers and Lua binding files under the editor's engine/ tree) for exact ground-truth signatures, overloads, enum values, macros, and key codes. Prefer this over guessing: search_engine_api is a curated index that uses Lua-style names and omits C++ overloads, so verify real signatures here before writing engine API code. Note key codes are Input.KEY_* in Lua but D_KEY_* macros in C++.",
            objectSchema({
                {"query", stringSchema("Case-insensitive text to find in the engine source, e.g. 'Quaternion(', 'D_KEY_', 'isKeyPressed', 'setPosition'")},
                {"path_filter", stringSchema("Optional substring the file path must contain, e.g. 'Input.h', 'math/', 'binding'")},
                {"max_results", integerSchema("Maximum matching lines to return, 1-80. Defaults to 40")}
            }, {"query"}),
            true
        },
        {
            "read_engine_source",
            "Read a file from the authoritative Doriax engine source tree. Paths are relative to the engine/ root, e.g. core/math/Quaternion.h, core/Input.h, core/object/Object.h, or core/script/binding/CoreClassesLua.cpp. Use after search_engine_source to see full signatures and enums in context.",
            objectSchema({
                {"path", stringSchema("Engine-source-relative file path, e.g. core/math/Quaternion.h")},
                {"start_line", integerSchema("Optional 1-based first line to return")},
                {"end_line", integerSchema("Optional 1-based last line to return")}
            }, {"path"}),
            true
        },
        {
            "read_output_log",
            "Read the editor's recent output log: build/compiler messages, warnings, errors, and runtime script crashes. Use this to VERIFY a script after starting the scene with control_play_mode -- a C++ compile error appears here as a build failure ('C++ script build failed' plus compiler output), and a Lua or C++ runtime error appears as a 'Script crash in scene ...' entry. If you see errors, stop play, fix the script, and verify again.",
            objectSchema({
                {"max_lines", integerSchema("Maximum number of recent log entries to return, 1-600. Defaults to 150")},
                {"errors_only", boolSchema("When true, return only error/warning/build entries")}
            }),
            true
        }
    };
    return definitions;
}

bool hasString(const Json& args, const char* key) {
    return args.contains(key) && args[key].is_string() && !args[key].get<std::string>().empty();
}

// Json::value() throws type_error.302 on a present wrong-typed key; read it as absent.
std::string stringArg(const Json& args, const char* key) {
    if (!args.is_object()) return {};
    const auto it = args.find(key);
    if (it == args.end() || !it->is_string()) return {};
    return it->get<std::string>();
}

// Wrong type, excluding null: models spell "no value" as null, but a number is a mistake.
bool isWrongTypedString(const Json& args, const char* key) {
    if (!args.is_object()) return false;
    const auto it = args.find(key);
    return it != args.end() && !it->is_string() && !it->is_null();
}

bool hasObject(const Json& args, const char* key) {
    return args.contains(key) && args[key].is_object();
}

bool hasAnyValueField(const Json& args) {
    static const char* keys[] = {
        "bool_value", "int_value", "number_value", "string_value",
        "vector2_value", "vector3_value", "vector4_value", "quat_value",
        "texture_path", "font_paths", "entity_value", "bursts_value", "gradient_value"
    };
    for (const char* key : keys) {
        if (args.contains(key)) return true;
    }
    return false;
}

bool hasEntitySelector(const Json& args) {
    return args.contains("entity_id") || hasString(args, "entity_name");
}

bool hasSceneId(const Json& args, const char* key) {
    return args.contains(key) && (args[key].is_number_integer() || args[key].is_number_unsigned());
}

ValidationResult ok() {
    return {true, {}};
}

ValidationResult fail(const std::string& error) {
    return {false, error};
}

} // namespace

std::vector<ToolDefinition> EditorActionRegistry::tools() {
    return cachedTools();
}

// McpServer sends its opening lines plus this as the instructions, which Claude Code cuts at 4096 characters
std::string EditorActionRegistry::essentialGuidance() {
    std::ostringstream text;
    text
        << "Assemble the game in the editor, not in scripts: a scene opened in the editor should already show every entity it uses, in place and with its real properties. Build scenes with the editor tools and leave scripts to drive what is already there, reaching it through entity properties (a C++ DPROPERTY pointer member such as doriax::Text*, or a Lua property of type \"entity\" or a wrapper name such as \"Text\") set with set_component_property on ScriptComponent (scripts[i].<name>, entity_value). Place what only appears later, such as a door or a reward, with Transform visible off on it and on every child placed under it (the saved flag does not cascade), and show it from the script (hiding leaves its physics body active). Create entities from a script only when their number or timing cannot be known in advance, such as projectiles, enemy waves or drops, and then spawn a bundle with BundleManager::createBundle instead of assembling entities and components in code.\n"
        << "Share entities between scenes through bundles: when the same entity or group (the player, an enemy, a pickup, a door, a recurring prop) appears in more than one scene, or more than once in a scene, build it once, turn it into a .bundle with create_bundle_from_entity and place every other copy with import_bundle_instance instead of rebuilding or duplicating it. Instances read their shared components from the .bundle, so one edit updates every instance in every scene; give one instance its own value with make_bundle_component_unique and share it again with revert_bundle_component.\n"
        << "UI entities (text, image, button, ...) are laid out in their scene's camera space: the canvas in a 2D or UI scene, but the world in a 3D scene. A HUD (score, health, timers, on-screen controls) therefore always goes in its own UI scene, in 2D and 3D games alike: make it with create_scene type ui and attach it with add_child_scene to every gameplay scene that shows it, never as UI entities inside a gameplay scene, where they move with its camera and each level needs its own copy. A script in the HUD scene reaches its texts and images through entity properties and reads gameplay values from state the scenes share (static members of a C++ class, or a Lua module both scripts require). An overlay shown on demand, such as a pause menu, is a child scene too, attached with start_active false and shown or hidden with SceneManager::addChildScene/removeChildScene.\n"
        << "Animate with the native animation system, never with animation code in scripts. Motion that can be authored ahead of time (spinning, bobbing, patrols, opening doors, fades, flashes, UI transitions, sprite frame cycles) belongs in animation and action entities, which show in the Properties panel and the Animation window and preview in the editor. A script only starts, stops or restarts them when the game calls for it; it never tweens a transform, color or alpha per frame, or steps sprite frames on a timer, to play such motion. Per-frame script movement is for what follows live input, physics or game state, such as a player controller or a camera following its target.\n";
    return text.str();
}

std::string EditorActionRegistry::guidance() {
    std::ostringstream text;
    text
        << essentialGuidance()
        << "Prefer project-relative paths.\n"
        << "Use read_resource_file before editing existing scripts or materials. Use inspect_scene for scene-level settings.\n"
        << "Create an animation with create_entity animation and add its frames with add_animation_action; each frame points at an action entity: position/rotation/scale/color/alpha_action, or translate/rotate/scale_tracks keyed with set_keyframe_track, whose target is create_entity's parent_id. Frame-by-frame sprite animation is a sprite_animation entity whose parent_id is the sprite: frames[i] are indices into the sprite's SpriteComponent framesRect and framesTime[i] are milliseconds. Each list's entries exist only up to its size (framesSize, framesTimeSize, the sprite's numFramesRect), so set the sizes in one call and the entries in the next. An imported model's own clips play with Model::playAnimation(name, fadeTime) (model:playAnimation in Lua). A single action also runs on its own: TimedActionComponent.loop, TimedActionComponent.pingPong (there and back, so a bob is one position_action), KeyframeTracksComponent.loop, and RotationActionComponent.spin with axis and angle in degrees for full turns. Track values are the target's absolute local values unless KeyframeTracksComponent.relative is set: then they are offsets from the target's pose when the track starts, so one animation works wherever the target is placed and moves anchored UI by its layout offset. ActionComponent.state 0 (Running) starts an action or animation with the scene (Play on start in Properties), ActionComponent.randomStart or startOffset keep copies out of sync, and AnimationComponent.loop repeats an animation. Scripts reach one through a doriax::Animation* property; restart() replays it. A kinematic body moved by an animation carries and pushes other bodies.\n"
        << "2D scenes have a dedicated lighting model: create_entity light_2d adds a Light2DComponent (radius/falloff point light) and occluder_2d adds an Occluder2DComponent (shadow caster). Light2D adds on top of the scene's 2D ambient (set_scene_property ambient_light_2d_color/ambient_light_2d_intensity), so dim the ambient to make 2D lights visible. Occluder2D shape AUTO_QUAD derives its outline from a sibling mesh; POLYGON uses its own points. Enable a Light2D's shadows property to cast from occluders. Shadow edge smoothness is per scene: shadows_quality (3D) and shadows_2d_quality (2D) take int_value 0=none/1=low/2=medium/3=high. These are separate from the 3D Light/global_illumination path.\n"
        << "For requested 3D physics or collisions on an existing visible model/mesh, inspect the entity and use add_body3d_shape. It adds Body3DComponent and its collision Shape3D to that SAME entity atomically; never create a separate body entity unless the user explicitly asks for one. A Body3DComponent without at least one shape does not collide. Use dynamic for player/rigid-body actors and static for floors, terrain, and level geometry; a dynamic character needs a static collision shape under it. Shape dimensions and local position are in mesh-local units and the engine multiplies them by the entity's transform scale on its own, so NEVER pre-divide or pre-multiply a size by the scale to compensate. Never guess a collider size: omit the size arguments so the shape is fitted to the mesh's measured bounds, or read mesh_bounds.local_size from inspect_entity and pass that. If the user says a collider is too big or too small, do not nudge numbers by feel; re-read mesh_bounds and set the shape to the measured local size. Growing a shape's height expands it equally up and down about its centre, so shift the shape position by half the added height to keep the bottom in place. For physics-based jumping, add the body and collision shape before editing the controller, use confirmed Body3D physics APIs, and never describe manually changing an entity transform as physics.\n"
        << "For requested 2D physics or collisions on an existing sprite, tilemap, or 2D mesh, inspect the entity and use add_body2d_shape. It adds Body2DComponent and its Shape2D to that SAME entity atomically; never create a separate body entity unless explicitly requested. A Body2DComponent without a shape does not collide. Use dynamic for actors and static for ground/platforms, and use the requested primitive or polygon/chain points in entity-local 2D units. As in 3D these are mesh-local and the engine multiplies them by the entity scale, so never pre-divide a size to compensate; read mesh_bounds.local_size from inspect_entity instead of guessing. Do not confuse Body2D with Body3D or claim transform-only movement is physics.\n"
        << "Box2D reports begin/end contacts (PhysicsSystem beginContact2D/endContact2D) only for pairs where at least one shape has contactEvents on, and it is off by default: set shapes[i].contactEvents with set_component_property first.\n"
        << "For external assets, use curated sources only and preserve license/author/source attribution.\n"
        << "For scripts and engine API code, the Doriax engine source under the editor's engine/ directory (read it with search_engine_source and read_engine_source) is the ONLY source of truth. Use ONLY classes, methods, properties, enums, macros, and constructor overloads you have confirmed exist in that source; if a symbol is not present there it does not exist in Doriax, so do not use it. Never invent APIs or carry over names, macros, or patterns from other engines or frameworks (e.g. Godot GDCLASS, Unreal GENERATED_BODY/UPROPERTY, Qt Q_OBJECT). search_engine_api is only a quick index into that same source; when a symbol is unfamiliar or you are unsure of its exact spelling or overloads, confirm it in the source before writing it (e.g. key codes are Input.KEY_* in Lua but D_KEY_* macros in C++, and Quaternion's axis-angle constructor takes the angle first: Quaternion(angle, axis)).\n"
        << "A C++ method existing on a class does NOT mean Lua can call it. LuaBridge binds many accessors as properties instead of methods, and calling the accessor from Lua fails at runtime with \"attempt to call a nil value (method 'x')\". search_engine_api marks this: kind 'Method' with a ':' in the detail (Body2D:getMass()) is Lua-callable, while kind 'CppMethod' with '::' in the detail (Object::getPosition(), Body3D::setLinearVelocity()) is C++ only and carries lua_callable=false plus a lua_note naming the property to use (object.position, body.linearVelocity). In Lua, read and assign those properties (self.sphere.position = p, body.linearVelocity = Vector3(0,0,0)); never translate a CppMethod into obj:getX()/obj:setX(). If a class has no bound property for what you need either, check the LuaBridge binding source under engine/core/script/binding/ before writing the call.\n"
        << "A .scene file only belongs to the project while project.yaml lists it; an unlisted one is not built, exported, opened, or usable as a child scene, so a scene file that arrived from outside the editor needs add_project_scene first.\n"
        << "A bundle is only built and registered when a scene instantiates it or when it is listed as standalone, so a bundle that exists only to be spawned from a script with BundleManager needs set_standalone_bundle before createBundle can find it by name. Every createBundle call creates its own instance root and returns it, so the same bundle can be spawned repeatedly; the optional third argument is only the entity the new root is parented to (a name resolved in that scene, an entity id of that scene, or an object carrying its own scene), never the root itself. destroyBundle(scene, root) removes what that spawn created and anything parented under it since, bundles spawned there included, and leaves the parent alone.\n"
        << "When you create a script for requested behavior, write the complete script with update_script_file instead of asking the user to edit it manually.\n"
        << "A ScriptComponent holds a list of script entries; inspect_component on ScriptComponent lists them with index, class name, and file paths. attach_script adds an entry, so use it only for a script the entity does not have yet: to rename or repoint an entry it already has use update_script_entry, and to detach one use remove_script_entry. rename_resource already repoints the attached entries at the renamed file, so after renaming a script only its class name is left to fix with update_script_entry.\n"
        << "Choose a project's script language once. If it already has scripts (search_resources with type script, or ScriptComponents in its scenes), keep their language. In a project with none yet, prefer C++ (cpp_script_class or cpp_subclass) unless the user asks for Lua, but first make sure this machine can build it: call check_cpp_build_tools, and after writing the first C++ script start the scene with control_play_mode and read_output_log. If the tools are missing, or the build fails for reasons outside your code (CMake cannot configure, the compiler or linker does not run, the editor's compatibility check fails), switch to Lua at once. If it fails on your code, fix it, but when it still does not compile after two fixes, switch to Lua as well, even though check_cpp_build_tools found the tools. To switch, detach the C++ scripts with remove_script_entry, delete their files with delete_resource, write the Lua versions, and tell the user why so they can switch back later.\n"
        << "A project can declare C++ script directories: get_project_summary reports them as script_dirs and set_project_script_dirs changes them. Each root is an include directory and every source under it is compiled without any ScriptComponent referencing it, so shared helper, utility, and library classes belong there. Create one with create_source_file, which writes a source or header without attaching it to an entity; create_script is only for a script an entity actually runs. A .cpp that is neither under a root nor included by an attached script is not compiled at all, so add the root instead of assuming it builds. The editor also only runs a C++ build when an enabled C++ script is attached to an entity somewhere in the project: Lua scripts never trigger one, so in a Lua-only project the helper sources are compiled first by an export, and a clean startup log is no proof they built.\n"
        << "An export ships what is inside the project's assets and Lua directories (get_project_summary reports assets_dir and lua_dir). A new project keeps both at the project root (.), where the export also picks up the scenes, project.yaml, notes and source art, and every .json or .txt file for the Lua side. So when a project still has either at the root, give it its own folder with set_project_asset_dirs, such as assets and scripts, before adding files: asset paths are then relative to assets and require() names to scripts. Leave directories the user already chose alone. If the project already has content at the root, ask the user first, since files move: only the files the project references move and get rewritten, so check scripts for asset paths and require() names written against the old root, and move those files too (copy_resource, then delete_resource).\n"
        << "The project C++ language standard is get_project_summary's cxx_standard (" << cxxStandardList() << ") and set_project_cxx_standard changes it. The next play or export builds with it, and an export compiles the engine with it too. Do not edit CMAKE_CXX_STANDARD in the generated CMakeLists.txt.\n"
        << "The project CMakeLists.txt is generated on every build and any edit to it is lost, so never edit it or ask the user to. Custom build settings -- extra include directories, linked libraries, compile definitions -- go in ProjectBuild.cmake at the project root: read the current one with read_resource_file and rewrite it whole with update_project_build_file. Attach settings to ${DORIAX_TARGET} rather than a literal target name (it differs between the editor and exported builds) and use ${DORIAX_SCRIPTS_DIR} as the root of script paths, e.g. target_include_directories(${DORIAX_TARGET} PRIVATE ${DORIAX_SCRIPTS_DIR}/Source/Public) and target_link_libraries(${DORIAX_TARGET} PRIVATE mylib).\n"
        << "After creating or editing ANY script (C++ or Lua), verify it before telling the user it is ready: start the scene with control_play_mode (action=start), then call read_output_log to inspect the editor output. A C++ compile error appears as a build failure with compiler messages; a Lua or C++ runtime error appears as a 'Script crash in scene ...' entry. If the scene is still building, read_output_log again. If you find errors, stop play with control_play_mode (action=stop), fix the script with update_script_file, and verify again -- repeat until the log is clean, then stop play. Do not claim the script works without reading the log.\n"
        << "Doriax Lua scripts are plain returned module tables: local Name = { properties = {} }; function Name:init() ... end; return Name.\n"
        << "Editor-exposed Lua properties go in the properties array as { name = \"speed\", displayName = \"Speed\", type = \"float\", default = 5.0 } and are read at runtime as self.speed.\n"
        << "Choose Lua update events by timing domain. Variable-frame logic such as UI, cameras, and direct non-physics transform movement uses RegisterEngineEvent(self, \"onUpdate\") with Name:onUpdate(); scale continuous non-physics movement by Engine.deltatime. Physics-step logic uses RegisterEngineEvent(self, \"onFixedUpdate\") with Name:onFixedUpdate(). Any continuously applied body force or torque MUST run in onFixedUpdate, never onUpdate: each physics world accumulates force calls until its next fixed step, so calling them from onUpdate makes the effective force depend on render FPS. The two body types spell these differently and are not interchangeable: Body3D has applyForce(force) for a centre force plus applyForce(force, point) and applyTorque(torque), and has NO applyForceToCenter; Body2D has applyForceToCenter(force, wake) plus applyForce(force, point, wake) (no single-argument overload exists) and applyTorque(torque, wake). Prefer onFixedUpdate for velocity control and held-input polling that drives physics too. Do not multiply forces or torques by Engine.deltatime; the physics engine integrates them over the fixed step. Never move a physics body by writing its entity transform from onFixedUpdate (object.position, object:setPosition): the body is synced from the transform before the step and written back after it, so the write is silently discarded. Teleport with body.position / body.rotation (Body3D) or body.position / body.angle (Body2D), which write the simulation directly; entities with no body have no such restriction. One-shot impulses must be emitted once, not every held-input update. No manual Lua unregister is needed.\n"
        << "For component/UI events in Lua use the global RegisterEvent(self, eventObject, \"method\"), e.g. RegisterEvent(self, Button(self.scene, self.entity):getButtonComponent().onPress, \"onPress\"). RegisterEngineEvent/RegisterEvent are Lua globals (not bindings); find them with search_engine_api.\n"
        << "Lua script instances receive self.scene and self.entity; self.entity is a numeric Entity id, not an object. Never call self.entity:... or self.entity....\n"
        << "They do not use Dori.Script, on_start, get_entity, get_component/getComponent, or set_property/setProperty.\n"
        << "Do not put editor property paths such as submeshes[0].material.baseColorFactor inside Lua scripts; use runtime wrappers and APIs from search_engine_api instead.\n"
        << "For Mesh/Shape color at runtime use Shape(self.scene, self.entity) then setColor(1,0,0,1) or setColor(Vector4(1,0,0,1)); do not call methods on self.entity directly.\n"
        << "Doriax C++ scripts use flat quoted headers from .doriax/engine-api (e.g. \"Mesh.h\", \"ScriptBase.h\", \"Engine.h\"). Never use #include <core/...>.\n"
        << "For mesh/cube behavior use cpp_subclass inheriting Mesh (or Shape) and call setColor() on this; do not use ScriptBase as if it had mesh APIs or onInit().\n"
        << "cpp_script_class inherits ScriptBase for general logic. Use REGISTER_ENGINE_EVENT(onUpdate) for variable-frame logic and REGISTER_ENGINE_EVENT(onFixedUpdate) for physics-step logic, with the matching UNREGISTER_ENGINE_EVENT in the destructor. Continuous Body2D/Body3D forces and torques must run in onFixedUpdate (velocity control and physics-driving input polling belong there too); never multiply forces or torques by Engine::getDeltatime(). Never move a physics body by writing its entity transform from onFixedUpdate (Object::setPosition/setRotation): the body is synced from the transform before the step and written back after it, so the write is silently discarded. Teleport with Body3D::setPosition/setRotation or Body2D::setPosition/setAngle; entities with no body have no such restriction.\n"
        << "A dynamic body's mass is never authored directly, it comes from the shape and its density, and the two dimensions do NOT share a formula or a default. In 3D mass is volume * density with default density 1000 (kg/m3, water), so a radius-1 sphere weighs about 4189 kg and a radius-0.5 sphere about 524 kg. In 2D mass is area * density with default density 1, and 2D shape sizes are points scaled to metres by pointsToMeterScale2D (64 by default), so a 100x100 point box weighs about 2.4 kg. Never apply 3D reasoning or the 3D default to a Body2D. Size forces from F = mass * desired acceleration, reading the real mass rather than guessing: a few hundred newtons on a multi-tonne body looks like a broken script. Mass is getMass() in C++, but in Lua it is the property body.mass on Body3D and the method body:getMass() on Body2D, so confirm the spelling in the bindings. If the user reports that movement needs an implausibly large force or feels sluggish, lower the shape density (or shrink the shape) instead of inflating the force, and say what the body actually weighs. Body3D::setMass only writes the live Jolt body and is lost on reload, so persist mass by authoring density.\n"
        << "A Doriax script class needs no class-declaration or reflection macro (no D_OBJECT/GDCLASS/GENERATED_BODY/Q_OBJECT): just inherit the base class and declare a (Scene*, Entity) constructor. Prefer editing the class skeleton create_script generated rather than rewriting it.\n"
        << "For component/UI events (button press, click, scrollbar change) use REGISTER_COMPONENT_EVENT(Component, event, method) or shortcuts REGISTER_UI_EVENT/REGISTER_BUTTON_EVENT/REGISTER_SCROLLBAR_EVENT/REGISTER_PANEL_EVENT in the constructor, paired with UNREGISTER_* in the destructor. These macros and DPROPERTY are C++ script macros (not Lua bindings); find them with search_engine_api.\n"
        << "Cameras orient by target, not by rotation: CameraComponent.useTarget defaults to true and Camera::setTarget re-enables it, so while it is on the view is lookAt(position, target, up) and the entity's rotation is ignored. Call disableTarget() first if you want the rotation to drive the view (forward = rotation * Vector3(0, 0, -1)). For a third-person or orbit camera, place the camera around the target with spherical coordinates -- offset = (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw)); position = target + offset*distance -- then call setTarget(target), so a positive pitch means the camera sits ABOVE the target. Do not derive the position by subtracting a rotated forward vector (target - forward*distance): rotations are right-handed, so a positive pitch tilts the VIEW up and that formula silently puts the camera below the target.\n"
        << "Engine angle arguments are in the engine's default unit, which is DEGREES unless Engine::setUseDegrees(false) was called: Quaternion(angle, axis), Quaternion::fromEulerAngles, Camera::rotateView and similar all pass their angle through Angle::defaultToRad, so never assume they take radians. Convert explicitly with Angle::degToDefault/Angle::radToDefault, and note that std::sin/std::cos always want radians (Angle::defaultToRad).\n"
        << "To print or log from scripts use the Log class: C++ Log::print(\"pos %f %f %f\", p.x, p.y, p.z) with #include \"Log.h\" (Log::warn/Log::error for severity); Lua Log.print(\"pos \" .. tostring(p)). Engine has no log method; never use Engine::log, printf, std::cout, or bare print().\n"
        << "Custom shaders are per-component on Mesh/UI/Points/Lines/Sky/Water via the customShader property. To customize, call fork_shader (it copies the built-in <type>.vert/.frag into a project-relative directory and sets customShader in one undoable step; directory defaults to shaders, and fork_includes optionally copies the engine includes into an editable includes/ folder beside the fork). Never set customShader by hand to files that do not exist; the value is a base path with no extension (vert=base.vert, frag=base.frag) or two explicit paths joined by '|' (\"a.vert|b.frag\") when the entry points have different names. Both a .vert and a .frag are always required. Clear customShader (empty string) to reset to the built-in shader. A Mesh fork drives the color pass only: when it displaces vertices or discards, also fork the depth shader (fork_shader with shader_type depth sets customDepthShader) so shadow maps follow - and SSAO/post-process depth while SSR is off (with SSR on they read the G-buffer, which keeps the built-in shader); it reads the same shaderUniforms.\n"
        << "The forked .vert/.frag are complete, compiling engine shaders with a strict structure: a #version line, #include \"includes/...\" (resolved against the engine library), #ifdef variant guards, uniform blocks, texture and sampler declarations, in/out varyings, and a specific fragment output variable. Editing them with write_shader_file REQUIRES first calling read_resource_file on the forked file, then making minimal targeted changes that PRESERVE the #version, #include lines, uniform blocks, ALL original texture and sampler declarations (uniform sampler2D/samplerCube/textureN/samplerN and their #ifdef guards), in/out declarations, and the existing output variable name. Keep every original texture/sampler declaration even if your new code does not sample it - the engine binds them by fixed slot, so removing or renaming one breaks the texture bindings. Likewise keep the original main() code that READS the in varyings (v_position, v_normal, v_uv1, v_color, etc.): if you delete a varying's usage the GLSL compiler strips that fragment input and the shader fails to cross-compile with a vertex/fragment interface mismatch (\"vertex shader output X does not exist in fragment shader inputs\"). Prefer to keep the original main() body that consumes the varyings and layer your effect on the final color (e.g. compute the original color, then posterize/override it), rather than deleting the existing computations. Only change the shading math (usually near the end of main()). Never rewrite a shader from scratch, drop the #version/#include lines, or invent sampler, texture, output or engine uniform names - that produces compile errors (e.g. missing precision qualifier / wrong #version) or broken bindings. A stylized custom shader (e.g. toon/cel) may legitimately replace the lighting model and stop sampling the shadow or IBL maps; that is allowed - keep their declarations (the engine harmlessly skips the now-unused bindings), but briefly tell the user when your shader changes lighting behavior such as no longer receiving shadows, so they can ask to keep it. If a custom shader fails to compile, the object falls back to the built-in shader and the error appears in the output window; read that error and the forked source, then fix minimally.\n"
        << "The one uniform block a fork MAY add is its own custom block: u_vs_customParams in the .vert and/or u_fs_customParams in the .frag (a post-process pass uses u_fs_postParams), e.g. uniform u_fs_customParams { float time; float amplitude; vec4 tint; } customParams; - keep the existing engine blocks untouched and put your effect parameters in this block with member names of your choice. Members are filled BY NAME from the component's shaderUniforms (or the pass uniforms): set them with set_shader_uniform (undoable, no shader rebuild) and list the declared members with inspect_shader_uniforms; scripts use mesh:setShaderUniform(\"amplitude\", 0.3) / scene:setPostProcessUniform(0, \"amount\", 0.5). The engine writes the members named time (seconds since startup) and resolution (xy = render target size, zw = 1/size) every frame, so an animated effect (waves, fire, scrolling) just declares float time and reads it - never set those two. Keep one scalar kind per block (do not mix int and float members: GL flattens the block after its first member) and avoid matrix and array members (they take no value and stay zero; declare one scalar/vector member per value). An unset member reads zero, so set a value or give the effect a sensible fallback.\n";
    return text.str();
}

bool EditorActionRegistry::hasTool(const std::string& name) {
    for (const auto& tool : cachedTools()) {
        if (tool.name == name) return true;
    }
    return false;
}

bool EditorActionRegistry::isReadOnly(const std::string& name) {
    return getTool(name).readOnly;
}

ToolDefinition EditorActionRegistry::getTool(const std::string& name) {
    for (const auto& tool : cachedTools()) {
        if (tool.name == name) return tool;
    }
    return {};
}

ValidationResult EditorActionRegistry::validate(const std::string& name, const Json& arguments) {
    if (!hasTool(name)) {
        return fail("Unknown editor action: " + name);
    }
    if (!arguments.is_object()) {
        return fail("Action arguments must be a JSON object.");
    }

    if (name == "search_resources") {
        // Missing/empty/null query is a browse request; rejecting it only burned a step.
        if (isWrongTypedString(arguments, "query")) {
            return fail("search_resources query must be a string (omit it to list files).");
        }
        if (isWrongTypedString(arguments, "type")) {
            return fail("search_resources type must be a string.");
        }
        return ok();
    }
    if (name == "search_engine_api") {
        return hasString(arguments, "query") ? ok() : fail("search_engine_api requires query.");
    }
    if (name == "search_engine_source") {
        return hasString(arguments, "query") ? ok() : fail("search_engine_source requires query.");
    }
    if (name == "read_engine_source") {
        return hasString(arguments, "path") ? ok() : fail("read_engine_source requires path.");
    }
    if (name == "read_output_log") {
        return ok();
    }
    if (name == "inspect_entity" || name == "delete_entity" || name == "duplicate_entity" ||
        name == "reload_project_model") {
        return hasEntitySelector(arguments) ? ok() : fail(name + " requires entity_id or entity_name.");
    }
    if (name == "inspect_component" || name == "add_component" || name == "remove_component" ||
        name == "make_bundle_component_unique" || name == "revert_bundle_component") {
        if (!hasEntitySelector(arguments)) return fail(name + " requires entity_id or entity_name.");
        return hasString(arguments, "component") ? ok() : fail(name + " requires component.");
    }
    if (name == "add_body3d_shape") {
        if (!hasEntitySelector(arguments)) return fail("add_body3d_shape requires entity_id or entity_name.");
        return hasString(arguments, "shape_type") ? ok() : fail("add_body3d_shape requires shape_type.");
    }
    if (name == "add_body2d_shape") {
        if (!hasEntitySelector(arguments)) return fail("add_body2d_shape requires entity_id or entity_name.");
        return hasString(arguments, "shape_type") ? ok() : fail("add_body2d_shape requires shape_type.");
    }
    if (name == "rename_entity") {
        if (!hasEntitySelector(arguments)) return fail("rename_entity requires entity_id or entity_name.");
        return hasString(arguments, "new_name") ? ok() : fail("rename_entity requires new_name.");
    }
    if (name == "create_entity") {
        if (!hasString(arguments, "name") || !hasString(arguments, "type")) {
            return fail("create_entity requires name and type.");
        }
        return ok();
    }
    if (name == "set_entity_transform") {
        if (!arguments.contains("entity_id") && !hasString(arguments, "entity_name")) {
            return fail("set_entity_transform requires entity_id or entity_name.");
        }
        if (!hasObject(arguments, "position") && !hasObject(arguments, "rotation") && !hasObject(arguments, "scale")) {
            return fail("set_entity_transform requires position, rotation, or scale.");
        }
        return ok();
    }
    if (name == "reparent_entity") {
        return hasEntitySelector(arguments) ? ok() : fail("reparent_entity requires entity_id or entity_name.");
    }
    if (name == "move_entity_order") {
        if (!hasEntitySelector(arguments)) return fail("move_entity_order requires entity_id or entity_name.");
        if (!arguments.contains("target_id") && !hasString(arguments, "target_name")) return fail("move_entity_order requires target_id or target_name.");
        return hasString(arguments, "insertion") ? ok() : fail("move_entity_order requires insertion.");
    }
    if (name == "set_component_property") {
        if (!hasEntitySelector(arguments)) return fail("set_component_property requires entity_id or entity_name.");
        if (!hasString(arguments, "component") || !hasString(arguments, "property")) {
            return fail("set_component_property requires component and property.");
        }
        return hasAnyValueField(arguments) ? ok() : fail("set_component_property requires one typed value field.");
    }
    if (name == "set_texture_settings") {
        if (!hasEntitySelector(arguments)) return fail("set_texture_settings requires entity_id or entity_name.");
        if (!hasString(arguments, "component") || !hasString(arguments, "property")) {
            return fail("set_texture_settings requires component and property.");
        }
        if (!arguments.contains("min_filter") && !arguments.contains("mag_filter") &&
            !arguments.contains("wrap_u") && !arguments.contains("wrap_v") &&
            !arguments.contains("svg_scale")) {
            return fail("set_texture_settings requires at least one of min_filter, mag_filter, wrap_u, wrap_v, svg_scale.");
        }
        return ok();
    }
    if (name == "add_project_scene") {
        return hasString(arguments, "scene_path") ? ok() : fail("add_project_scene requires scene_path.");
    }
    if (name == "create_scene") {
        return hasString(arguments, "name") && hasString(arguments, "type")
            ? ok() : fail("create_scene requires name and type.");
    }
    if (name == "rename_scene") {
        return hasString(arguments, "new_name") ? ok() : fail("rename_scene requires new_name.");
    }
    if (name == "set_start_scene") {
        return hasSceneId(arguments, "scene_id") ? ok() : fail("set_start_scene requires scene_id.");
    }
    if (name == "set_scene_property") {
        if (!hasString(arguments, "property")) return fail("set_scene_property requires property.");
        return hasAnyValueField(arguments) ? ok() : fail("set_scene_property requires one typed value field.");
    }
    if (name == "control_play_mode") {
        return hasString(arguments, "action") ? ok() : fail("control_play_mode requires action.");
    }
    if (name == "add_child_scene" || name == "remove_child_scene") {
        return hasSceneId(arguments, "child_scene_id") ? ok() : fail(name + " requires child_scene_id.");
    }
    if (name == "set_child_scene_start_active") {
        if (!hasSceneId(arguments, "child_scene_id")) return fail("set_child_scene_start_active requires child_scene_id.");
        return arguments.contains("start_active") && arguments["start_active"].is_boolean()
            ? ok() : fail("set_child_scene_start_active requires start_active.");
    }
    if (name == "load_child_scene_inline") {
        if (!hasSceneId(arguments, "child_scene_id")) return fail("load_child_scene_inline requires child_scene_id.");
        return arguments.contains("load") && arguments["load"].is_boolean()
            ? ok() : fail("load_child_scene_inline requires load.");
    }
    if (name == "create_folder") {
        return hasString(arguments, "name") ? ok() : fail("create_folder requires name.");
    }
    if (name == "rename_resource") {
        return hasString(arguments, "path") && hasString(arguments, "new_name")
            ? ok() : fail("rename_resource requires path and new_name.");
    }
    if (name == "delete_resource") {
        return hasString(arguments, "path") ? ok() : fail("delete_resource requires path.");
    }
    if (name == "import_resource_file") {
        return hasString(arguments, "source_path") && hasString(arguments, "target_dir")
            ? ok() : fail("import_resource_file requires source_path and target_dir.");
    }
    if (name == "link_material") {
        if (!hasEntitySelector(arguments)) return fail("link_material requires entity_id or entity_name.");
        return hasString(arguments, "material_path") ? ok() : fail("link_material requires material_path.");
    }
    if (name == "unlink_material" || name == "remove_entity_from_bundle") {
        return hasEntitySelector(arguments) ? ok() : fail(name + " requires entity_id or entity_name.");
    }
    if (name == "create_material_file") {
        if (!hasEntitySelector(arguments)) return fail("create_material_file requires entity_id or entity_name.");
        return hasString(arguments, "target_dir") ? ok() : fail("create_material_file requires target_dir.");
    }
    if (name == "create_script") {
        if (!hasEntitySelector(arguments)) return fail("create_script requires entity_id or entity_name.");
        return hasString(arguments, "type") && hasString(arguments, "class_name")
            ? ok() : fail("create_script requires type and class_name.");
    }
    if (name == "attach_script") {
        if (!hasEntitySelector(arguments)) return fail("attach_script requires entity_id or entity_name.");
        return hasString(arguments, "type") && hasString(arguments, "class_name") && hasString(arguments, "path")
            ? ok() : fail("attach_script requires type, class_name, and path.");
    }
    if (name == "update_script_entry" || name == "remove_script_entry") {
        if (!hasEntitySelector(arguments)) return fail(name + " requires entity_id or entity_name.");
        const bool hasIndex = arguments.contains("index") && arguments["index"].is_number_integer();
        if (!hasIndex && !hasString(arguments, "class_name") && !hasString(arguments, "path")) {
            return fail(name + " requires index, class_name, or path.");
        }
        if (name == "update_script_entry" && !hasString(arguments, "new_class_name") &&
            !hasString(arguments, "new_path") && !hasString(arguments, "new_header_path")) {
            return fail("update_script_entry requires new_class_name, new_path, or new_header_path.");
        }
        return ok();
    }
    if (name == "update_script_file") {
        if (!hasString(arguments, "path")) return fail("update_script_file requires path.");
        if (!arguments.contains("content") || !arguments["content"].is_string()) {
            return fail("update_script_file requires string content.");
        }
        fs::path path = fs::path(arguments["path"].get<std::string>()).lexically_normal();
        if (!PathUtils::isSafeRelativePath(path)) {
            return fail("path must be a safe project-relative path.");
        }
        const std::string ext = path.extension().string();
        if (ext != ".lua" && ext != ".cpp" && ext != ".h" && ext != ".hpp") {
            return fail("update_script_file only supports .lua, .cpp, .h, and .hpp files.");
        }
        return ok();
    }
    if (name == "create_source_file") {
        if (!hasString(arguments, "path")) return fail("create_source_file requires path.");
        if (!arguments.contains("content") || !arguments["content"].is_string()) {
            return fail("create_source_file requires string content.");
        }
        fs::path path = fs::path(arguments["path"].get<std::string>()).lexically_normal();
        if (!PathUtils::isSafeRelativePath(path)) {
            return fail("path must be a safe project-relative path.");
        }
        // The same set update_script_file accepts, so a created file stays editable.
        const std::string ext = path.extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".hpp") {
            return fail("create_source_file only supports .cpp, .h, and .hpp files.");
        }
        return ok();
    }
    if (name == "set_project_script_dirs") {
        const auto it = arguments.find("directories");
        if (it == arguments.end() || !it->is_array()) {
            return fail("set_project_script_dirs requires a directories array.");
        }
        // Only the executor sees the configured roots that decide which forms are allowed.
        for (const Json& entry : *it) {
            if (!entry.is_string() || entry.get<std::string>().empty()) {
                return fail("directories must contain only non-empty strings.");
            }
        }
        return ok();
    }
    if (name == "set_project_asset_dirs") {
        if (isWrongTypedString(arguments, "assets_dir") || isWrongTypedString(arguments, "lua_dir")) {
            return fail("assets_dir and lua_dir must be strings.");
        }
        return hasString(arguments, "assets_dir") || hasString(arguments, "lua_dir")
            ? ok() : fail("set_project_asset_dirs requires assets_dir or lua_dir.");
    }
    if (name == "set_project_cxx_standard") {
        if (!arguments.contains("cxx_standard") || !arguments["cxx_standard"].is_number_integer()) {
            return fail("set_project_cxx_standard requires cxx_standard (" + cxxStandardList() + ").");
        }
        const Json& standard = arguments["cxx_standard"];
        if (standard < cxxStandards[0] || standard > cxxStandards[std::size(cxxStandards) - 1]
                || !isSupportedCxxStandard(standard.get<int>())) {
            return fail("cxx_standard must be " + cxxStandardList() + ".");
        }
        return ok();
    }
    if (name == "update_project_build_file") {
        if (!arguments.contains("content") || !arguments["content"].is_string()) {
            return fail("update_project_build_file requires string content.");
        }
        return ok();
    }
    if (name == "create_bundle_from_entity") {
        if (!hasString(arguments, "bundle_path")) return fail("create_bundle_from_entity requires bundle_path.");
        return hasEntitySelector(arguments) ? ok() : fail("create_bundle_from_entity requires entity_id or entity_name.");
    }
    if (name == "import_bundle_instance") {
        return hasString(arguments, "bundle_path") ? ok() : fail("import_bundle_instance requires bundle_path.");
    }
    if (name == "add_entity_to_bundle") {
        if (!hasEntitySelector(arguments)) return fail("add_entity_to_bundle requires entity_id or entity_name.");
        return arguments.contains("bundle_root_id") ? ok() : fail("add_entity_to_bundle requires bundle_root_id.");
    }
    if (name == "set_standalone_bundle") {
        return hasString(arguments, "bundle_path") ? ok() : fail("set_standalone_bundle requires bundle_path.");
    }
    if (name == "export_project" || name == "generate_shaders") {
        return hasString(arguments, "target_dir") ? ok() : fail(name + " requires target_dir.");
    }
    if (name == "fork_shader") {
        if (arguments.contains("directory")) {
            if (!arguments["directory"].is_string()) {
                return fail("fork_shader directory must be a string.");
            }
            fs::path directory = fs::path(arguments["directory"].get<std::string>()).lexically_normal();
            if (directory != "." && !PathUtils::isSafeRelativePath(directory)) {
                return fail("fork_shader directory must be a safe project-relative path.");
            }
        }
        if (arguments.contains("fork_includes") && !arguments["fork_includes"].is_boolean()) {
            return fail("fork_shader fork_includes must be a boolean.");
        }
        if (hasEntitySelector(arguments) || hasString(arguments, "shader_type")) return ok();
        return fail("fork_shader requires entity_id/entity_name, or shader_type to fork a scene default.");
    }
    if (name == "inspect_shader_uniforms") {
        if (hasEntitySelector(arguments) || arguments.contains("pass_index")) return ok();
        return fail("inspect_shader_uniforms requires entity_id/entity_name, or pass_index for a post-process pass.");
    }
    if (name == "set_shader_uniform") {
        if (!hasEntitySelector(arguments) && !arguments.contains("pass_index")) {
            return fail("set_shader_uniform requires entity_id/entity_name, or pass_index for a post-process pass.");
        }
        if (!hasString(arguments, "name")) return fail("set_shader_uniform requires name.");
        if (arguments.contains("remove") && !arguments["remove"].is_boolean()) {
            return fail("set_shader_uniform remove must be a boolean.");
        }
        const bool remove = arguments.value("remove", false);
        const bool hasValue = arguments.contains("number_value") || hasObject(arguments, "vector2_value") ||
                              hasObject(arguments, "vector3_value") || hasObject(arguments, "vector4_value");
        if (!remove && !hasValue) {
            return fail("set_shader_uniform requires number_value, vector2_value, vector3_value or vector4_value (or remove=true).");
        }
        return ok();
    }
    if (name == "write_shader_file") {
        if (!hasString(arguments, "path")) return fail("write_shader_file requires path.");
        if (!arguments.contains("content") || !arguments["content"].is_string()) {
            return fail("write_shader_file requires string content.");
        }
        fs::path path = fs::path(arguments["path"].get<std::string>()).lexically_normal();
        if (!PathUtils::isSafeRelativePath(path)) {
            return fail("path must be a safe project-relative path.");
        }
        const std::string ext = path.extension().string();
        if (ext != ".vert" && ext != ".frag" && ext != ".glsl") {
            return fail("write_shader_file only supports .vert, .frag, and .glsl files.");
        }
        return ok();
    }
    if (name == "create_terrain_heightmap") {
        std::string mode = arguments.value("mode", "middle");
        if (mode != "flat" && mode != "middle" && mode != "random_noise" && mode != "fractal_noise") {
            return fail("create_terrain_heightmap mode must be flat, middle, random_noise, or fractal_noise.");
        }
        return ok();
    }
    if (name == "import_project_model") {
        if (!hasString(arguments, "model_path")) {
            return fail("import_project_model requires model_path.");
        }
        if (!PathUtils::isSafeRelativePath(arguments["model_path"].get<std::string>())) {
            return fail("model_path must be a safe project-relative path.");
        }
        return ok();
    }
    if (name == "search_curated_assets") {
        return hasString(arguments, "provider") && hasString(arguments, "query")
            ? ok()
            : fail("search_curated_assets requires provider and query.");
    }
    if (name == "download_curated_asset") {
        if (!hasString(arguments, "provider") || !hasString(arguments, "asset_id") ||
            !hasString(arguments, "title") || !hasString(arguments, "source_url") ||
            !hasString(arguments, "download_url")) {
            return fail("download_curated_asset requires provider, asset_id, title, source_url, and download_url.");
        }
        const std::string url = arguments["download_url"].get<std::string>();
        if (url.rfind("https://", 0) != 0) {
            return fail("download_url must use HTTPS.");
        }
        return ok();
    }
    if (name == "read_resource_file") {
        return hasString(arguments, "path") ? ok() : fail("read_resource_file requires path.");
    }
    if (name == "inspect_scene") {
        return ok();
    }
    if (name == "set_main_camera") {
        if (arguments.value("clear", false)) return ok();
        return hasEntitySelector(arguments) ? ok() : fail("set_main_camera requires entity_id, entity_name, or clear.");
    }
    if (name == "open_scene") {
        return hasString(arguments, "scene_path") ? ok() : fail("open_scene requires scene_path.");
    }
    if (name == "select_scene" || name == "delete_scene") {
        return hasSceneId(arguments, "scene_id") ? ok() : fail(name + " requires scene_id.");
    }
    if (name == "regenerate_mesh_geometry") {
        return hasEntitySelector(arguments) ? ok() : fail("regenerate_mesh_geometry requires entity_id or entity_name.");
    }
    if (name == "save_scene") {
        return isWrongTypedString(arguments, "path") ? fail("save_scene path must be a string.") : ok();
    }
    if (name == "save_project") {
        return isWrongTypedString(arguments, "path") || isWrongTypedString(arguments, "name")
            ? fail("save_project path and name must be strings.") : ok();
    }
    if (name == "set_project_settings") {
        for (const char* key : {"canvas_width", "canvas_height", "window_width", "window_height"}) {
            if (arguments.contains(key) && (!arguments[key].is_number_integer() || arguments[key].get<int>() <= 0)) {
                return fail(std::string("set_project_settings ") + key + " must be a positive integer.");
            }
        }
        if (arguments.contains("loading_scene_id") && (!arguments["loading_scene_id"].is_number_integer() || arguments["loading_scene_id"].get<int64_t>() < 0)) {
            return fail("set_project_settings loading_scene_id must be a scene id, or 0 for none.");
        }
        for (const char* key : {"loading_delay", "loading_timeout"}) {
            if (arguments.contains(key) && (!arguments[key].is_number() || arguments[key].get<double>() < 0.0)) {
                return fail(std::string("set_project_settings ") + key + " must be a non-negative number.");
            }
        }
        for (const char* key : {"vsync", "async_loading", "web_resize_canvas", "web_hide_emscripten_ui"}) {
            if (arguments.contains(key) && !arguments[key].is_boolean()) {
                return fail(std::string("set_project_settings ") + key + " must be a boolean.");
            }
        }
        for (const char* key : {"scaling_mode", "texture_strategy"}) {
            if (isWrongTypedString(arguments, key)) {
                return fail(std::string("set_project_settings ") + key + " must be a string.");
            }
        }
        return ok();
    }
    if (name == "create_project" || name == "open_project") {
        return hasString(arguments, "path") ? ok() : fail(name + " requires path.");
    }
    if (name == "copy_resource") {
        return hasString(arguments, "source_path") && hasString(arguments, "target_dir")
            ? ok() : fail("copy_resource requires source_path and target_dir.");
    }
    if (name == "update_material_file") {
        if (!hasString(arguments, "path")) return fail("update_material_file requires path.");
        if (!arguments.contains("content") || !arguments["content"].is_string()) {
            return fail("update_material_file requires string content.");
        }
        return ok();
    }
    if (name == "set_component_properties") {
        if (!hasEntitySelector(arguments)) return fail("set_component_properties requires entity_id or entity_name.");
        if (!arguments.contains("properties") || !arguments["properties"].is_array() || arguments["properties"].empty()) {
            return fail("set_component_properties requires a non-empty properties array.");
        }
        for (const Json& item : arguments["properties"]) {
            if (!item.is_object() || !hasString(item, "component") || !hasString(item, "property")) {
                return fail("Each properties entry requires component and property.");
            }
            if (!hasAnyValueField(item)) {
                return fail("Each properties entry requires one typed value field.");
            }
        }
        return ok();
    }
    if (name == "add_tilemap_tile") {
        if (!hasEntitySelector(arguments)) return fail("add_tilemap_tile requires entity_id or entity_name.");
        return arguments.contains("rect_index") ? ok() : fail("add_tilemap_tile requires rect_index.");
    }
    if (name == "remove_tilemap_tile" || name == "duplicate_tilemap_tile") {
        if (!hasEntitySelector(arguments)) return fail(name + " requires entity_id or entity_name.");
        return arguments.contains("tile_index") ? ok() : fail(name + " requires tile_index.");
    }
    if (name == "add_animation_action") {
        return hasEntitySelector(arguments) ? ok() : fail("add_animation_action requires entity_id or entity_name.");
    }
    if (name == "remove_animation_action") {
        if (!hasEntitySelector(arguments)) return fail("remove_animation_action requires entity_id or entity_name.");
        return arguments.contains("action_index") ? ok() : fail("remove_animation_action requires action_index.");
    }
    if (name == "set_keyframe_times") {
        if (!hasEntitySelector(arguments)) return fail("set_keyframe_times requires entity_id or entity_name.");
        if (arguments.contains("times") && arguments["times"].is_array()) return ok();
        if (arguments.value("append", false) && arguments.contains("time") && arguments["time"].is_number()) return ok();
        return fail("set_keyframe_times requires times array or append=true with time.");
    }
    if (name == "set_keyframe_easing") {
        if (!hasEntitySelector(arguments)) return fail("set_keyframe_easing requires entity_id or entity_name.");
        if (arguments.contains("easings") && arguments["easings"].is_array()) return ok();
        if (arguments.contains("segment") && arguments.contains("ease")) return ok();
        return fail("set_keyframe_easing requires easings array or segment + ease.");
    }
    if (name == "set_keyframe_track") {
        if (!hasEntitySelector(arguments)) return fail("set_keyframe_track requires entity_id or entity_name.");
        return arguments.contains("times") && arguments["times"].is_array() ? ok() : fail("set_keyframe_track requires a times array.");
    }
    if (name == "undo_editor" || name == "redo_editor") {
        return ok();
    }

    return ok();
}

std::string EditorActionRegistry::describe(const std::string& name, const Json& arguments) {
    std::ostringstream out;
    if (name == "get_editor_guidance") {
        return "Read editor guidance";
    }
    if (name == "get_project_summary") {
        return "Read project summary";
    }
    if (name == "search_resources") {
        const std::string query = stringArg(arguments, "query");
        const std::string type = stringArg(arguments, "type");
        if (!query.empty()) {
            return "Search resources for \"" + query + "\"";
        }
        return (type.empty() || type == "any")
            ? "List project resources"
            : "List project " + type + " resources";
    }
    if (name == "list_scene_entities") {
        return arguments.contains("scene_id")
            ? "List entities in scene " + std::to_string(arguments.value("scene_id", 0u))
            : "List entities in the selected scene";
    }
    if (name == "inspect_entity") {
        return arguments.contains("entity_id")
            ? "Inspect entity " + std::to_string(arguments.value("entity_id", 0u))
            : "Inspect entity \"" + arguments.value("entity_name", "") + "\"";
    }
    if (name == "inspect_component") {
        return "Inspect " + arguments.value("component", "component") + " properties";
    }
    if (name == "list_component_types") {
        return "List editor component types";
    }
    if (name == "search_engine_api") {
        return "Search engine API for \"" + arguments.value("query", "") + "\"";
    }
    if (name == "search_engine_source") {
        return "Search engine source for \"" + arguments.value("query", "") + "\"";
    }
    if (name == "read_engine_source") {
        return "Read engine source " + arguments.value("path", "");
    }
    if (name == "read_output_log") {
        return "Read editor output log";
    }
    if (name == "create_entity") {
        out << "Create " << arguments.value("type", "entity")
            << " entity \"" << arguments.value("name", "Entity") << "\"";
        return out.str();
    }
    if (name == "set_entity_transform") {
        if (arguments.contains("entity_id")) {
            out << "Set transform for entity " << arguments["entity_id"].get<uint64_t>();
        } else {
            out << "Set transform for entity \"" << arguments.value("entity_name", "") << "\"";
        }
        return out.str();
    }
    if (name == "rename_entity") {
        return "Rename entity to \"" + arguments.value("new_name", "") + "\"";
    }
    if (name == "delete_entity") {
        return "Delete entity";
    }
    if (name == "duplicate_entity") {
        return "Duplicate entity";
    }
    if (name == "reparent_entity") {
        return "Reparent entity";
    }
    if (name == "move_entity_order") {
        return "Move entity " + arguments.value("insertion", "relative to target");
    }
    if (name == "select_entities") {
        return arguments.value("clear", false) ? "Clear entity selection" : "Select entity";
    }
    if (name == "add_component") {
        return "Add " + arguments.value("component", "component");
    }
    if (name == "remove_component") {
        return "Remove " + arguments.value("component", "component");
    }
    if (name == "add_body3d_shape") {
        return "Add " + arguments.value("shape_type", "3D collision") + " shape to Body3D";
    }
    if (name == "add_body2d_shape") {
        return "Add " + arguments.value("shape_type", "2D collision") + " shape to Body2D";
    }
    if (name == "set_component_property") {
        return "Set " + arguments.value("component", "component") + "." + arguments.value("property", "property");
    }
    if (name == "set_texture_settings") {
        return "Set texture settings of " + arguments.value("component", "component") + "." + arguments.value("property", "property");
    }
    if (name == "add_project_scene") {
        return "Add scene " + arguments.value("scene_path", "") + " to the project";
    }
    if (name == "create_scene") {
        return "Create " + arguments.value("type", "scene") + " scene \"" + arguments.value("name", "Scene") + "\"";
    }
    if (name == "rename_scene") {
        return "Rename scene to \"" + arguments.value("new_name", "") + "\"";
    }
    if (name == "save_scene") {
        return hasString(arguments, "path") ? "Save scene to " + arguments["path"].get<std::string>() : "Save scene";
    }
    if (name == "save_all_scenes") {
        return "Save all scenes";
    }
    if (name == "set_start_scene") {
        return "Set startup scene " + std::to_string(arguments.value("scene_id", 0u));
    }
    if (name == "set_scene_property") {
        return "Set scene property " + arguments.value("property", "");
    }
    if (name == "control_play_mode") {
        std::string action = arguments.value("action", "control");
        if (!action.empty()) {
            action[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(action[0])));
        }
        return action + " play mode";
    }
    if (name == "add_child_scene") {
        return "Add child scene " + std::to_string(arguments.value("child_scene_id", 0u));
    }
    if (name == "remove_child_scene") {
        return "Remove child scene " + std::to_string(arguments.value("child_scene_id", 0u));
    }
    if (name == "set_child_scene_start_active") {
        return std::string(arguments.value("start_active", true) ? "Enable" : "Disable") + " child scene Start active";
    }
    if (name == "load_child_scene_inline") {
        return std::string(arguments.value("load", true) ? "Load" : "Unload") + " child scene inline";
    }
    if (name == "create_folder") {
        return "Create folder \"" + arguments.value("name", "") + "\"";
    }
    if (name == "rename_resource") {
        return "Rename resource " + arguments.value("path", "");
    }
    if (name == "delete_resource") {
        return "Delete resource " + arguments.value("path", "");
    }
    if (name == "import_resource_file") {
        return "Import file " + arguments.value("source_path", "");
    }
    if (name == "link_material") {
        return "Link material " + arguments.value("material_path", "");
    }
    if (name == "unlink_material") {
        return "Unlink material";
    }
    if (name == "create_material_file") {
        return "Create linked material file";
    }
    if (name == "set_terrain_textures") {
        return "Set terrain textures";
    }
    if (name == "set_editor_camera") {
        return "Move editor camera";
    }
    if (name == "set_terrain_foliage_layer") {
        if (arguments.value("remove", false)) return "Remove terrain foliage layer";
        return arguments.contains("layer_index") ? "Change terrain foliage layer" : "Add terrain foliage layer";
    }
    if (name == "create_terrain_blendmap") {
        return "Create terrain blendmap";
    }
    if (name == "add_terrain_collision") {
        return "Add terrain collision";
    }
    if (name == "create_script") {
        return "Create " + arguments.value("type", "script") + " script " + arguments.value("class_name", "");
    }
    if (name == "attach_script") {
        return "Attach script " + arguments.value("class_name", "");
    }
    if (name == "update_script_entry" || name == "remove_script_entry") {
        const std::string action = (name == "update_script_entry") ? "Update" : "Remove";
        std::string label = arguments.value("class_name", "");
        if (label.empty()) label = arguments.value("path", "");
        return label.empty() ? action + " script entry" : action + " script entry " + label;
    }
    if (name == "update_script_file") {
        return "Update script file " + arguments.value("path", "");
    }
    if (name == "create_source_file") {
        return "Create source file " + arguments.value("path", "");
    }
    if (name == "set_project_script_dirs") {
        const auto it = arguments.find("directories");
        if (it == arguments.end() || !it->is_array() || it->empty()) {
            return "Clear project script directories";
        }
        std::string list;
        for (const Json& entry : *it) {
            if (!entry.is_string()) continue;
            if (!list.empty()) list += ", ";
            list += entry.get<std::string>();
        }
        return list.empty() ? "Set project script directories"
                            : "Set project script directories: " + list;
    }
    if (name == "set_project_asset_dirs") {
        return "Set project asset directories";
    }
    if (name == "check_cpp_build_tools") {
        return "Check C++ build tools";
    }
    if (name == "set_project_cxx_standard") {
        if (arguments.contains("cxx_standard") && arguments["cxx_standard"].is_number_integer()) {
            return "Set project C++ standard to C++" + std::to_string(arguments["cxx_standard"].get<int>());
        }
        return "Set project C++ standard";
    }
    if (name == "update_project_build_file") {
        return "Update ProjectBuild.cmake";
    }
    if (name == "create_bundle_from_entity") {
        return "Create bundle " + arguments.value("bundle_path", "");
    }
    if (name == "import_bundle_instance") {
        return "Import bundle " + arguments.value("bundle_path", "");
    }
    if (name == "add_entity_to_bundle") {
        return "Add entity to bundle";
    }
    if (name == "remove_entity_from_bundle") {
        return "Remove entity from bundle";
    }
    if (name == "make_bundle_component_unique") {
        return "Make bundle component unique";
    }
    if (name == "revert_bundle_component") {
        return "Revert bundle component";
    }
    if (name == "set_standalone_bundle") {
        return "Set standalone bundle " + arguments.value("bundle_path", "");
    }
    if (name == "set_export_shaders") {
        return "Set export shader overrides";
    }
    if (name == "export_project") {
        return "Export project to " + arguments.value("target_dir", "");
    }
    if (name == "generate_shaders") {
        return "Generate shaders to " + arguments.value("target_dir", "");
    }
    if (name == "fork_shader") {
        if (arguments.contains("entity_id")) {
            return "Fork shader for entity " + std::to_string(arguments.value("entity_id", 0u));
        }
        if (arguments.contains("entity_name")) {
            return "Fork shader for \"" + arguments.value("entity_name", "") + "\"";
        }
        if (arguments.contains("shader_type")) {
            return "Fork scene default " + arguments.value("shader_type", "") + " shader";
        }
        return "Fork custom shader";
    }
    if (name == "write_shader_file") {
        return "Write shader " + arguments.value("path", "");
    }
    if (name == "inspect_shader_uniforms") {
        return "Inspect shader uniforms";
    }
    if (name == "set_shader_uniform") {
        const std::string verb = arguments.value("remove", false) ? "Remove" : "Set";
        if (arguments.contains("pass_index")) {
            return verb + " post-process uniform " + arguments.value("name", "") + " of pass " + std::to_string(arguments.value("pass_index", 0));
        }
        return verb + " shader uniform " + arguments.value("name", "");
    }
    if (name == "create_terrain_heightmap") {
        out << "Create " << arguments.value("mode", "middle")
            << " terrain heightmap";
        if (arguments.contains("entity_id")) {
            out << " for entity " << arguments["entity_id"].get<uint64_t>();
        } else if (arguments.contains("entity_name")) {
            out << " for \"" << arguments.value("entity_name", "") << "\"";
        }
        return out.str();
    }
    if (name == "import_project_model") {
        return "Import model " + arguments.value("model_path", "");
    }
    if (name == "reload_project_model") {
        return "Reload model";
    }
    if (name == "search_curated_assets") {
        return "Search " + arguments.value("provider", "curated assets") +
               " for \"" + arguments.value("query", "") + "\"";
    }
    if (name == "download_curated_asset") {
        return "Download curated asset \"" + arguments.value("title", "Asset") + "\"";
    }
    if (name == "read_resource_file") {
        return "Read resource " + arguments.value("path", "");
    }
    if (name == "inspect_scene") {
        return arguments.contains("scene_id")
            ? "Inspect scene " + std::to_string(arguments.value("scene_id", 0u))
            : "Inspect selected scene";
    }
    if (name == "set_main_camera") {
        return arguments.value("clear", false) ? "Clear main camera" : "Set main camera";
    }
    if (name == "open_scene") {
        return "Open scene " + arguments.value("scene_path", "");
    }
    if (name == "select_scene") {
        return "Select scene " + std::to_string(arguments.value("scene_id", 0u));
    }
    if (name == "regenerate_mesh_geometry") {
        return "Regenerate mesh geometry";
    }
    if (name == "delete_scene") {
        return "Delete scene " + std::to_string(arguments.value("scene_id", 0u));
    }
    if (name == "save_project") {
        return hasString(arguments, "path") ? "Save project to " + arguments["path"].get<std::string>() : "Save project";
    }
    if (name == "set_project_settings") {
        return "Change project settings";
    }
    if (name == "create_project") {
        return "Create project " + arguments.value("path", "");
    }
    if (name == "open_project") {
        return "Open project " + arguments.value("path", "");
    }
    if (name == "copy_resource") {
        return "Copy " + arguments.value("source_path", "") + " to " + arguments.value("target_dir", "");
    }
    if (name == "update_material_file") {
        return "Update material file " + arguments.value("path", "");
    }
    if (name == "set_component_properties") {
        return "Set multiple component properties";
    }
    if (name == "add_tilemap_tile") {
        return "Add tilemap tile";
    }
    if (name == "remove_tilemap_tile") {
        return "Remove tilemap tile " + std::to_string(arguments.value("tile_index", 0));
    }
    if (name == "duplicate_tilemap_tile") {
        return "Duplicate tilemap tile " + std::to_string(arguments.value("tile_index", 0));
    }
    if (name == "add_animation_action") {
        return "Add animation action";
    }
    if (name == "remove_animation_action") {
        return "Remove animation action " + std::to_string(arguments.value("action_index", 0));
    }
    if (name == "set_keyframe_times") {
        return "Set keyframe times";
    }
    if (name == "set_keyframe_easing") {
        return "Set keyframe easing";
    }
    if (name == "set_keyframe_track") {
        return "Set keyframe track";
    }
    if (name == "undo_editor") {
        return "Undo " + arguments.value("scope", "scene") + " command";
    }
    if (name == "redo_editor") {
        return "Redo " + arguments.value("scope", "scene") + " command";
    }
    return name;
}

} // namespace doriax::editor::ai
