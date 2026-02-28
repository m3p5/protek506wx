// ============================================================
//  Protek506Logger — DmmParser.cpp
//
//  Protek 506 DATA FORMAT (manual section 7, page 45):
//
//  The meter transmits a plain ASCII line terminated by CR (0x0D).
//  The line consists of a multi-character MODE WORD followed by a
//  space, then the VALUE, then (for numeric readings) a space and
//  a UNITS string.  The final byte before CR is the last char of
//  the units (or the last char of a special value token like "OL").
//
//  Mode words as sent by the meter (from the data table):
//    "DC"   DC voltage / current
//    "AC"   AC voltage / current
//    "RES"  Resistance
//    "BUZ"  Continuity (buzzer mode)
//    "DIOD" Diode test
//    "LOG"  Logic level
//    "FR"   Frequency
//    "CAP"  Capacitance
//    "IND"  Inductance
//    "TEMP" Temperature
//
//  Example lines (bytes before the CR):
//    "DC  3.999 V"        → mode=DC,  value=3.999, units=V
//    "AC  0L"             → mode=AC,  value=OL,    units=""
//    "RES 3.999 MOH"      → mode=RES, value=3.999, units=MOH
//    "BUZ SHORT"          → mode=BUZ, value=SHORT, units=""
//    "LOG LOW"            → mode=LOG, value=Low,   units=""
//    "FR  9.999 MHz"      → mode=FR,  value=9.999, units=MHz
//    "CAP 9.999 uF"       → mode=CAP, value=9.999, units=uF
//    "IND 0L"             → mode=IND, value=OL,    units=""
//    "TEMP 0802 5 C"      → mode=TEMP,value=0802.5,units=C
// ============================================================
#include "DmmParser.h"
#include <cctype>
#include <algorithm>
#include <cstring>

DmmParser::DmmParser() {}

// ----------------------------------------------------------------
// Known mode words (all uppercase, as sent by the meter).
// The meter always sends a space after the mode word before the
// value, so we match the token before the first space.
// ----------------------------------------------------------------
static const struct { const char* word; const char* friendly; } s_modes[] =
{
    { "DC",   "DC"          },
    { "AC",   "AC"          },
    { "RES",  "RES"         },
    { "BUZ",  "CONT"        },   // continuity / buzzer
    { "DIOD", "DIODE"       },
    { "LOG",  "LOGIC"       },
    { "FR",   "FREQ"        },
    { "CAP",  "CAP"         },
    { "IND",  "IND"         },
    { "TEMP", "TEMP"        },
};
static const int s_modeCount = static_cast<int>(sizeof(s_modes) / sizeof(s_modes[0]));

// ----------------------------------------------------------------
// IsKnownModeCode — kept for API compatibility; tests the first
// character only (used by the header guard in Parse()).
// ----------------------------------------------------------------
bool DmmParser::IsKnownModeCode(char c)
{
    // First chars of all known mode words
    static const char* firsts = "DARBLFCIT";
    return strchr(firsts, static_cast<unsigned char>(c)) != nullptr;
}

// ----------------------------------------------------------------
// MapMode — now takes the full mode word string.
// ----------------------------------------------------------------
std::string DmmParser::MapMode(const std::string& word) const
{
    for (int i = 0; i < s_modeCount; ++i)
        if (word == s_modes[i].word)
            return s_modes[i].friendly;
    return word;   // pass-through for unknown/future modes
}

// ----------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------
static std::string Trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string ToUpper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return s;
}

static std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

