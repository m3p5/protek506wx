// ============================================================
//  Protek506Logger — CsvLogger.cpp
// ============================================================
#include "CsvLogger.h"
#include <sys/stat.h>

CsvLogger::CsvLogger() : m_rowCount(0) {}
CsvLogger::~CsvLogger() { Close(); }

bool CsvLogger::Open(const std::string& filePath)
{
    Close();
    m_filePath = filePath;
    m_rowCount = 0;

    // Check if file exists and is non-empty
    bool needHeader = true;
    struct stat st;
    if (stat(filePath.c_str(), &st) == 0 && st.st_size > 0)
        needHeader = false;

    m_file.open(filePath, std::ios::app);
    if (!m_file.is_open())
    {
        m_lastError = "Cannot open file: " + filePath;
        return false;
    }

    if (needHeader)
        m_file << "date,time,mode,reading,units\n";

    m_file.flush();
    return true;
}

void CsvLogger::Close()
{
    if (m_file.is_open())
        m_file.close();
}

bool CsvLogger::IsOpen() const
{
    return m_file.is_open();
}

void CsvLogger::Write(const std::string& date,
                      const std::string& time,
                      const std::string& mode,
                      const std::string& reading,
                      const std::string& units)
{
    if (!m_file.is_open()) return;

    m_file << Escape(date)    << ","
           << Escape(time)    << ","
           << Escape(mode)    << ","
           << Escape(reading) << ","
           << Escape(units)   << "\n";
    m_file.flush();
    ++m_rowCount;
}

std::string CsvLogger::Escape(const std::string& field)
{
    // If the field contains comma, quote, or newline — wrap in double-quotes
    bool needsQuote = (field.find(',')  != std::string::npos ||
                       field.find('"')  != std::string::npos ||
                       field.find('\n') != std::string::npos);
    if (!needsQuote) return field;

    std::string out = "\"";
    for (char c : field)
    {
        if (c == '"') out += "\"\""; // escape embedded quotes
        else          out += c;
    }
    out += "\"";
    return out;
}
