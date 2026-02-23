// ============================================================
//  Protek506Logger — SerialPort.cpp
//  Cross-platform RS-232 implementation.
//  Protek 506 spec: 1200 baud, 7 data bits, 2 stop bits, no parity.
// ============================================================
#include "SerialPort.h"
#include <algorithm>
#include <sstream>

// ----------------------------------------------------------------
#ifdef _WIN32
// ========================= WINDOWS ==============================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#pragma comment(lib, "setupapi.lib")

SerialPort::SerialPort() : m_handle(INVALID_HANDLE_VALUE), m_open(false) {}

SerialPort::~SerialPort() { Close(); }

// ---------- enumerate COM ports via SetupAPI ----------
std::vector<PortInfo> SerialPort::ListPorts()
{
    std::vector<PortInfo> result;

    HDEVINFO hDevInfo = SetupDiGetClassDevs(
        &GUID_DEVCLASS_PORTS, nullptr, nullptr,
        DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE)
        return result;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i)
    {
        char portName[256]  = {};
        char desc[256]      = {};
        char mfg[256]       = {};

        // Friendly name (description)
        SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData,
            SPDRP_FRIENDLYNAME, nullptr,
            (PBYTE)desc, sizeof(desc), nullptr);

        // Manufacturer
        SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData,
            SPDRP_MFG, nullptr,
            (PBYTE)mfg, sizeof(mfg), nullptr);

        // Port name from registry key
        HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfoData,
            DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hKey != INVALID_HANDLE_VALUE)
        {
            DWORD size = sizeof(portName);
            RegQueryValueExA(hKey, "PortName", nullptr, nullptr,
                (LPBYTE)portName, &size);
            RegCloseKey(hKey);
        }

        if (portName[0] != '\0')
        {
            PortInfo pi;
            pi.device       = portName;
            pi.description  = desc;
            pi.manufacturer = mfg;
            result.push_back(pi);
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);

    std::sort(result.begin(), result.end(),
        [](const PortInfo& a, const PortInfo& b){ return a.device < b.device; });
    return result;
}

bool SerialPort::Open(const std::string& device, int baudRate,
                      int dataBits, int stopBits, char parity, int timeoutMs)
{
    Close();
    // Windows requires "\\\\.\\" prefix for ports > COM9
    std::string path = "\\\\.\\" + device;

    m_handle = CreateFileA(path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);

    if (m_handle == INVALID_HANDLE_VALUE)
    {
        SetError("CreateFile failed: error " + std::to_string(GetLastError()));
        return false;
    }

    // --- DCB ---
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_handle, &dcb))
    {
        SetError("GetCommState failed");
        Close(); return false;
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = (BYTE)dataBits;
    dcb.StopBits = (stopBits == 2) ? TWOSTOPBITS : ONESTOPBIT;
    dcb.Parity   = (parity == 'E') ? EVENPARITY
                 : (parity == 'O') ? ODDPARITY
                 :                   NOPARITY;
    dcb.fParity  = (parity != 'N') ? TRUE : FALSE;
    dcb.fBinary  = TRUE;

    if (!SetCommState(m_handle, &dcb))
    {
        SetError("SetCommState failed");
        Close(); return false;
    }

    // --- timeouts ---
    COMMTIMEOUTS ct = {};
    ct.ReadIntervalTimeout         = MAXDWORD;
    ct.ReadTotalTimeoutMultiplier  = MAXDWORD;
    ct.ReadTotalTimeoutConstant    = (DWORD)timeoutMs;
    ct.WriteTotalTimeoutMultiplier = 0;
    ct.WriteTotalTimeoutConstant   = timeoutMs;
    SetCommTimeouts(m_handle, &ct);

    m_open = true;
    return true;
}

void SerialPort::Close()
{
    if (m_open && m_handle != INVALID_HANDLE_VALUE)
        CloseHandle(m_handle);
    m_handle = INVALID_HANDLE_VALUE;
    m_open   = false;
}

bool SerialPort::IsOpen() const { return m_open; }

int SerialPort::Write(const uint8_t* data, int len)
{
    if (!m_open) return -1;
    DWORD written = 0;
    if (!WriteFile(m_handle, data, (DWORD)len, &written, nullptr))
    {
        SetError("WriteFile failed");
        return -1;
    }
    return (int)written;
}

int SerialPort::WriteByte(uint8_t b)
{
    return Write(&b, 1);
}

int SerialPort::Read(uint8_t* buf, int maxLen)
{
    if (!m_open) return -1;
    DWORD got = 0;
    if (!ReadFile(m_handle, buf, (DWORD)maxLen, &got, nullptr))
    {
        SetError("ReadFile failed");
        return -1;
    }
    return (int)got;
}