// ----------------------------------------------------------------
// ParseValueAndUnits
// Receives the part of the line AFTER the mode-word-plus-space has
// been removed.  Splits on the last space to separate value from
// units; handles all special tokens (OL, SHORT, OPEN, etc.).
// ----------------------------------------------------------------
void DmmParser::ParseValueAndUnits(const std::string& rest, DmmReading& out) const
{
    std::string r = Trim(rest);
    if (r.empty()) return;

    // Split: value = everything up to last space, units = last token.
    // For readings with no units (OL, SHORT, OPEN, HIGH, LOW, ----)
    // there will be no space, so the whole string is the value.
    size_t lastSpace = r.find_last_of(' ');
    if (lastSpace != std::string::npos && lastSpace < r.size() - 1)
    {
        std::string valPart  = Trim(r.substr(0, lastSpace));
        std::string unitPart = Trim(r.substr(lastSpace + 1));

        // Normalise temperature unit notations
        if (unitPart == "^C" || unitPart == "C")  unitPart = "\xc2\xb0""C";  // °C (UTF-8)
        if (unitPart == "^F" || unitPart == "F")  unitPart = "\xc2\xb0""F";  // °F (UTF-8)

        // The meter sends resistance units as "OH", "KOH", "MOH".
        // Map to readable Ω forms.
        if (unitPart == "OH")  unitPart = "\xce\xa9";         // Ω
        if (unitPart == "KOH") unitPart = "k\xce\xa9";        // kΩ
        if (unitPart == "MOH") unitPart = "M\xce\xa9";        // MΩ

        out.rawValue = valPart;
        out.units    = unitPart;
    }
    else
    {
        // No space — entire string is a bare token (OL, SHORT, etc.)
        out.rawValue = r;
        out.units    = "";
    }

    // Classify special values (case-insensitive substring match)
    std::string v_lower = ToLower(out.rawValue);

    if (v_lower == "ol" || v_lower.find("ol") == 0)
    {
        out.isOverload = true;
        out.rawValue   = "OL";
        out.units      = "";
    }
    else if (v_lower == "open")
    {
        out.isOpen   = true;
        out.rawValue = "OPEN";
        out.units    = "";
    }
    else if (v_lower == "short")
    {
        out.isShort  = true;
        out.rawValue = "SHORT";
        out.units    = "";
    }
    else if (v_lower == "good")
    {
        out.rawValue = "GOOD";
        out.units    = "";
    }
    else if (v_lower == "high" || v_lower == "hi")
    {
        out.isLogicHigh = true;
        out.rawValue    = "High";
        out.units       = "";
    }
    else if (v_lower == "low" || v_lower == "lo")
    {
        out.isLogicLow = true;
        out.rawValue   = "Low";
        out.units      = "";
    }
    else if (v_lower.find("----") != std::string::npos)
    {
        out.isLogicUndef = true;
        out.rawValue     = "----";
        out.units        = "";
    }
}

// ----------------------------------------------------------------
// Parse
//
// The incoming line (CR already stripped by ReadLine) looks like:
//
//   "DC  3.999 V"
//   "RES 3.999 MOH"
//   "AC  0L"
//   "TEMP 0802 5 C"
//
// Algorithm:
//   1. Trim the line.
//   2. Find the first space — everything before it is the mode word.
//   3. Look up the mode word; reject the line if unknown.
//   4. Pass everything after the mode word + space to ParseValueAndUnits.
// ----------------------------------------------------------------
DmmReading DmmParser::Parse(const std::string& line)
{
    DmmReading out;
    out.rawLine = line;

    std::string clean = Trim(line);
    if (clean.empty()) return out;

    // Quick first-character check to skip obvious non-readings cheaply
    if (!IsKnownModeCode(clean[0])) return out;

    // Split mode word from the rest at the first space
    size_t spacePos = clean.find(' ');
    if (spacePos == std::string::npos)
    {
        // No space at all — line is just a mode word with no value.
        // Treat as invalid (meter shouldn't send this, but be safe).
        return out;
    }

    std::string modeWord = ToUpper(clean.substr(0, spacePos));
    std::string rest     = clean.substr(spacePos + 1);   // value [space units]

    // Verify it's a recognised mode word
    std::string friendly;
    bool found = false;
    for (int i = 0; i < s_modeCount; ++i)
    {
        if (modeWord == s_modes[i].word)
        {
            friendly = s_modes[i].friendly;
            found    = true;
            break;
        }
    }
    if (!found) return out;   // unknown mode word → discard

    out.valid    = true;
    out.modeCode = modeWord;
    out.modeName = friendly;

    ParseValueAndUnits(rest, out);
    return out;
}
