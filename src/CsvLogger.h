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

    std::string FilePath()  const { return m_filePath; }
    std::string LastError() const { return m_lastError; }
    long        RowCount()  const { return m_rowCount; }

private:
    std::ofstream m_file;
    std::string   m_filePath;
    std::string   m_lastError;
    long          m_rowCount;

    // CSV-escape a field (wrap in quotes if needed)
    static std::string Escape(const std::string& field);
};
