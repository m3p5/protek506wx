// ============================================================
//  Protek506Logger — ReaderThread.cpp
// ============================================================
#include "ReaderThread.h"
#include <wx/datetime.h>
#include <chrono>          // for high-resolution timestamping
#include "Events.h"

// === DEFINE THE EVENTS HERE (only once, in this file) ===
wxDEFINE_EVENT(EVT_DMM_READING, wxCommandEvent);
wxDEFINE_EVENT(EVT_DMM_ERROR,   wxCommandEvent);

// ----------------------------------------------------------------
// Pack reading into a pipe-delimited string.
//
// Time resolution: historically we logged only to the second via
// FormatISOTime(), but users want tenths of a second when recording
// fast-changing measurements.  `PackReading` now appends a decimal
// digit derived from the millisecond counter.
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
//
// v1.4.0: Added rawLine as the 6th field so the receiver can log
//   and display the verbatim ASCII line received from the meter.
// ----------------------------------------------------------------
static wxString PackReading(const DmmReading& r)
{
    wxDateTime now  = wxDateTime::Now();
    wxString   date = now.FormatISODate();   // e.g. "2026-02-26"

    // wxDateTime apparently only reports whole-second precision on some
    // platforms (macOS build showed GetMillisecond()==0 every call), which
    // produced a misleading ".0" digit for every timestamp.  Avoid
    // reliance on its millisecond field and instead grab the current time
    // via std::chrono for sub-second accuracy.
    using namespace std::chrono;
    system_clock::time_point tp = system_clock::now();
    auto ms_total = duration_cast<milliseconds>(tp.time_since_epoch()).count();
    int tenth = static_cast<int>((ms_total / 100) % 10);   // 0..9

    // Convert tp to local calendar time for the HH:MM:SS portion.
    std::time_t t = system_clock::to_time_t(tp);
    std::tm tm_local;
#if defined(_WIN32) || defined(__WINDOWS__)
    localtime_s(&tm_local, &t);
#else
    localtime_r(&t, &tm_local);
#endif
    char buf[16]; // "HH:MM:SS" fits in 9 + null
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_local);
    wxString time = wxString::FromUTF8(buf) +
                      wxString::Format(".%d", tenth);    // e.g. "15:30:45.3"

    return wxString::Format("%s|%s|%s|%s|%s|%s",
        date,
        time,
        wxString(r.modeName),
        wxString(r.rawValue),
        wxString::FromUTF8(r.units.c_str()),
        wxString::FromUTF8(r.rawLine.c_str()));
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
