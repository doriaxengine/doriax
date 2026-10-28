// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SHADERPOOL_H
#define SHADERPOOL_H

#include "Engine.h"
#include "render/ShaderRender.h"
#include "shader/ShaderBuildTypes.h"
#include <map>
#include <memory>
#include <functional>
#include <vector>

namespace doriax{

    typedef std::map<ShaderKey, std::shared_ptr<ShaderRender>> shaders_t;

    using ShaderBuilderFn = std::function<ShaderBuildResult(ShaderKey)>;

    class DORIAX_API ShaderPool{
    private:
        static shaders_t& getMap();

        static ShaderBuilderFn shaderBuilderFn;

        static std::string getShaderFile(const std::string& shaderStr, const std::string& extension);
        static std::string getShaderName(const std::string& shaderStr);

        static bool loadPrecompiled(ShaderType shaderType, uint32_t properties, uint16_t customId, ShaderData& shaderData);
        static uint32_t getFallbackProperties(ShaderType shaderType, uint32_t properties, uint16_t customId);
        static void addMissingShader(const std::string& shaderStr);

    public:
        // ShaderKey layout (uint64):
        //   bits [0..31]  properties bitfield
        //   bits [32..47] ShaderType
        //   bits [48..63] customShaderId (0 = built-in). Built-in keys are unchanged
        //                 (customId 0), so old serialized keys remain valid.
        static ShaderKey getShaderKey(ShaderType shaderType, uint32_t properties, uint16_t customId = 0);

        static std::shared_ptr<ShaderRender> get(ShaderType shaderType, uint32_t properties, uint16_t customId = 0);
        static void remove(ShaderType shaderType, uint32_t properties, uint16_t customId = 0);

        // True when the last get() attempt failed, either building the source or creating
        // the backend resource. The pool stops re-invoking the (expensive, error-spamming)
        // builder until the failure is cleared by remove()/destroyCustomShaders().
        static bool isShaderBuildFailed(ShaderType shaderType, uint32_t properties, uint16_t customId = 0);

        static std::string getShaderStr(ShaderType shaderType, uint32_t properties, uint16_t customId = 0);
        static std::string getShaderTypeName(ShaderType shaderType, bool lowerCase = false);
        static bool parseShaderTypeToken(const std::string& typeToken, ShaderType& shaderType);
        static int getShaderPropertyCount(ShaderType shaderType);
        static std::string getShaderPropertyName(ShaderType shaderType, int bit, bool shortName = true);
        // by short or long name, -1 when unknown
        static int getShaderPropertyBit(ShaderType shaderType, const std::string& name);
        static ShaderType getShaderTypeFromKey(ShaderKey key);
        static uint32_t getPropertiesFromKey(ShaderKey key);
        static uint16_t getCustomIdFromKey(ShaderKey key);

        // Key used to validate a serialized .sdat/header blob. The customShaderId is a
        // session-local registry value (assigned in registration order) and is NOT
        // stable between the editor that exported the shader and the standalone runtime
        // that loads it. The unique filename already identifies the forked source, so
        // the on-disk validation key drops customId and keeps only type+properties.
        static ShaderKey getStorageKey(ShaderKey key);

        // Bitmask of the property positions that currently map to a real variant for
        // this shader type (derived from getShaderPropertyName, so retired/reserved
        // bits like MESH bit 5 are excluded). Used to normalize keys.
        static uint32_t getValidPropertyMask(ShaderType shaderType);

        // Strips property bits that no longer map to a variant (retired features,
        // bits beyond the current count). Keeps type + customId. Persisted keys from
        // older editor versions collapse onto their current equivalent, so stale
        // variants stop being exported and looked up.
        static ShaderKey normalizeKey(ShaderKey key);

        // Custom (user-forked) shader registry. Interns a project-relative base path
        // (e.g. "shaders/myMesh", no extension) and returns a stable session id used
        // in the ShaderKey; an empty path returns 0 (built-in). The derived (sanitized)
        // basename is what makes editor build, disk cache, export and shipping-runtime
        // .sdat filenames line up. Thread-safe (ShaderBuilder runs on a thread pool).
        static uint16_t registerCustomShader(const std::string& baseName);
        static std::string getCustomShaderName(uint16_t customId);     // project-relative base, "" if none
        static std::string getCustomShaderBasename(uint16_t customId); // filesystem-safe token, "" if none

        // Frees the GPU handle of every cached forked-shader variant so the next get()
        // rebuilds (used by the editor after a shader source is edited). The pool keeps
        // the ShaderRender objects, so meshes holding them stay valid and re-bind on
        // their needReload pass.
        static void destroyCustomShaders();

        static const std::vector<ShaderBackend>& getShaderBackends();
        // backend this runtime was built for
        static ShaderBackend getShaderBackend();
        static std::string getShaderBackendName(ShaderBackend backend);      // "Metal (iOS)"
        static std::string getShaderBackendCliToken(ShaderBackend backend);  // "metal-ios"
        static bool parseShaderBackend(const std::string& value, ShaderBackend& out);

        static std::string getShaderLangStr();
        static std::string getShaderLangStr(ShaderBackend backend);
        static std::string getShaderLangStr(ShaderLang lang, int version, bool es = false, Platform platform = Platform::Linux);
        static bool getShaderCliSpec(const std::string& shaderStr, std::string& cliSpec);
        static std::string getSuggestedCliBackend();
        static std::string getMissingShadersCliArgs();
        static std::string getMissingShadersDisplayList();
        static std::vector<std::string>& getMissingShaders();

        static void setShaderBuilder(ShaderBuilderFn fn);

        static uint32_t getMeshProperties(bool unlit, bool uv1, bool uv2,
						bool punctual, bool shadows, bool normals, bool normalMap,
						bool tangents, bool vertexColorVec3, bool vertexColorVec4, bool textureRect,
                        bool fog, bool skinning, bool morphTarget, bool morphNormal, bool morphTangent,
                        bool terrain, bool instanced, bool ibl, bool mirror, bool ssao = false,
                        bool light2d = false, bool shadows2d = false,
                        bool alphaMask = false, bool alphaOpaque = false, bool instanceFade = false,
                        bool terrainPBR = false);
        static uint32_t getDepthMeshProperties(bool texture, bool skinning, bool morphTarget, bool morphNormal, bool morphTangent, bool terrain, bool instanced, bool alphaMask = false, bool instanceFade = false);
        static uint32_t getGBufferMeshProperties(bool baseColorTexture, bool normals, bool skinning, bool morphTarget, bool morphNormal, bool morphTangent, bool terrain, bool instanced, bool metallicRoughnessTexture, bool terrainPBR = false);
        static uint32_t getUIProperties(bool texture, bool fontAtlasTexture, bool vertexColorVec3, bool vertexColorVec4);
        static uint32_t getPointsProperties(bool texture, bool vertexColorVec3, bool vertexColorVec4, bool textureRect);
        static uint32_t getLinesProperties(bool vertexColorVec3, bool vertexColorVec4);
        static uint32_t getWaterProperties(bool fog, bool sceneDepth, bool planarReflection, bool shadows, bool refraction);

		// necessary for engine shutdown
		static void clear();
		// Destroys shaders no one else references and resets per-project state
		// (missing/failed lists, custom shader registry)
		static void clearUnused();
    };
}

#endif /* SHADERPOOL_H */