std::string SerialPort::ReadLine(uint8_t terminator, int maxBytes)
{
    m_lastError.clear();    // clear stale error; genuine errors set it below
    std::string line;
    line.reserve(32);
    uint8_t ch;
    while ((int)line.size() < maxBytes)
    {
        int n = Read(&ch, 1);
        if (n < 0) return line;     // error already stored in m_lastError
        if (n == 0) break;          // timeout — m_lastError stays empty
        if (ch == terminator) break;
        line += (char)ch;
    }
    return line;
}

void SerialPort::SetError(const std::string& msg)
{
    m_lastError = msg;
}

// ================================================================
#else
// ========================= POSIX (macOS / Linux) ================
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>

#ifdef __APPLE__
  #include <sys/param.h>
  #include <IOKit/IOKitLib.h>
  #include <IOKit/serial/IOSerialKeys.h>
  #include <CoreFoundation/CoreFoundation.h>
#endif

SerialPort::SerialPort() : m_fd(-1), m_timeoutMs(1000), m_open(false) {}
SerialPort::~SerialPort() { Close(); }

// ---------- POSIX port enumeration ----------
std::vector<PortInfo> SerialPort::ListPorts()
{
    std::vector<PortInfo> result;

#ifdef __APPLE__
    // Use IOKit to find serial ports on macOS
    CFMutableDictionaryRef matchDict =
        IOServiceMatching(kIOSerialBSDServiceValue);
    if (!matchDict) return result;

    CFDictionarySetValue(matchDict,
        CFSTR(kIOSerialBSDTypeKey),
        CFSTR(kIOSerialBSDAllTypes));

    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(
        kIOMainPortDefault, matchDict, &iter);
    if (kr != KERN_SUCCESS) return result;

    io_object_t obj;
    while ((obj = IOIteratorNext(iter)) != 0)
    {
        auto getCFStr = [&](CFStringRef key) -> std::string {
            CFTypeRef val = IORegistryEntryCreateCFProperty(
                obj, key, kCFAllocatorDefault, 0);
            if (!val) return "";
            char buf[512] = {};
            CFStringGetCString((CFStringRef)val, buf, sizeof(buf),
                               kCFStringEncodingUTF8);
            CFRelease(val);
            return buf;
        };

        PortInfo pi;
        pi.device       = getCFStr(CFSTR(kIOCalloutDeviceKey));
        pi.description  = getCFStr(CFSTR(kIOTTYDeviceKey));
        pi.manufacturer = getCFStr(CFSTR("USB Vendor Name"));
        if (!pi.device.empty())
            result.push_back(pi);

        IOObjectRelease(obj);
    }
    IOObjectRelease(iter);

#else
    // Linux: scan /dev for ttyUSB*, ttyACM*, ttyS*
    static const char* prefixes[] = { "ttyUSB", "ttyACM", "ttyS", nullptr };
    DIR* dir = opendir("/dev");
    if (!dir) return result;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr)
    {
        std::string name = ent->d_name;
        for (int i = 0; prefixes[i]; ++i)
        {
            if (name.substr(0, strlen(prefixes[i])) == prefixes[i])
            {
                PortInfo pi;
                pi.device       = std::string("/dev/") + name;
                pi.description  = name;
                pi.manufacturer = "";

                // Try to read manufacturer from sysfs
                // e.g. /sys/class/tty/ttyUSB0/device/../manufacturer
                std::string sysPath =
                    std::string("/sys/class/tty/") + name +
                    "/device/../manufacturer";
                FILE* f = fopen(sysPath.c_str(), "r");
                if (!f)
                {
                    sysPath = std::string("/sys/class/tty/") + name +
                              "/device/../../manufacturer";
                    f = fopen(sysPath.c_str(), "r");
                }
                if (f)
                {
                    char buf[256] = {};
                    if (fgets(buf, sizeof(buf), f))
                    {
                        pi.manufacturer = buf;
                        // strip trailing newline
                        while (!pi.manufacturer.empty() &&
                               (pi.manufacturer.back() == '\n' ||
                                pi.manufacturer.back() == '\r'))
                            pi.manufacturer.pop_back();
                    }
                    fclose(f);
                }
                result.push_back(pi);
                break;
            }
        }
    }
    closedir(dir);
#endif

    std::sort(result.begin(), result.end(),
        [](const PortInfo& a, const PortInfo& b){ return a.device < b.device; });
    return result;
}

static speed_t BaudToSpeed(int baud)
{
    switch (baud)
    {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B1200;
    }
}

