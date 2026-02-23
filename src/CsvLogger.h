#pragma once
// ============================================================
//  Protek506Logger — CsvLogger.h
//  Writes DMM readings to a CSV file.
//  Header is written only when the file is new/empty.
//  Columns: date, time, mode, reading, units
// ============================================================
#include <string>
#include <fstream>

class CsvLogger
{
public:
    CsvLogger();
    ~CsvLogger();

    bool Open(const std::string& filePath);
    void Close();
    bool IsOpen() const;

    void Write(const std::string& date,
               const std::string& time,
               const std::string& mode,
               const std::string& reading,
               const std::string& units);

    // fix #12: Returns false if the last Write() failed (e.g. disk full).
    // The file is closed on error; IsOpen() will return false afterward.
    bool WriteOk() const { return m_writeOk; }

    std::string FilePath()  const { return m_filePath; }
    std::string LastError() const { return m_lastError; }
    long        RowCount()  const { return m_rowCount; }

private:
    std::ofstream m_file;
    std::string   m_filePath;
    std::string   m_lastError;
    long          m_rowCount;
    bool          m_writeOk;   // fix #12: tracks post-open write health

    // CSV-escape a field (wrap in quotes if needed)
    static std::string Escape(const std::string& field);
};
