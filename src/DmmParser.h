#pragma once
// ============================================================
//  Protek506Logger — DmmParser.h
//  Parses raw ASCII lines from the Protek 506.
//
//  Protocol (from manual, section 8, "DATA FORMAT"):
//    Byte 1  : mode code  D=DC, A=AC, R=RES, L=DIO/Logic,
//                         F=FREQ, C=CAP, I=IND, T=TEMP
//    Byte 2  : range/sub-mode code (C = auto/common)
//    Byte 3+ : reading value string
//    Last    : units string
//    Terminator: CR (0x0D)
//
//  The meter sends data after receiving '\n' (0x0A).
// ============================================================
#include <string>

struct DmmReading
{
    bool        valid     = false;
    std::string modeCode;     // raw 1st byte, e.g. "D"
    std::string modeName;     // friendly, e.g. "DC"
    std::string rawValue;     // e.g. "3.141", "OL", "High", "----"
    std::string units;        // e.g. "V", "kΩ", "°C"
    std::string rawLine;      // original line for logging/debug
    bool        isOverload  = false;
    bool        isOpen      = false;
    bool        isShort     = false;
    bool        isLogicHigh = false;
    bool        isLogicLow  = false;
    bool        isLogicUndef= false;
};

class DmmParser
{
public:
    DmmParser();

    // Parse a raw line (without terminator) received from the meter.
    // Returns a DmmReading; check .valid before using.
    DmmReading Parse(const std::string& line);

    // True if the first byte is a known mode code.
    static bool IsKnownModeCode(char c);

private:
    std::string MapMode(char code) const;
    void        ParseValueAndUnits(const std::string& rest,
                                   DmmReading& out) const;
};