bool SerialPort::Open(const std::string& device, int baudRate,
                      int dataBits, int stopBits, char parity, int timeoutMs)
{
    Close();
    m_timeoutMs = timeoutMs;    // fix #7: store for use in ReadLine()

    m_fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0)
    {
        SetError(std::string("open() failed: ") + strerror(errno));
        return false;
    }

    // Switch to blocking mode; timing is handled by select() in ReadLine
    int flags = fcntl(m_fd, F_GETFL, 0);
    fcntl(m_fd, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty = {};
    if (tcgetattr(m_fd, &tty) != 0)
    {
        SetError(std::string("tcgetattr failed: ") + strerror(errno));
        Close(); return false;
    }

    speed_t spd = BaudToSpeed(baudRate);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    tty.c_cflag |= (CLOCAL | CREAD);

    // Data bits
    tty.c_cflag &= ~CSIZE;
    switch (dataBits)
    {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        default:tty.c_cflag |= CS8; break;
    }

    // Stop bits
    if (stopBits == 2)
        tty.c_cflag |= CSTOPB;
    else
        tty.c_cflag &= ~CSTOPB;

    // Parity
    if (parity == 'E')
    {
        tty.c_cflag |= PARENB;
        tty.c_cflag &= ~PARODD;
        tty.c_iflag |= INPCK;
    }
    else if (parity == 'O')
    {
        tty.c_cflag |= PARENB;
        tty.c_cflag |= PARODD;
        tty.c_iflag |= INPCK;
    }
    else
    {
        tty.c_cflag &= ~PARENB;
        tty.c_iflag &= ~INPCK;
    }

    // Raw mode — no canonical processing, no signals, no software flow
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;

    // fix #7: VMIN=0 VTIME=0 → fully non-blocking reads.
    // All timeout logic is handled by select() in ReadLine() so that:
    //  (a) the full inter-character gap at 1200 baud is tolerated, and
    //  (b) the overall line timeout still applies if the meter is silent.
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(m_fd, TCSANOW, &tty) != 0)
    {
        SetError(std::string("tcsetattr failed: ") + strerror(errno));
        Close(); return false;
    }

    tcflush(m_fd, TCIOFLUSH);
    m_open = true;
    return true;
}

void SerialPort::Close()
{
    if (m_open && m_fd >= 0)
        ::close(m_fd);
    m_fd   = -1;
    m_open = false;
}

bool SerialPort::IsOpen() const { return m_open; }

int SerialPort::Write(const uint8_t* data, int len)
{
    if (!m_open) return -1;
    ssize_t n = ::write(m_fd, data, (size_t)len);
    if (n < 0) { SetError(std::string("write() failed: ") + strerror(errno)); return -1; }
    return (int)n;
}

int SerialPort::WriteByte(uint8_t b)
{
    return Write(&b, 1);
}

int SerialPort::Read(uint8_t* buf, int maxLen)
{
    if (!m_open) return -1;
    ssize_t n = ::read(m_fd, buf, (size_t)maxLen);
    if (n < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        SetError(std::string("read() failed: ") + strerror(errno));
        return -1;
    }
    return (int)n;
}

// fix #7: select()-based ReadLine.
//
// The old implementation called Read() byte-by-byte with VTIME-based
// blocking.  At 1200 baud a single character takes ~8 ms, and the meter
// may take tens of milliseconds to respond after receiving '\n'.
// With VMIN=0 / VTIME=N each individual Read() timed out after N tenths
// of a second, so a slow first byte would cause an early empty return
// and the rest of the packet would be silently discarded.
//
// The new implementation uses select() with the full configured timeout
// for every byte wait.  This correctly tolerates the meter's response
// latency while still returning promptly on genuine silence (timeout).
// A genuine I/O error is stored in m_lastError; a clean timeout clears
// m_lastError so the caller can distinguish the two cases.
std::string SerialPort::ReadLine(uint8_t terminator, int maxBytes)
{
    m_lastError.clear();    // clear stale errors before each line read

    std::string line;
    line.reserve(32);

    uint8_t ch;
    while ((int)line.size() < maxBytes)
    {
        // Wait up to m_timeoutMs for the next byte
        fd_set rdset;
        FD_ZERO(&rdset);
        FD_SET(m_fd, &rdset);

        struct timeval tv;
        tv.tv_sec  =  m_timeoutMs / 1000;
        tv.tv_usec = (m_timeoutMs % 1000) * 1000;

        int ready = ::select(m_fd + 1, &rdset, nullptr, nullptr, &tv);
        if (ready < 0)
        {
            if (errno == EINTR) continue;   // interrupted by signal — retry
            SetError(std::string("select() failed: ") + strerror(errno));
            return line;
        }
        if (ready == 0)
        {
            // Timeout — not an error; meter simply hasn't responded yet.
            // m_lastError stays empty so the caller can tell it apart from
            // a real I/O error (fix #3 in ReaderThread).
            return line;
        }

        ssize_t n = ::read(m_fd, &ch, 1);
        if (n < 0)
        {
            SetError(std::string("read() failed: ") + strerror(errno));
            return line;
        }
        if (n == 0) break;              // EOF / device removed

        if (ch == terminator) break;
        line += (char)ch;
    }
    return line;
}

void SerialPort::SetError(const std::string& msg)
{
    m_lastError = msg;
}

#endif // _WIN32
