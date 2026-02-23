// ============================================================
//  Protek506Logger — DmmParser.cpp
//
//  Fixes applied:
//  #4  Sub-range byte (byte 2 per the manual's data format
//      table) is now skipped before value parsing.  Previously
//      only the mode byte was stripped, so the sub-range
//      character was passed into ParseValueAndUnits() and could
//      corrupt numeric extraction.
//  #5  The std::regex object is now static const so it is
//      compiled once, not on every Parse() call.
//  #6  Logic-level detection now matches whole tokens (CiEquals)
//      anchored to the full trimmed string, preventing false
//      positives such as "Hi" matching inside "kHz" or "HIGH"
//      matching inside other unit strings.
// ============================================================
#include "DmmParser.h"
#include <cctype>
#include <algorithm>
#include <cstring>
#include <regex>

// Known mode first-byte characters per manual section 8
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

// Trim leading/trailing whitespace
static std::string Trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Case-insensitive whole-string equality (fix #6: avoids substring
// matches that caused false positives on "Hi" inside "kHz" etc.)
static bool CiEquals(const std::string& a, const char* b)
{
    std::string au = a, bu = b;
    std::transform(au.begin(), au.end(), au.begin(), ::toupper);
    std::transform(bu.begin(), bu.end(), bu.begin(), ::toupper);
    return au == bu;
}

// Case-insensitive prefix check
static bool CiStartsWith(const std::string& s, const char* prefix)
{
    size_t plen = strlen(prefix);
    if (s.size() < plen) return false;
    for (size_t i = 0; i < plen; ++i)
        if (toupper((unsigned char)s[i]) != toupper((unsigned char)prefix[i]))
            return false;
    return true;
}

void DmmParser::ParseValueAndUnits(const std::string& rest,
                                   DmmReading& out) const
{
    std::string r = Trim(rest);
    if (r.empty()) return;

    // --- overload variants (.OL, -OL, OL) ---
    // Check before numeric pattern since "OL" contains no digits.
    if (CiEquals(r, "OL") || CiEquals(r, ".OL") || CiEquals(r, "-OL")
        || CiStartsWith(r, "OL ") || CiStartsWith(r, ".OL ") || CiStartsWith(r, "-OL "))
    {
        out.isOverload = true;
        out.rawValue   = "OL";
        // Any text after "OL" (possibly units) — find where OL ends
        size_t olStart = (r[0] == '.' || r[0] == '-') ? 1 : 0;
        size_t after   = olStart + 2; // skip "OL"
        if (after < r.size())
            out.units = Trim(r.substr(after));
        return;
    }

    // --- continuity / diode special readings ---
    if (CiStartsWith(r, "OPEN"))
    {
        out.isOpen   = true;
        out.rawValue = "OPEN";
        return;
    }
    if (CiStartsWith(r, "SHORT"))
    {
        out.isShort  = true;
        out.rawValue = "SHORT";
        return;
    }

    // fix #6: Logic-level detection uses whole-string equality so
    // "Hi" does NOT match inside "kHz", "MHz", etc.
    // The meter's logic mode outputs exactly "Hi", "Lo", or "----".
    if (CiEquals(r, "Hi") || CiEquals(r, "High"))
    {
        out.isLogicHigh = true;
        out.rawValue    = "High";
        return;
    }
    if (CiEquals(r, "Lo") || CiEquals(r, "Low"))
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
    // fix #5: static const — regex compiled once, not per call.
    static const std::regex numPat(
        R"(([-+]?[0-9]*\.?[0-9]+[kKmMuUzZ]?))",
        std::regex::ECMAScript | std::regex::optimize);

    std::smatch m;
    if (std::regex_search(r, m, numPat))
    {
        out.rawValue = m[1].str();
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
        // Fallback: treat entire trimmed string as raw value
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

    // fix #4: Skip BOTH byte 1 (mode) AND byte 2 (sub-range/function
    // code) before parsing the value.  The manual's data format table
    // (section 8) shows byte 2 is always a sub-range qualifier, e.g.:
    //   'C' for auto-range DC V, 'E' for certain resistance ranges, etc.
    // Previously only byte 1 was stripped, so 'C'/'E' etc. was fed into
    // ParseValueAndUnits() and broke numeric pattern matching.
    std::string rest = (clean.size() > 2) ? clean.substr(2) : "";
    ParseValueAndUnits(rest, out);

    return out;
}
