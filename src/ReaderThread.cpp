// ============================================================
//  Protek506Logger — ReaderThread.cpp
//
//  Fixes applied:
//  #1  m_stop is now std::atomic<bool> — eliminates data race.
//  #2  Thread is JOINABLE (not detached).  The owner must call
//      RequestStop() then Wait() before deleting this object or
//      destroying the sink window.  This guarantees no events
//      are ever posted to a dangling wxEvtHandler pointer.
//  #3  ReadLine() returning empty is checked against
//      LastError(); a genuine I/O error fires EVT_DMM_ERROR
//      and terminates the thread rather than silently looping.
//  #14 wxQueueEvent() used in place of wxPostEvent() to avoid
//      an unnecessary event copy.
// ============================================================
#include "ReaderThread.h"
#include <wx/datetime.h>

// Event type definitions
wxDEFINE_EVENT(EVT_DMM_READING, wxCommandEvent);
wxDEFINE_EVENT(EVT_DMM_ERROR,   wxCommandEvent);

// Package a DmmReading as a pipe-delimited string so we don't
// need a custom event class:
//   date|time|modeName|rawValue|units|rawLine
static wxString PackReading(const DmmReading& r)
{
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
// Constructor — note wxTHREAD_JOINABLE (fix #2)
// ----------------------------------------------------------------
ReaderThread::ReaderThread(wxEvtHandler* sink,
                           const std::string& port,
                           int pollDelayMs)
    : wxThread(wxTHREAD_JOINABLE)   // fix #2: was wxTHREAD_DETACHED
    , m_sink(sink)
    , m_port(port)
    , m_pollDelayMs(pollDelayMs)
    , m_stop(false)                 // fix #1: atomic initialisation
{}

ReaderThread::~ReaderThread() {}

void ReaderThread::RequestStop()
{
    m_stop.store(true);             // fix #1: atomic store
}

// ----------------------------------------------------------------
// Thread entry point
// ----------------------------------------------------------------
wxThread::ExitCode ReaderThread::Entry()
{
    // Protek 506: 1200 baud, 7 data bits, 2 stop bits, no parity.
    // Timeout 1 s — long enough that Wait() on the main thread
    // will block at most ~1 s after RequestStop() is called.
    if (!m_serial.Open(m_port, 1200, 7, 2, 'N', 1000))
    {
        PostError(wxString::Format("Cannot open port %s: %s",
            m_port, m_serial.LastError()));
        return (ExitCode)1;
    }

    while (!m_stop.load() && !TestDestroy())   // fix #1: atomic load
    {
        // Trigger the meter to emit one reading
        m_serial.WriteByte('\n');

        // Read CR-terminated response
        std::string line = m_serial.ReadLine('\r', 256);

        if (!line.empty())
        {
            // Strip non-printable characters
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
        else
        {
            // fix #3: distinguish timeout (normal) from I/O error
            std::string err = m_serial.LastError();
            if (!err.empty())
            {
                PostError(wxString::Format("Serial read error: %s", err));
                break;
            }
            // else: timeout waiting for meter — just poll again
        }

        // Interruptible sleep — breaks in 10 ms increments so
        // RequestStop() + Wait() returns promptly.
        for (int i = 0; i < m_pollDelayMs && !m_stop.load() && !TestDestroy(); i += 10)
            wxThread::Sleep(10);
    }

    m_serial.Close();
    return (ExitCode)0;
}

// ----------------------------------------------------------------
// Helpers — use wxQueueEvent (fix #14: avoids event copy)
// ----------------------------------------------------------------
void ReaderThread::PostReading(const DmmReading& r)
{
    auto* evt = new wxCommandEvent(EVT_DMM_READING);
    evt->SetString(PackReading(r));
    wxQueueEvent(m_sink, evt);      // fix #14
}

void ReaderThread::PostError(const wxString& msg)
{
    auto* evt = new wxCommandEvent(EVT_DMM_ERROR);
    evt->SetString(msg);
    wxQueueEvent(m_sink, evt);      // fix #14
}
