// ============================================================
//  Protek506Logger — DmmParser.cpp
// ============================================================
#include "DmmParser.h"
#include <cctype>
#include <algorithm>
#include <cstring>
#include <regex>

// Known mode first-byte characters
static const char* s_modeCodes = "ABCDFILT";

DmmParser::DmmParser() {}

bool DmmParser::IsKnownModeCode(char c)
{
    return strchr(s_modeCodes, (unsigned char)c) != nullptr;
}

std::string DmmParser::MapMode(char code) const
{
    switch (code)
    {
        case 'A': return "AC";
        case 'B': return "DIODE";   // alternate continuity code
        case 'C': return "CAP";
        case 'D': return "DC";
        case 'F': return "FREQ";
        case 'I': return "IND";
        case 'L': return "DIODE";   // logic / diode / continuity
        case 'R': return "RES";
        case 'T': return "TEMP";
        default:  return std::string(1, code);
    }
}

// Trim leading/trailing whitespace in-place
static std::string Trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

void DmmParser::ParseValueAndUnits(const std::string& rest,
                                   DmmReading& out) const
{
    // Case-insensitive search helpers
    auto ciFind = [](const std::string& hay, const char* needle) {
        std::string h = hay, n = needle;
        std::transform(h.begin(), h.end(), h.begin(), ::toupper);
        std::transform(n.begin(), n.end(), n.begin(), ::toupper);
        return h.find(n);
    };

    std::string r = Trim(rest);

    // --- overload variants ---
    if (ciFind(r, "OL") != std::string::npos ||
        ciFind(r, ".OL") != std::string::npos ||
        ciFind(r, "-OL") != std::string::npos)
    {
        out.isOverload = true;
        out.rawValue   = "OL";
        // units may follow OL
        size_t pos = ciFind(r, "OL");
        if (pos != std::string::npos)
        {
            std::string after = Trim(r.substr(pos + 2));
            if (!after.empty()) out.units = after;
        }
        return;
    }

    // --- continuity / diode special readings ---
    if (ciFind(r, "OPEN") != std::string::npos)
    {
        out.isOpen   = true;
        out.rawValue = "OPEN";
        return;
    }
    if (ciFind(r, "SHORT") != std::string::npos)
    {
        out.isShort  = true;
        out.rawValue = "SHORT";
        return;
    }

    // --- logic levels ---
    if (ciFind(r, "HIGH") != std::string::npos ||
        r.find("Hi") != std::string::npos ||
        r.find("HI") != std::string::npos)
    {
        out.isLogicHigh = true;
        out.rawValue    = "High";
        return;
    }
    if (ciFind(r, "LOW") != std::string::npos ||
        r.find("Lo") != std::string::npos ||
        r.find("LO") != std::string::npos)
    {
        out.isLogicLow = true;
        out.rawValue   = "Low";
        return;
    }
    if (r.find("----") != std::string::npos ||
        r.find("- - - -") != std::string::npos)
    {
        out.isLogicUndef = true;
        out.rawValue     = "----";
        return;
    }

    // --- numeric value ---
    // Pattern: optional sign, digits, optional decimal, optional suffix
    // Unit suffix letters (k, m, u, z) may be embedded in value.
    std::regex numPat(R"(([-+]?[0-9]*\.?[0-9]+[kKmMuUzZ]?))",
                      std::regex::ECMAScript);
    std::smatch m;
    if (std::regex_search(r, m, numPat))
    {
        out.rawValue = m[1].str();
        // everything after the matched number is units
        size_t endPos = (size_t)m.position(1) + m.length(1);
        std::string unitStr = Trim(r.substr(endPos));
        // Fix mangled degree symbol common in 7-bit ASCII transfer
        size_t dpos = unitStr.find("^C");
        while (dpos != std::string::npos)
        {
            unitStr.replace(dpos, 2, "\xC2\xB0""C"); // UTF-8 °C
            dpos = unitStr.find("^C", dpos + 3);
        }
        out.units = unitStr;
    }
    else
    {
        // Fallback: treat entire string as raw value
        out.rawValue = r;
    }
}

DmmReading DmmParser::Parse(const std::string& line)
{
    DmmReading out;
    out.rawLine = line;

    std::string clean = Trim(line);
    if (clean.empty()) return out;

    char firstByte = clean[0];
    if (!IsKnownModeCode(firstByte)) return out;

    out.valid    = true;
    out.modeCode = std::string(1, firstByte);
    out.modeName = MapMode(firstByte);

    // Rest of the line (skip mode byte and optional sub-range byte)
    std::string rest = (clean.size() > 1) ? clean.substr(1) : "";
    ParseValueAndUnits(rest, out);

    return out;
}
