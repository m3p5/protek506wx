// ============================================================
//  Protek506Logger — MainFrame.cpp
// ============================================================
#include "MainFrame.h"
#include <wx/aboutdlg.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/numdlg.h>
#include <wx/datetime.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/statbox.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/fileconf.h>
#include <wx/settings.h>   // wxSystemSettings — for dark-mode-safe colours

static const wxString APP_VERSION = "1.2.3";
static const int      TIMER_MS    = 1000; // status-bar refresh

// ============================================================
// Event table
// ============================================================
wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_BUTTON(ID_CONNECT,      MainFrame::OnConnect)
    EVT_BUTTON(ID_DISCONNECT,   MainFrame::OnDisconnect)
    EVT_BUTTON(ID_TOGGLE_LOG,   MainFrame::OnToggleLog)
    EVT_BUTTON(ID_CHOOSE_FILE,  MainFrame::OnChooseLogFile)
    EVT_BUTTON(ID_CLEAR_LOG,    MainFrame::OnClearLog)
    EVT_BUTTON(ID_REFRESH_PORTS,MainFrame::OnRefreshPorts)
    EVT_MENU(wxID_EXIT,         MainFrame::OnExit)
    EVT_MENU(wxID_ABOUT,        MainFrame::OnAbout)
    EVT_CLOSE(                  MainFrame::OnClose)
    EVT_TIMER(ID_TIMER,         MainFrame::OnTimer)
    EVT_COMMAND(wxID_ANY, EVT_DMM_READING, MainFrame::OnDmmReading)
    EVT_COMMAND(wxID_ANY, EVT_DMM_ERROR,   MainFrame::OnDmmError)
wxEND_EVENT_TABLE()

// ============================================================
// Constructor
// ============================================================
MainFrame::MainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title,
              wxDefaultPosition, wxSize(820, 640))
    , m_timer(this, ID_TIMER)
{
    BuildMenuBar();
    BuildUI();
    SetMinSize(wxSize(700, 560));
    Centre();
    m_timer.Start(TIMER_MS);
    UpdatePortList();
    LoadSettings();       // restore last port, log file path
    UpdateStatusBar();
}

MainFrame::~MainFrame()
{
    StopReaderThread();
    m_logger.Close();
}

// ============================================================
// Menu
// ============================================================
void MainFrame::BuildMenuBar()
{
    wxMenuBar* mb = new wxMenuBar;

    wxMenu* mFile = new wxMenu;
    mFile->Append(wxID_EXIT, "E&xit\tAlt+F4");

    wxMenu* mHelp = new wxMenu;
    mHelp->Append(wxID_ABOUT, "&About...");

    mb->Append(mFile, "&File");
    mb->Append(mHelp, "&Help");
    SetMenuBar(mb);
}

