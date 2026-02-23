#pragma once
// ============================================================
//  Protek506Logger — SerialPort.h
//  Thin cross-platform serial port wrapper (no external libs).
//  Windows  : Win32 HANDLE / CreateFile
//  POSIX    : POSIX termios (macOS + Linux)
// ============================================================
#include <string>
#include <vector>
#include <cstdint>

struct PortInfo
{
    std::string device;       // e.g. "COM3" or "/dev/ttyUSB0"
    std::string description;
    std::string manufacturer;
};

class SerialPort
{
public:
    SerialPort();
    ~SerialPort();

    // --- port enumeration ---
    static std::vector<PortInfo> ListPorts();

    // --- connection ---
    bool Open(const std::string& device,
              int    baudRate    = 1200,
              int    dataBits    = 7,
              int    stopBits    = 2,
              char   parity      = 'N',
              int    timeoutMs   = 1000);
    void Close();
    bool IsOpen() const;

    // --- I/O ---
    // Write raw bytes; returns bytes written or -1 on error.
    int  Write(const uint8_t* data, int len);
    int  WriteByte(uint8_t b);

    // Read up to maxLen bytes; returns bytes read or -1 on error.
    int  Read(uint8_t* buf, int maxLen);

    // Read until terminator byte or timeout; terminatorIncluded=false strips it.
    // Returns the line (without terminator) or empty string on timeout/error.
    std::string ReadLine(uint8_t terminator = '\r', int maxBytes = 256);

    std::string LastError() const { return m_lastError; }

    // Clear a previous error (e.g. after the caller has handled it).
    void ClearError() { m_lastError.clear(); }

private:
    void SetError(const std::string& msg);

#ifdef _WIN32
    void* m_handle;   // HANDLE
#else
    int   m_fd;
    int   m_timeoutMs;  // stored for select()-based ReadLine (fix #7)
#endif
    std::string m_lastError;
    bool        m_open;
};
