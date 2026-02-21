// ============================================================
//  Protek506Logger — ReaderThread.cpp
// ============================================================
#include "ReaderThread.h"
#include <wx/datetime.h>

// Event type definitions
wxDEFINE_EVENT(EVT_DMM_READING, wxCommandEvent);
wxDEFINE_EVENT(EVT_DMM_ERROR,   wxCommandEvent);

// We package a DmmReading in the event's string as a pipe-delimited
// record so we don't need a custom event class:
//   modeName | rawValue | units | rawLine
// The main frame unpacks it.
static wxString PackReading(const DmmReading& r)
{
    // Timestamp
    wxDateTime now = wxDateTime::Now();
    wxString ts = now.Format("%Y-%m-%d|%H:%M:%S.") +
                  wxString::Format("%03ld", now.GetMillisecond());

    return wxString::Format("%s|%s|%s|%s|%s",
        ts,
        wxString::FromUTF8(r.modeName.c_str()),
        wxString::FromUTF8(r.rawValue.c_str()),
        wxString::FromUTF8(r.units.c_str()),
        wxString::FromUTF8(r.rawLine.c_str()));
}

// ----------------------------------------------------------------
ReaderThread::ReaderThread(wxEvtHandler* sink,
                           const std::string& port,
                           int pollDelayMs)
    : wxThread(wxTHREAD_DETACHED)
    , m_sink(sink)
    , m_port(port)
    , m_pollDelayMs(pollDelayMs)
    , m_stop(false)
{}

ReaderThread::~ReaderThread() {}

void ReaderThread::RequestStop()
{
    m_stop = true;
}

wxThread::ExitCode ReaderThread::Entry()
{
    // Open the serial port
    // Protek 506: 1200 baud, 7 data bits, 2 stop bits, no parity
    if (!m_serial.Open(m_port, 1200, 7, 2, 'N', 1000))
    {
        PostError(wxString::Format("Cannot open port %s: %s",
            m_port, m_serial.LastError()));
        return (ExitCode)1;
    }

    while (!m_stop && !TestDestroy())
    {
        // Trigger meter to send reading (send '\n')
        m_serial.WriteByte('\n');

        // Read response line (CR terminated)
        std::string line = m_serial.ReadLine('\r', 256);

        if (!line.empty())
        {
            // Strip any stray whitespace / control chars
            std::string clean;
            for (char c : line)
                if ((unsigned char)c >= 0x20 || c == '\t')
                    clean += c;

            if (!clean.empty() && DmmParser::IsKnownModeCode(clean[0]))
            {
                DmmReading reading = m_parser.Parse(clean);
                if (reading.valid)
                    PostReading(reading);
            }
        }

        // Respect polling delay (interruptible sleep)
        for (int i = 0; i < m_pollDelayMs && !m_stop && !TestDestroy(); i += 10)
            wxThread::Sleep(10);
    }

    m_serial.Close();
    return (ExitCode)0;
}

void ReaderThread::PostReading(const DmmReading& r)
{
    wxCommandEvent evt(EVT_DMM_READING);
    evt.SetString(PackReading(r));
    wxPostEvent(m_sink, evt);
}

void ReaderThread::PostError(const wxString& msg)
{
    wxCommandEvent evt(EVT_DMM_ERROR);
    evt.SetString(msg);
    wxPostEvent(m_sink, evt);
}
