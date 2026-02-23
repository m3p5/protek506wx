#pragma once
// ============================================================
//  Protek506Logger — ReaderThread.h
//  wxThread that polls the Protek 506 and posts events to
//  the main frame whenever a valid reading arrives.
//
//  Design notes:
//  - JOINABLE thread (not detached) so the owner can call
//    Wait() and guarantee the thread has exited before the
//    sink window is destroyed. This prevents posting events
//    to a dangling pointer.
//  - m_stop is std::atomic<bool> to avoid a data race between
//    the main thread calling RequestStop() and the worker
//    thread reading the flag in Entry().
// ============================================================
#include <wx/wx.h>
#include <wx/thread.h>
#include <atomic>
#include "SerialPort.h"
#include "DmmParser.h"

// Custom events posted to the main window
wxDECLARE_EVENT(EVT_DMM_READING, wxCommandEvent);
wxDECLARE_EVENT(EVT_DMM_ERROR,   wxCommandEvent);

class ReaderThread : public wxThread
{
public:
    ReaderThread(wxEvtHandler* sink,
                 const std::string& port,
                 int pollDelayMs = 200);
    virtual ~ReaderThread();

    // Signal the thread to stop.  Call this before Wait().
    void RequestStop();

protected:
    virtual ExitCode Entry() override;

private:
    wxEvtHandler*       m_sink;
    std::string         m_port;
    int                 m_pollDelayMs;
    SerialPort          m_serial;
    DmmParser           m_parser;
    std::atomic<bool>   m_stop;   // fix #1: was plain bool — data race

    void PostReading(const DmmReading& r);
    void PostError(const wxString& msg);
};
