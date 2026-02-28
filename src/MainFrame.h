#pragma once
// ============================================================
//  Protek506Logger — MainFrame.h
//  Primary application window.
// ============================================================
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/aboutdlg.h>
#include <wx/fileconf.h>   // wxFileConfig — INI persistence
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <memory>
#include "ReaderThread.h"
#include "CsvLogger.h"
#include "Events.h"

class MainFrame : public wxFrame
{
public:
    explicit MainFrame(const wxString& title);
    virtual ~MainFrame();

private:
    // ---- UI creation ----
    void BuildUI();
    void BuildMenuBar();
    void BuildToolBar();
    void UpdatePortList();

    // ---- state ----
    void SetConnected(bool connected);
    void UpdateStatusBar();

    // ---- event handlers ----
    void OnConnect(wxCommandEvent& evt);
    void OnDisconnect(wxCommandEvent& evt);
    void OnToggleLog(wxCommandEvent& evt);
    void OnChooseLogFile(wxCommandEvent& evt);
    void OnClearLog(wxCommandEvent& evt);
    void OnRefreshPorts(wxCommandEvent& evt);
    void OnAbout(wxCommandEvent& evt);
    void OnExit(wxCommandEvent& evt);
    void OnClose(wxCloseEvent& evt);
    void OnDmmReading(wxCommandEvent& evt);
    void OnDmmError(wxCommandEvent& evt);
    void OnTimer(wxTimerEvent& evt);

    // ---- helpers ----
    void AppendLogRow(const wxString& date, const wxString& time,
                      const wxString& mode, const wxString& reading,
                      const wxString& units);
    void DisplayReading(const wxString& modeName,
                        const wxString& value,
                        const wxString& units);
    void StopReaderThread();

    // ---- INI persistence ----
    void SaveSettings();
    void LoadSettings();
    static wxString IniPath();   // full path to Protek506Logger.ini

    // ---- widgets ----
    wxChoice*      m_portChoice       = nullptr;
    wxButton*      m_btnRefresh       = nullptr;
    wxButton*      m_btnConnect       = nullptr;
    wxButton*      m_btnDisconnect    = nullptr;
    wxSpinCtrl*    m_spinDelay        = nullptr;

    // Big reading display
    wxStaticText*  m_lblMode          = nullptr;
    wxStaticText*  m_lblReading       = nullptr;
    wxStaticText*  m_lblUnits         = nullptr;

    // Log controls
    wxButton*      m_btnToggleLog     = nullptr;
    wxButton*      m_btnChooseFile    = nullptr;
    wxButton*      m_btnClearLog      = nullptr;
    wxTextCtrl*    m_txtLogFile       = nullptr;
    wxListCtrl*    m_listLog          = nullptr;

    wxStatusBar*   m_statusBar        = nullptr;
    wxTimer        m_timer;

    // ---- state ----
    ReaderThread*  m_thread           = nullptr;
    CsvLogger      m_logger;
    bool           m_connected        = false;
    bool           m_logging          = false;
    long           m_readingCount     = 0;
    wxString       m_lastRawLine;

    wxDECLARE_EVENT_TABLE();
};

// Control IDs
enum
{
    ID_CONNECT      = wxID_HIGHEST + 1,
    ID_DISCONNECT,
    ID_TOGGLE_LOG,
    ID_CHOOSE_FILE,
    ID_CLEAR_LOG,
    ID_REFRESH_PORTS,
    ID_TIMER,
};
