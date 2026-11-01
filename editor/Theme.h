// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "imgui.h"

namespace doriax::editor {

    class Theme {
    public:
        struct Colors {
            static ImVec4 ButtonActivated;
            // Hover/press tint for transparent icon buttons (find close, replace toggle).
            static ImVec4 IconButtonHovered;
            static ImVec4 IconButtonActive;
            static ImVec4 FileCardBackground;
            static ImVec4 FileCardBackgroundHovered;
            static ImVec4 SubtleText;
            static ImVec4 filenameLabel;
            static ImVec4 ExtEntityButton;
            static ImVec4 ExtEntityButtonHovered;
            static ImVec4 ExtEntityButtonActive;
            static ImVec4 NestedHeader;
            static ImVec4 NestedHeaderHovered;
            static ImVec4 NestedHeaderActive;
            static ImVec4 DisabledGreenText;
            static ImVec4 ChildSceneText;
            // Structure tree
            static ImVec4 RuntimeEntityText;
            static ImVec4 CameraPreviewText;
            static ImVec4 SearchMatchText;
            static ImVec4 LockedBundleText;
            static ImVec4 BundleRootText;
            static ImVec4 NestedBundleRootText;
            static ImVec4 BundleChildText;
            static ImVec4 TreeMarkText;
            static ImVec4 ChildSceneUnloadedText;
            static ImVec4 ChildSceneEyeHovered;
            static ImVec4 ChildSceneEyeActive;
            // Marks the viewport sub-selection (tiles, instances, occluder points)
            // in Properties lists; same orange as the viewport selection outline.
            static ImVec4 SubSelectionText;
            // Amber warning text, e.g. alert notes; matches OutputWindow's Warning log color.
            static ImVec4 WarningText;
        };

        static void apply();
        static void applyDpiScale(float dpiScale);
        // Editor scale on top of the monitor DPI
        static void setUiScale(float uiScale);
        static float uiScale();

        static float dpiScale();
        static float dpi(float value);
        static ImVec2 dpi(const ImVec2& value);
    };

}

