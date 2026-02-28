#pragma once
// ============================================================
//  Protek506Logger — DmmParser.h
//  Parses raw ASCII lines from the Protek 506.
//
//  Protocol (from manual, section 7, "DATA FORMAT", page 45):
//
//  The meter sends a plain ASCII line terminated by CR (0x0D)
//  in response to each LF (0x0A) trigger from the host.
//
//  Line format:
//    <MODE_WORD> <SP> <VALUE> [<SP> <UNITS>] <CR>
//
//  MODE_WORD is a multi-character ASCII token (always uppercase):
//    DC    DC voltage or current
//    AC    AC voltage or current
//    RES   Resistance
//    BUZ   Continuity (buzzer)
//    DIOD  Diode test
//    LOG   Logic level
//    FR    Frequency
//    CAP   Capacitance
//    IND   Inductance
//    TEMP  Temperature
//
//  VALUE is a numeric string (e.g. "3.999", "-0.001") or a
//  special token: "OL" (overload), "SHORT", "OPEN", "HIGH",
//  "LOW", "GOOD", "----".
//
//  UNITS is a short ASCII string, e.g. "V", "mV", "MOH", "KOH",
//  "OH", "MHz", "kHz", "uF", "nF", "C", "F".
//
//  The meter sends data after receiving '\n' (0x0A).
//
//  Serial settings: 1200 baud, 7 data bits, 2 stop bits, no parity.
// ============================================================
#include <string>

struct DmmReading
{
    bool        valid        = false;
    std::string modeCode;       // full mode word as sent, e.g. "DC", "RES", "TEMP"
    std::string modeName;       // friendly display name, e.g. "DC", "FREQ", "TEMP"
    std::string rawValue;       // e.g. "3.141", "OL", "High", "----"
    std::string units;          // e.g. "V", "kΩ", "°C"  (UTF-8)
    std::string rawLine;        // original line for logging/debug
    bool        isOverload   = false;
    bool        isOpen       = false;
    bool        isShort      = false;
    bool        isLogicHigh  = false;
    bool        isLogicLow   = false;
    bool        isLogicUndef = false;
};

class DmmParser
{
public:
    DmmParser();

    // Parse a raw line (CR terminator already stripped) from the meter.
    // Returns a DmmReading; check .valid before using.
    DmmReading Parse(const std::string& line);

    // Returns true if the first byte of the line could begin a valid
    // mode word.  Used as a cheap pre-filter before full parsing.
    static bool IsKnownModeCode(char c);

private:
    // Returns the friendly display name for a mode word, e.g. "FR" → "FREQ".
    std::string MapMode(const std::string& word) const;

    void ParseValueAndUnits(const std::string& rest, DmmReading& out) const;
};