// ============================================================
// UI layout
// ============================================================
void MainFrame::BuildUI()
{
    // ---- Status bar ----
    m_statusBar = CreateStatusBar(3);
    int widths[] = { -1, 160, 140 };
    m_statusBar->SetStatusWidths(3, widths);

    wxPanel* root = new wxPanel(this);
    wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

    // ================================================================
    // TOP SECTION: Port selector + connection controls
    // ================================================================
    wxStaticBox*      connBox   = new wxStaticBox(root, wxID_ANY, "Serial Port");
    wxStaticBoxSizer* connSizer = new wxStaticBoxSizer(connBox, wxHORIZONTAL);

    // On GTK 3, children of a wxStaticBoxSizer MUST be parented to the
    // wxStaticBox itself (not to the containing panel). If they are parented
    // to the panel, GTK's internal widget tree is inconsistent and calling
    // gtk_widget_set_sensitive() (i.e. Enable()) triggers a
    // gtk_box_gadget_distribute assertion and segfault.
    connSizer->Add(new wxStaticText(connBox, wxID_ANY, "Port:"),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

    m_portChoice = new wxChoice(connBox, wxID_ANY, wxDefaultPosition, wxSize(180, -1));
    connSizer->Add(m_portChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    m_btnRefresh = new wxButton(connBox, ID_REFRESH_PORTS, "Refresh",
                                wxDefaultPosition, wxSize(72, -1));
    connSizer->Add(m_btnRefresh, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);

    connSizer->Add(new wxStaticText(connBox, wxID_ANY, "Poll (ms):"),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

    m_spinDelay = new wxSpinCtrl(connBox, wxID_ANY, "200",
                                 wxDefaultPosition, wxSize(72, -1),
                                 wxSP_ARROW_KEYS, 50, 5000, 200);
    connSizer->Add(m_spinDelay, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);

    m_btnConnect = new wxButton(connBox, ID_CONNECT, "Connect",
                                wxDefaultPosition, wxSize(90, -1));
    m_btnConnect->SetForegroundColour(wxColour(0, 128, 0));
    connSizer->Add(m_btnConnect, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    m_btnDisconnect = new wxButton(connBox, ID_DISCONNECT, "Disconnect",
                                   wxDefaultPosition, wxSize(90, -1));
    m_btnDisconnect->SetForegroundColour(wxColour(180, 0, 0));
    m_btnDisconnect->Enable(false);
    connSizer->Add(m_btnDisconnect, 0, wxALIGN_CENTER_VERTICAL);

    rootSizer->Add(connSizer, 0, wxEXPAND | wxALL, 8);

    // ================================================================
    // MIDDLE: Big reading display panel
    // ================================================================
    wxStaticBox*      readBox   = new wxStaticBox(root, wxID_ANY, "Live Reading");
    wxStaticBoxSizer* readSizer = new wxStaticBoxSizer(readBox, wxVERTICAL);

    // Mode label (e.g. "DC Voltage")
    m_lblMode = new wxStaticText(readBox, wxID_ANY, "---",
                                 wxDefaultPosition, wxDefaultSize,
                                 wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
    {
        wxFont f = m_lblMode->GetFont();
        f.SetPointSize(16);
        f.SetWeight(wxFONTWEIGHT_BOLD);
        m_lblMode->SetFont(f);
        m_lblMode->SetForegroundColour(wxColour(60, 60, 180));
    }
    readSizer->Add(m_lblMode, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 6);

    // Reading value (large 7-segment-style)
    m_lblReading = new wxStaticText(readBox, wxID_ANY, "----",
                                    wxDefaultPosition, wxDefaultSize,
                                    wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
    {
        wxFont f = m_lblReading->GetFont();
        f.SetPointSize(54);
        f.SetWeight(wxFONTWEIGHT_BOLD);
        f.SetFamily(wxFONTFAMILY_TELETYPE);
        m_lblReading->SetFont(f);
        m_lblReading->SetForegroundColour(wxColour(20, 160, 20));
    }
    readSizer->Add(m_lblReading, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

    // Units
    m_lblUnits = new wxStaticText(readBox, wxID_ANY, "",
                                  wxDefaultPosition, wxDefaultSize,
                                  wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
    {
        wxFont f = m_lblUnits->GetFont();
        f.SetPointSize(22);
        f.SetWeight(wxFONTWEIGHT_NORMAL);
        m_lblUnits->SetFont(f);
        m_lblUnits->SetForegroundColour(wxColour(100, 100, 100));
    }
    readSizer->Add(m_lblUnits, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 6);

    rootSizer->Add(readSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // ================================================================
    // LOGGING CONTROLS
    // ================================================================
    wxStaticBox*      logCtrlBox   = new wxStaticBox(root, wxID_ANY, "CSV Logging");
    wxStaticBoxSizer* logCtrlSizer = new wxStaticBoxSizer(logCtrlBox, wxHORIZONTAL);

    logCtrlSizer->Add(new wxStaticText(logCtrlBox, wxID_ANY, "File:"),
                      0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

    m_txtLogFile = new wxTextCtrl(logCtrlBox, wxID_ANY, "Protek-506-log.csv",
                                  wxDefaultPosition, wxDefaultSize,
                                  wxTE_READONLY);
    logCtrlSizer->Add(m_txtLogFile, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    m_btnChooseFile = new wxButton(logCtrlBox, ID_CHOOSE_FILE, "Browse\xe2\x80\xa6",
                                   wxDefaultPosition, wxSize(78, -1));
    logCtrlSizer->Add(m_btnChooseFile, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    m_btnToggleLog = new wxButton(logCtrlBox, ID_TOGGLE_LOG, "Start Logging",
                                  wxDefaultPosition, wxSize(110, -1));
    m_btnToggleLog->SetForegroundColour(wxColour(0, 128, 0));
    logCtrlSizer->Add(m_btnToggleLog, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    m_btnClearLog = new wxButton(logCtrlBox, ID_CLEAR_LOG, "Clear Table",
                                 wxDefaultPosition, wxSize(90, -1));
    logCtrlSizer->Add(m_btnClearLog, 0, wxALIGN_CENTER_VERTICAL);

    rootSizer->Add(logCtrlSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // ================================================================
    // LOG TABLE
    // ================================================================
    wxStaticBox*      tableBox   = new wxStaticBox(root, wxID_ANY, "Reading Log");
    wxStaticBoxSizer* tableSizer = new wxStaticBoxSizer(tableBox, wxVERTICAL);

    m_listLog = new wxListCtrl(tableBox, wxID_ANY,
                               wxDefaultPosition, wxDefaultSize,
                               wxLC_REPORT | wxLC_HRULES | wxLC_VRULES |
                               wxBORDER_SUNKEN);

    // Columns: #, Date, Time, Mode, Reading, Units
    m_listLog->InsertColumn(0, "#",       wxLIST_FORMAT_RIGHT,  50);
    m_listLog->InsertColumn(1, "Date",    wxLIST_FORMAT_LEFT,  100);
    m_listLog->InsertColumn(2, "Time",    wxLIST_FORMAT_LEFT,  115);
    m_listLog->InsertColumn(3, "Mode",    wxLIST_FORMAT_LEFT,   70);
    m_listLog->InsertColumn(4, "Reading", wxLIST_FORMAT_RIGHT, 110);
    m_listLog->InsertColumn(5, "Units",   wxLIST_FORMAT_LEFT,   90);

    tableSizer->Add(m_listLog, 1, wxEXPAND | wxALL, 4);
    rootSizer->Add(tableSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    root->SetSizer(rootSizer);
    rootSizer->Fit(root);
}

// ============================================================
// Port list refresh
// ============================================================
void MainFrame::UpdatePortList()
{
    m_portChoice->Clear();
    std::vector<PortInfo> ports = SerialPort::ListPorts();
    if (ports.empty())
    {
        m_portChoice->Append("(no ports found)");
        m_portChoice->SetSelection(0);
        return;
    }
    for (auto& p : ports)
    {
        wxString label = wxString::FromUTF8(p.device.c_str());
        if (!p.description.empty() && p.description != p.device)
            label += " — " + wxString::FromUTF8(p.description.c_str());
        m_portChoice->Append(label, new wxStringClientData(
            wxString::FromUTF8(p.device.c_str())));
    }
    m_portChoice->SetSelection(0);
}

// ============================================================
// Connection
// ============================================================
void MainFrame::OnRefreshPorts(wxCommandEvent&)
{
    UpdatePortList();
}

void MainFrame::OnConnect(wxCommandEvent&)
{
    if (m_connected) return;

    int sel = m_portChoice->GetSelection();
    if (sel == wxNOT_FOUND)
    {
        wxMessageBox("Please select a serial port.", "No Port Selected",
                     wxICON_WARNING | wxOK, this);
        return;
    }

    wxStringClientData* data =
        dynamic_cast<wxStringClientData*>(m_portChoice->GetClientObject(sel));
    wxString port = data ? data->GetData() : m_portChoice->GetStringSelection();

    // Strip " — description" if present (fallback when client data missing)
    int dash = port.Find(" \xe2\x80\x94 "); // em-dash
    if (dash != wxNOT_FOUND) port = port.Left(dash);

    int delayMs = m_spinDelay->GetValue();

    m_thread = new ReaderThread(this, port.ToStdString(), delayMs);
    if (m_thread->Create() != wxTHREAD_NO_ERROR ||
        m_thread->Run()    != wxTHREAD_NO_ERROR)
    {
        wxMessageBox("Failed to start reader thread.", "Error",
                     wxICON_ERROR | wxOK, this);
        delete m_thread;
        m_thread = nullptr;
        return;
    }

    SetConnected(true);
    SaveSettings();   // persist the chosen port
    m_statusBar->SetStatusText(
        wxString::Format("Connected: %s", port), 1);
}

void MainFrame::OnDisconnect(wxCommandEvent&)
{
    StopReaderThread();
    SetConnected(false);
    m_statusBar->SetStatusText("Disconnected", 1);
    m_lblMode->SetLabel("---");
    m_lblReading->SetLabel("----");
    m_lblUnits->SetLabel("");
}

void MainFrame::StopReaderThread()
{
    if (m_thread)
    {
        // fix #2: Thread is JOINABLE.  Signal it to stop then Wait()
        // so we are guaranteed it has exited — and therefore will never
        // call wxQueueEvent on this (possibly already-destroyed) window —
        // before we null the pointer.
        m_thread->RequestStop();
        m_thread->Wait();   // blocks ≤ ~1 s (serial read timeout)
        delete m_thread;
        m_thread = nullptr;
    }
}

void MainFrame::SetConnected(bool connected)
{
    m_connected = connected;
    m_btnConnect->Enable(!connected);
    m_btnDisconnect->Enable(connected);
    m_portChoice->Enable(!connected);
    m_btnRefresh->Enable(!connected);
    m_spinDelay->Enable(!connected);
}

// ============================================================
// Logging
// ============================================================
void MainFrame::OnToggleLog(wxCommandEvent&)
{
    if (!m_logging)
    {
        // Start logging
        wxString path = m_txtLogFile->GetValue();
        if (path.IsEmpty())
            path = "Protek-506-log.csv";

        if (!m_logger.Open(path.ToStdString()))
        {
            wxMessageBox(
                wxString::Format("Cannot open log file:\n%s\n\n%s",
                    path, m_logger.LastError()),
                "Log Error", wxICON_ERROR | wxOK, this);
            return;
        }

        m_logging = true;
        m_readingCount = 0;   // count from 1 when first row is appended
        m_btnToggleLog->SetLabel("Stop Logging");
        m_btnToggleLog->SetForegroundColour(wxColour(180, 0, 0));
        m_btnChooseFile->Enable(false);
        m_statusBar->SetStatusText(
            wxString::Format("Logging → %s",
                wxFileName(path).GetFullName()), 2);
    }
    else
    {
        // Stop logging
        m_logger.Close();
        m_logging = false;
        m_btnToggleLog->SetLabel("Start Logging");
        m_btnToggleLog->SetForegroundColour(wxColour(0, 128, 0));
        m_btnChooseFile->Enable(true);
        m_statusBar->SetStatusText("", 2);
    }
}

void MainFrame::OnChooseLogFile(wxCommandEvent&)
{
    wxFileDialog dlg(this, "Choose CSV log file", "", "Protek-506-log.csv",
                     "CSV files (*.csv)|*.csv|Text files (*.txt)|*.txt|All files (*.*)|*.*",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_OK)
    {
        m_txtLogFile->SetValue(dlg.GetPath());
        SaveSettings();   // persist the new log file path
    }
}

void MainFrame::OnClearLog(wxCommandEvent&)
{
    m_listLog->DeleteAllItems();
    m_readingCount = 0;
    UpdateStatusBar();
}

// ============================================================
// DMM events from reader thread
// ============================================================
void MainFrame::OnDmmReading(wxCommandEvent& evt)
{
    // Unpack pipe-delimited: date|time|modeName|rawValue|units|rawLine
    wxString packed = evt.GetString();
    wxArrayString parts = wxSplit(packed, '|');
    if (parts.GetCount() < 5) return;

    wxString date     = parts[0];
    wxString time     = parts[1];
    wxString modeName = parts[2];
    wxString value    = parts[3];
    wxString units    = parts[4];
    wxString rawLine  = (parts.GetCount() > 5) ? parts[5] : "";

    m_lastRawLine = rawLine;

    // Always update the live display regardless of logging state
    DisplayReading(modeName, value, units);

    // Only count and append rows while logging is active
    if (m_logging)
    {
        ++m_readingCount;
        AppendLogRow(date, time, modeName, value, units);
    }

    // Write to CSV if logging
    if (m_logging && m_logger.IsOpen())
    {
        m_logger.Write(date.ToStdString(),
                       time.ToStdString(),
                       modeName.ToStdString(),
                       value.ToStdString(),
                       units.ToStdString());

        // fix #12: Detect write failure (disk full, I/O error, etc.)
        // IsOpen() returns false once CsvLogger::Write closes the file on error.
        if (!m_logger.IsOpen())
        {
            m_logging = false;
            m_btnToggleLog->SetLabel("Start Logging");
            m_btnToggleLog->SetForegroundColour(wxColour(0, 128, 0));
            m_btnChooseFile->Enable(true);
            m_statusBar->SetStatusText("", 2);

            wxString errMsg = m_logger.LastError();
            CallAfter([this, errMsg]() {
                if (IsBeingDeleted()) return;
                wxMessageBox(
                    wxString::Format("CSV logging stopped due to a write error:\n\n%s\n\n"
                                     "Check available disk space.", errMsg),
                    "Log Write Error", wxICON_ERROR | wxOK, this);
            });
        }
    }

    UpdateStatusBar();
}

void MainFrame::OnDmmError(wxCommandEvent& evt)
{
    wxString msg = evt.GetString();

    // Update status bar with error
    m_statusBar->SetStatusText(wxString::Format("Error: %s", msg), 1);

    // If we get an error the thread has stopped; update UI
    if (m_connected)
    {
        SetConnected(false);
        m_lblMode->SetLabel("ERROR");
        m_lblMode->SetForegroundColour(wxColour(200, 0, 0));
        m_lblReading->SetLabel("----");
        m_lblUnits->SetLabel("");

        // fix #11: The original lambda captured raw 'this', which could
        // be dangling if the window is closed before CallAfter fires.
        // Guard with IsBeingDeleted() — safe because CallAfter runs on
        // the main thread where wxWindow lifetime is single-threaded.
        wxString errMsg = msg;
        CallAfter([this, errMsg]() {
            if (IsBeingDeleted()) return;   // window already closing
            wxMessageBox(wxString::Format(
                "Connection error:\n\n%s\n\nCheck that:\n"
                " \xe2\x80\xa2 The meter is powered on\n"
                " \xe2\x80\xa2 RS232 mode is enabled on the meter\n"
                " \xe2\x80\xa2 The correct serial port is selected\n"
                " \xe2\x80\xa2 The cable is connected",
                errMsg),
                "DMM Connection Error",
                wxICON_ERROR | wxOK, this);
        });
    }
}

// ============================================================
// Live display
// ============================================================
void MainFrame::DisplayReading(const wxString& modeName,
                               const wxString& value,
                               const wxString& units)
{
    // Mode label mapping to friendly strings
    wxString friendly = modeName;
    if      (modeName == "DC")   friendly = "DC Voltage / Current";
    else if (modeName == "AC")   friendly = "AC Voltage / Current";
    else if (modeName == "RES")  friendly = "Resistance";
    else if (modeName == "FREQ") friendly = "Frequency";
    else if (modeName == "CAP")  friendly = "Capacitance";
    else if (modeName == "IND")  friendly = "Inductance";
    else if (modeName == "TEMP") friendly = "Temperature";
    else if (modeName == "DIODE")friendly = "Diode / Continuity";

    m_lblMode->SetLabel(friendly);
    m_lblReading->SetLabel(value.IsEmpty() ? "----" : value);
    m_lblUnits->SetLabel(units);

    // Colour the reading based on state
    wxColour readColour(20, 160, 20); // normal green
    if (value == "OL" || value == "OPEN")
        readColour = wxColour(200, 120, 0);
    else if (value == "SHORT")
        readColour = wxColour(180, 0, 0);
    else if (value == "High" || value == "Low" || value == "----")
        readColour = wxColour(0, 120, 200);

    m_lblReading->SetForegroundColour(readColour);
    m_lblMode->SetForegroundColour(wxColour(60, 60, 180));

    // Force re-layout for the reading panel
    m_lblReading->GetParent()->Layout();
    m_lblReading->GetParent()->Refresh();
}

// ============================================================
// Log table
// ============================================================
void MainFrame::AppendLogRow(const wxString& date, const wxString& time,
                              const wxString& mode, const wxString& reading,
                              const wxString& units)
{
    long row = m_listLog->GetItemCount();

    // Keep table from growing unboundedly (keep last 5000 rows)
    if (row >= 5000)
    {
        m_listLog->DeleteItem(0);
        row = m_listLog->GetItemCount();
    }

    long idx = m_listLog->InsertItem(row, wxString::Format("%ld", m_readingCount));
    m_listLog->SetItem(idx, 1, date);
    m_listLog->SetItem(idx, 2, time);
    m_listLog->SetItem(idx, 3, mode);
    m_listLog->SetItem(idx, 4, reading);
    m_listLog->SetItem(idx, 5, units);

    // ----------------------------------------------------------------
    // Dark-mode-safe alternating row colours (fix #3).
    //
    // Problem: hardcoded light backgrounds (white / pale blue) remain
    // light even when macOS switches to Dark mode.  The system then
    // renders the list text in a light colour too, making it invisible
    // against the light row background.
    //
    // Solution: derive both the background and the text colour from the
    // wxSystemSettings palette, which respects the current OS appearance.
    //
    //   wxSYS_COLOUR_LISTBOX      — the list's natural background
    //   wxSYS_COLOUR_LISTBOXTEXT  — the list's natural foreground
    //   wxSYS_COLOUR_HIGHLIGHT    — (avoided; only correct for selected)
    //
    // For the alternating stripe we blend the list background 90 % with
    // the window-highlight colour (5 % in dark mode feels about right).
    // A helper lambda does the blend in a single expression.
    // ----------------------------------------------------------------
    wxColour base  = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);
    wxColour text  = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT);

    // Blend: result = base * (1-t) + accent * t,  t = 0.08
    auto blend = [](wxColour a, wxColour b, double t) -> wxColour {
        return wxColour(
            static_cast<unsigned char>(a.Red()   * (1.0 - t) + b.Red()   * t),
            static_cast<unsigned char>(a.Green() * (1.0 - t) + b.Green() * t),
            static_cast<unsigned char>(a.Blue()  * (1.0 - t) + b.Blue()  * t));
    };

    // Use the window background as the accent colour for the stripe so
    // it contrasts gently in both light and dark mode.
    wxColour accent = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    wxColour stripe = blend(base, accent, 0.35);

    wxColour rowBg = (idx % 2 == 0) ? stripe : base;

    m_listLog->SetItemBackgroundColour(idx, rowBg);
    m_listLog->SetItemTextColour(idx, text);

    // Auto-scroll to last row
    m_listLog->EnsureVisible(idx);
}

// ============================================================
// Status bar
// ============================================================
void MainFrame::UpdateStatusBar()
{
    m_statusBar->SetStatusText(
        wxString::Format("Readings: %ld", m_readingCount), 0);
}

// ============================================================
// Timer (periodic UI refresh)
// ============================================================
void MainFrame::OnTimer(wxTimerEvent&)
{
    UpdateStatusBar();
    if (m_logging && m_logger.IsOpen())
    {
        m_statusBar->SetStatusText(
            wxString::Format("Logging (%ld rows) → %s",
                m_logger.RowCount(),
                wxFileName(m_txtLogFile->GetValue()).GetFullName()), 2);
    }
}

// ============================================================
// INI persistence (fix: remember last port and log file path)
// ============================================================

// Returns the full path to Protek506Logger.ini.
// On macOS/Linux this is in the user's config directory
// (e.g. ~/.config/Protek506Logger/Protek506Logger.ini).
// On Windows it uses %APPDATA%\Protek506Logger\Protek506Logger.ini.
// If the directory doesn't exist wxFileConfig creates it.
/*static*/ wxString MainFrame::IniPath()
{
    wxFileName ini;
    ini.AssignDir(wxStandardPaths::Get().GetUserDataDir());
    ini.SetFullName("Protek506Logger.ini");
    // Ensure the directory exists so wxFileConfig can write
    if (!ini.DirExists())
        wxFileName::Mkdir(ini.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return ini.GetFullPath();
}

void MainFrame::SaveSettings()
{
    wxFileConfig cfg("Protek506Logger", wxEmptyString,
                     IniPath(), wxEmptyString,
                     wxCONFIG_USE_LOCAL_FILE);

    // Save whichever port device string is currently selected
    int sel = m_portChoice->GetSelection();
    if (sel != wxNOT_FOUND)
    {
        wxStringClientData* cd =
            dynamic_cast<wxStringClientData*>(m_portChoice->GetClientObject(sel));
        wxString device = cd ? cd->GetData() : m_portChoice->GetStringSelection();
        cfg.Write("/Serial/LastPort", device);
    }

    // Save the log file path
    cfg.Write("/Logging/LastFile", m_txtLogFile->GetValue());

    cfg.Flush();
}

void MainFrame::LoadSettings()
{
    wxFileConfig cfg("Protek506Logger", wxEmptyString,
                     IniPath(), wxEmptyString,
                     wxCONFIG_USE_LOCAL_FILE);

    // Restore last port — scan the choice items for a matching device string
    wxString lastPort;
    if (cfg.Read("/Serial/LastPort", &lastPort) && !lastPort.IsEmpty())
    {
        for (unsigned i = 0; i < m_portChoice->GetCount(); ++i)
        {
            wxStringClientData* cd =
                dynamic_cast<wxStringClientData*>(m_portChoice->GetClientObject(i));
            wxString device = cd ? cd->GetData() : m_portChoice->GetString(i);
            if (device == lastPort)
            {
                m_portChoice->SetSelection(i);
                break;
            }
        }
    }

    // Restore log file path
    wxString lastFile;
    if (cfg.Read("/Logging/LastFile", &lastFile) && !lastFile.IsEmpty())
        m_txtLogFile->SetValue(lastFile);
}

// ============================================================
// About dialog
// ============================================================
void MainFrame::OnAbout(wxCommandEvent&)
{
    wxAboutDialogInfo info;
    info.SetName("Protek 506 DMM Logger");
    info.SetVersion(APP_VERSION);
    info.SetDescription(
        "A cross-platform data logger for the Protek 506 Digital\n"
        "Multimeter via its RS-232C serial interface.\n\n"
        "Serial settings: 1200 baud, 7 data bits, 2 stop bits, no parity.\n\n"
        "Remember to enable RS232 mode on the meter:\n"
        "  MENU → RS232 → Enter");
    info.SetCopyright("(C) 2026");
    info.AddDeveloper("m3p5 - C++ with wxWidgets");
    wxAboutBox(info, this);
}

// ============================================================
// Exit / Close
// ============================================================
void MainFrame::OnExit(wxCommandEvent&)
{
    Close(true);
}

void MainFrame::OnClose(wxCloseEvent& evt)
{
    SaveSettings();   // persist port and log file path on every close
    StopReaderThread();
    if (m_logging) m_logger.Close();
    evt.Skip(); // allow default close
}
