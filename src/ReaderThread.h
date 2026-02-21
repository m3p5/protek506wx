#pragma once
// ============================================================
//  Protek506Logger — ReaderThread.h
//  wxThread that polls the Protek 506 and posts events to
//  the main frame whenever a valid reading arrives.
// ============================================================
#include <wx/wx.h>
#include <wx/thread.h>
#include "SerialPort.h"
#include "DmmParser.h"

// Custom event posted to the main window
wxDECLARE_EVENT(EVT_DMM_READING, wxCommandEvent);
wxDECLARE_EVENT(EVT_DMM_ERROR,   wxCommandEvent);

class ReaderThread : public wxThread
{
public:
    ReaderThread(wxEvtHandler* sink,
                 const std::string& port,
                 int pollDelayMs = 200);
    virtual ~ReaderThread();

    void RequestStop();

protected:
    virtual ExitCode Entry() override;

private:
    wxEvtHandler* m_sink;
    std::string   m_port;
    int           m_pollDelayMs;
    SerialPort    m_serial;
    DmmParser     m_parser;
    bool          m_stop;

    void PostReading(const DmmReading& r);
    void PostError(const wxString& msg);
};
