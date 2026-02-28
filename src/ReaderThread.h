#pragma once
// ============================================================
//  Protek506Logger — ReaderThread.h
//  wxThread that polls the Protek 506 and posts events to
//  the main frame whenever a valid reading arrives.
// ============================================================
#include <wx/wx.h>
#include <wx/thread.h>
#include <atomic>
#include "SerialPort.h"
#include "DmmParser.h"
#include "Events.h"

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
