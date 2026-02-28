// ============================================================
//  Protek506Logger — ReaderThread.cpp
// ============================================================
#include "ReaderThread.h"
#include <wx/datetime.h>
#include "Events.h"

// === DEFINE THE EVENTS HERE (only once, in this file) ===
wxDEFINE_EVENT(EVT_DMM_READING, wxCommandEvent);
wxDEFINE_EVENT(EVT_DMM_ERROR,   wxCommandEvent);

// ----------------------------------------------------------------
// Pack reading into a pipe-delimited string.
//
// FIX (macOS + Linux column bug):
//   The original code used FormatISOCombined() which produced ONE
//   timestamp field ("2026-02-26T15:30:45"), but OnDmmReading()
//   expects TWO separate fields (date and time) as parts[0] and
//   parts[1].  This caused every subsequent field to be shifted
//   right by one column on Linux (wrong display positions) and on
//   macOS the sixth %s format specifier had no matching argument
//   (undefined behaviour), producing a malformed string that failed
//   the parts.GetCount() < 5 guard — so nothing was ever shown.
//
//   Fix: split the timestamp into date and time before packing, use
//   exactly five fields, and drop rawLine (the receiver never used it).
// ----------------------------------------------------------------
static wxString PackReading(const DmmReading& r)
{
    wxDateTime now  = wxDateTime::Now();
    wxString   date = now.FormatISODate();   // e.g. "2026-02-26"
    wxString   time = now.FormatISOTime();   // e.g. "15:30:45"

    return wxString::Format("%s|%s|%s|%s|%s",
        date,
        time,
        wxString(r.modeName),
        wxString(r.rawValue),
        wxString(r.units));
}

// ----------------------------------------------------------------
// Constructor / Destructor / RequestStop
// ----------------------------------------------------------------
ReaderThread::ReaderThread(wxEvtHandler* sink,
                           const std::string& port,
                           int pollDelayMs)
    : wxThread(wxTHREAD_JOINABLE),
      m_sink(sink),
      m_port(port),
      m_pollDelayMs(pollDelayMs),
      m_stop(false)
{
}

ReaderThread::~ReaderThread() {}

void ReaderThread::RequestStop()
{
    m_stop.store(true);
}

// ----------------------------------------------------------------
// Thread entry point
// ----------------------------------------------------------------
wxThread::ExitCode ReaderThread::Entry()
{
    if (!m_serial.Open(m_port, 1200, 7, 2, 'N', 1000))
    {
        PostError(wxString::Format("Cannot open port %s: %s",
                                   m_port, m_serial.LastError()));
        return (ExitCode)1;
    }

    while (!m_stop.load() && !TestDestroy())
    {
        m_serial.WriteByte('\n');               // Trigger every cycle

        std::string line = m_serial.ReadLine('\r', 256);

        if (!line.empty())
        {
            DmmReading r = m_parser.Parse(line);
            if (r.valid)
                PostReading(r);
        }
        else if (!m_serial.LastError().empty())
        {
            PostError(wxString::Format("Serial read error: %s",
                                       m_serial.LastError()));
            break;
        }

        for (int i = 0; i < m_pollDelayMs && !m_stop.load() && !TestDestroy(); i += 10)
            wxThread::Sleep(10);
    }

    m_serial.Close();
    return (ExitCode)0;
}

// ----------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------
void ReaderThread::PostReading(const DmmReading& r)
{
    if (!m_sink) return;
    auto* evt = new wxCommandEvent(EVT_DMM_READING);
    evt->SetString(PackReading(r));
    wxQueueEvent(m_sink, evt);
}

void ReaderThread::PostError(const wxString& msg)
{
    if (!m_sink) return;
    auto* evt = new wxCommandEvent(EVT_DMM_ERROR);
    evt->SetString(msg);
    wxQueueEvent(m_sink, evt);
}
