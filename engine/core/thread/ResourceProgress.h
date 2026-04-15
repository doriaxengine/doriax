//
// (c) 2026 Eduardo Doria.
//

#ifndef RESOURCEPROGRESS_H
#define RESOURCEPROGRESS_H

#include "Export.h"
#include <string>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <vector>

namespace doriax {

    enum class ResourceType {
        Shader,
        Texture,
        Model,
        Audio,
        Material
    };

    struct ResourceBuildInfo {
        ResourceType type;
        std::string name;
        float progress = 0.0f; // 0.0 to 1.0
        bool isActive = false;
        std::chrono::steady_clock::time_point startTime;
    };

    struct OverallBuildProgress {
        float totalProgress = 0.0f;  // 0.0 to 1.0
        int totalBuilds = 0;
        int completedBuilds = 0;
        std::string currentBuildName;  // Name of the most recently started build
        ResourceType currentBuildType; // Type of the most recently started build
        bool hasActiveBuilds = false;
    };

    class DORIAX_API ResourceProgress {
    private:
        static std::mutex progressMutex;
        static std::unordered_map<uint64_t, ResourceBuildInfo> activeBuilds;
        static uint64_t mostRecentBuildId;

    public:
        static void startBuild(uint64_t id, ResourceType type, const std::string& name);
        static void updateProgress(uint64_t id, float progress);
        static void completeBuild(uint64_t id);
        static void failBuild(uint64_t id);

        static bool hasActiveBuilds();
        static OverallBuildProgress getOverallProgress();
        static int getActiveBuildCount();
        static std::vector<ResourceBuildInfo> getAllActiveBuilds();

        // Keep for backward compatibility if needed
        static ResourceBuildInfo getCurrentBuild();

        // Helper methods
        static std::string getResourceTypeName(ResourceType type);
    };

}

#endif //RESOURCEPROGRESS_H