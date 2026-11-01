// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "BidiText.h"

#include "util/StringUtils.h"

#include <SheenBidi/SheenBidi.h>

namespace doriax::editor {

// Presentation form of each letter, 0 where that form does not exist. A letter with no
// initial form does not connect to the next one.
struct ArabicForms {
    uint32_t letter;
    uint32_t isolated;
    uint32_t final;
    uint32_t initial;
    uint32_t medial;
};

static const ArabicForms arabicForms[] = {
    {0x0621, 0xFE80, 0,      0,      0     },
    {0x0622, 0xFE81, 0xFE82, 0,      0     },
    {0x0623, 0xFE83, 0xFE84, 0,      0     },
    {0x0624, 0xFE85, 0xFE86, 0,      0     },
    {0x0625, 0xFE87, 0xFE88, 0,      0     },
    {0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C},
    {0x0627, 0xFE8D, 0xFE8E, 0,      0     },
    {0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92},
    {0x0629, 0xFE93, 0xFE94, 0,      0     },
    {0x062A, 0xFE95, 0xFE96, 0xFE97, 0xFE98},
    {0x062B, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C},
    {0x062C, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0},
    {0x062D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4},
    {0x062E, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8},
    {0x062F, 0xFEA9, 0xFEAA, 0,      0     },
    {0x0630, 0xFEAB, 0xFEAC, 0,      0     },
    {0x0631, 0xFEAD, 0xFEAE, 0,      0     },
    {0x0632, 0xFEAF, 0xFEB0, 0,      0     },
    {0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4},
    {0x0634, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8},
    {0x0635, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC},
    {0x0636, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0},
    {0x0637, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4},
    {0x0638, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8},
    {0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC},
    {0x063A, 0xFECD, 0xFECE, 0xFECF, 0xFED0},
    {0x0641, 0xFED1, 0xFED2, 0xFED3, 0xFED4},
    {0x0642, 0xFED5, 0xFED6, 0xFED7, 0xFED8},
    {0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC},
    {0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0},
    {0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4},
    {0x0646, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8},
    {0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC},
    {0x0648, 0xFEED, 0xFEEE, 0,      0     },
    {0x0649, 0xFEEF, 0xFEF0, 0,      0     },
    {0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4},

    // Persian, Urdu and other extended letters. Their forms live in Presentation
    // Forms-A instead, which is why they need their own entries
    {0x0671, 0xFB50, 0xFB51, 0,      0     },
    {0x0679, 0xFB66, 0xFB67, 0xFB68, 0xFB69},
    {0x067A, 0xFB5E, 0xFB5F, 0xFB60, 0xFB61},
    {0x067B, 0xFB52, 0xFB53, 0xFB54, 0xFB55},
    {0x067E, 0xFB56, 0xFB57, 0xFB58, 0xFB59},
    {0x067F, 0xFB62, 0xFB63, 0xFB64, 0xFB65},
    {0x0680, 0xFB5A, 0xFB5B, 0xFB5C, 0xFB5D},
    {0x0683, 0xFB76, 0xFB77, 0xFB78, 0xFB79},
    {0x0684, 0xFB72, 0xFB73, 0xFB74, 0xFB75},
    {0x0686, 0xFB7A, 0xFB7B, 0xFB7C, 0xFB7D},
    {0x0687, 0xFB7E, 0xFB7F, 0xFB80, 0xFB81},
    {0x0688, 0xFB88, 0xFB89, 0,      0     },
    {0x068C, 0xFB84, 0xFB85, 0,      0     },
    {0x068D, 0xFB82, 0xFB83, 0,      0     },
    {0x068E, 0xFB86, 0xFB87, 0,      0     },
    {0x0691, 0xFB8C, 0xFB8D, 0,      0     },
    {0x0698, 0xFB8A, 0xFB8B, 0,      0     },
    {0x06A4, 0xFB6A, 0xFB6B, 0xFB6C, 0xFB6D},
    {0x06A6, 0xFB6E, 0xFB6F, 0xFB70, 0xFB71},
    {0x06A9, 0xFB8E, 0xFB8F, 0xFB90, 0xFB91},
    {0x06AD, 0xFBD3, 0xFBD4, 0xFBD5, 0xFBD6},
    {0x06AF, 0xFB92, 0xFB93, 0xFB94, 0xFB95},
    {0x06B1, 0xFB9A, 0xFB9B, 0xFB9C, 0xFB9D},
    {0x06B3, 0xFB96, 0xFB97, 0xFB98, 0xFB99},
    {0x06BA, 0xFB9E, 0xFB9F, 0,      0     },
    {0x06BB, 0xFBA0, 0xFBA1, 0xFBA2, 0xFBA3},
    {0x06BE, 0xFBAA, 0xFBAB, 0xFBAC, 0xFBAD},
    {0x06C0, 0xFBA4, 0xFBA5, 0,      0     },
    {0x06C1, 0xFBA6, 0xFBA7, 0xFBA8, 0xFBA9},
    {0x06C5, 0xFBE0, 0xFBE1, 0,      0     },
    {0x06C6, 0xFBD9, 0xFBDA, 0,      0     },
    {0x06C7, 0xFBD7, 0xFBD8, 0,      0     },
    {0x06C8, 0xFBDB, 0xFBDC, 0,      0     },
    {0x06C9, 0xFBE2, 0xFBE3, 0,      0     },
    {0x06CB, 0xFBDE, 0xFBDF, 0,      0     },
    {0x06CC, 0xFBFC, 0xFBFD, 0xFBFE, 0xFBFF},
    {0x06D0, 0xFBE4, 0xFBE5, 0xFBE6, 0xFBE7},
    {0x06D2, 0xFBAE, 0xFBAF, 0,      0     },
    {0x06D3, 0xFBB0, 0xFBB1, 0,      0     }
};

// Lam followed by one of these collapses into a single glyph.
struct LamAlef {
    uint32_t alef;
    uint32_t isolated;
    uint32_t final;
};

static const LamAlef lamAlefs[] = {
    {0x0622, 0xFEF5, 0xFEF6},
    {0x0623, 0xFEF7, 0xFEF8},
    {0x0625, 0xFEF9, 0xFEFA},
    {0x0627, 0xFEFB, 0xFEFC}
};

static const uint32_t TATWEEL = 0x0640;

static const ArabicForms* findForms(uint32_t codepoint) {
    if (codepoint < 0x0621 || (codepoint > 0x064A && codepoint < 0x0671) || codepoint > 0x06D3) {
        return nullptr;
    }

    for (const ArabicForms& forms : arabicForms) {
        if (forms.letter == codepoint) {
            return &forms;
        }
    }
    return nullptr;
}

static const LamAlef* findLamAlef(uint32_t codepoint) {
    for (const LamAlef& ligature : lamAlefs) {
        if (ligature.alef == codepoint) {
            return &ligature;
        }
    }
    return nullptr;
}

// Harakat sit above or below a letter without breaking the join.
static bool isTransparent(uint32_t codepoint) {
    return (codepoint >= 0x064B && codepoint <= 0x065F) || codepoint == 0x0670
        || (codepoint >= 0x06D6 && codepoint <= 0x06ED);
}

static bool joinsForward(uint32_t codepoint) {
    const ArabicForms* forms = findForms(codepoint);
    return codepoint == TATWEEL || (forms && forms->initial != 0);
}

static bool joinsBackward(uint32_t codepoint) {
    const ArabicForms* forms = findForms(codepoint);
    return codepoint == TATWEEL || (forms && forms->final != 0);
}

// Picks the form of every letter in the run, still in logical order.
static void shapeArabic(const std::vector<uint32_t>& codepoints, size_t offset, size_t length, std::vector<uint32_t>& out) {
    for (size_t i = offset; i < offset + length; i++) {
        uint32_t codepoint = codepoints[i];

        if (isTransparent(codepoint)) {
            out.push_back(codepoint);
            continue;
        }

        size_t previous = i;
        while (previous > offset && isTransparent(codepoints[previous - 1])) {
            previous--;
        }
        bool afterJoiner = previous > offset && joinsForward(codepoints[previous - 1]);

        size_t next = i + 1;
        while (next < offset + length && isTransparent(codepoints[next])) {
            next++;
        }
        bool beforeJoinable = next < offset + length && joinsBackward(codepoints[next]);

        if (codepoint == 0x0644 && next < offset + length) {
            if (const LamAlef* ligature = findLamAlef(codepoints[next])) {
                out.push_back(afterJoiner ? ligature->final : ligature->isolated);
                i = next;
                continue;
            }
        }

        const ArabicForms* forms = findForms(codepoint);
        if (!forms) {
            out.push_back(codepoint);
            continue;
        }

        bool joinPrevious = afterJoiner && forms->final != 0;
        bool joinNext = beforeJoinable && forms->initial != 0;

        uint32_t form = forms->isolated;
        if (joinPrevious && joinNext) {
            form = forms->medial;
        } else if (joinPrevious) {
            form = forms->final;
        } else if (joinNext) {
            form = forms->initial;
        }

        out.push_back(form != 0 ? form : codepoint);
    }
}

std::string BidiText::toVisual(const std::string& text) {
    // Every right-to-left codepoint starts on a byte of 0xD6 or above, so plain text
    // leaves without decoding. This runs per item per frame in lists.
    bool couldBeRightToLeft = false;
    for (unsigned char byte : text) {
        if (byte >= 0xD6) {
            couldBeRightToLeft = true;
            break;
        }
    }

    if (!couldBeRightToLeft) {
        return text;
    }

    bool hadInvalid = false;
    std::vector<uint32_t> codepoints = StringUtils::decodeUtf8ToCodepoints(text, hadInvalid);

    bool rightToLeft = false;
    for (uint32_t codepoint : codepoints) {
        // rebuilding the string goes through wchar_t, which loses astral characters
        // where it is 16 bits, so those strings are left alone
        if (codepoint > 0xFFFF) {
            return text;
        }
        if (codepoint >= 0x0590 && codepoint <= 0x08FF) {
            rightToLeft = true;
        }
    }

    if (!rightToLeft) {
        return text;
    }

    SBCodepointSequence sequence;
    sequence.stringEncoding = SBStringEncodingUTF32;
    sequence.stringBuffer = (void*)codepoints.data();
    sequence.stringLength = (SBUInteger)codepoints.size();

    SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
    if (!algorithm) {
        return text;
    }

    std::vector<uint32_t> visual;
    visual.reserve(codepoints.size());

    // One paragraph per line, so a newline does not drag the next line into its order.
    SBUInteger start = 0;
    while (start < codepoints.size()) {
        SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, start, INT32_MAX, SBLevelDefaultLTR);
        if (!paragraph) {
            break;
        }

        SBUInteger paragraphLength = SBParagraphGetLength(paragraph);
        SBLineRef line = paragraphLength > 0 ? SBParagraphCreateLine(paragraph, start, paragraphLength) : nullptr;

        if (line) {
            SBUInteger runCount = SBLineGetRunCount(line);
            const SBRun* runs = SBLineGetRunsPtr(line);

            for (SBUInteger i = 0; i < runCount; i++) {
                size_t offset = (size_t)runs[i].offset;
                size_t length = (size_t)runs[i].length;

                if ((runs[i].level & 1) == 0) {
                    visual.insert(visual.end(), codepoints.begin() + offset, codepoints.begin() + offset + length);
                    continue;
                }

                std::vector<uint32_t> shaped;
                shapeArabic(codepoints, offset, length, shaped);
                visual.insert(visual.end(), shaped.rbegin(), shaped.rend());
            }

            SBLineRelease(line);
        }

        SBParagraphRelease(paragraph);

        if (paragraphLength == 0) {
            break;
        }
        start += paragraphLength;
    }

    SBAlgorithmRelease(algorithm);

    std::string out;
    out.reserve(text.size());
    for (uint32_t codepoint : visual) {
        out += StringUtils::toUTF8(codepoint);
    }

    return out;
}

}
