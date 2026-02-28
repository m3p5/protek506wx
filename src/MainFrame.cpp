// ============================================================
//  Protek506Logger — MainFrame.cpp   (v1.3.1)
// ============================================================
#include "MainFrame.h"
#include <wx/aboutdlg.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/datetime.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/fileconf.h>
#include <wx/settings.h>
#include "Events.h"

static const wxString APP_VERSION = "1.3.1";
static const int      TIMER_MS    = 1000;

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_BUTTON(ID_CONNECT,       MainFrame::OnConnect)
    EVT_BUTTON(ID_DISCONNECT,    MainFrame::OnDisconnect)
    EVT_BUTTON(ID_TOGGLE_LOG,    MainFrame::OnToggleLog)
    EVT_BUTTON(ID_CHOOSE_FILE,   MainFrame::OnChooseLogFile)
    EVT_BUTTON(ID_CLEAR_LOG,     MainFrame::OnClearLog)
    EVT_BUTTON(ID_REFRESH_PORTS, MainFrame::OnRefreshPorts)
    EVT_MENU(wxID_EXIT,          MainFrame::OnExit)
    EVT_MENU(wxID_ABOUT,         MainFrame::OnAbout)
    EVT_CLOSE(                   MainFrame::OnClose)
    EVT_TIMER(ID_TIMER,          MainFrame::OnTimer)
    EVT_COMMAND(wxID_ANY, EVT_DMM_READING, MainFrame::OnDmmReading)
    EVT_COMMAND(wxID_ANY, EVT_DMM_ERROR,   MainFrame::OnDmmError)
wxEND_EVENT_TABLE()

// ============================================================
// Constructor / Destructor
// ============================================================
MainFrame::MainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(820, 640))
    , m_timer(this, ID_TIMER)
{
    BuildMenuBar();
    BuildUI();
    SetMinSize(wxSize(700, 560));
    Centre();
    m_timer.Start(TIMER_MS);
    UpdatePortList();
    LoadSettings();
    UpdateStatusBar();
}

MainFrame::~MainFrame()
{
    StopReaderThread();
    m_logger.Close();
}

// ============================================================
// BuildUI
// ============================================================
void MainFrame::BuildUI()
{
    m_statusBar = CreateStatusBar(3);
    int widths[] = { -1, 160, 140 };
    m_statusBar->SetStatusWidths(3, widths);

    wxPanel*    root      = new wxPanel(this);
    wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

    // ----------------------------------------------------------
    // Serial Port
    // ----------------------------------------------------------
    {
        wxStaticBox*      box   = new wxStaticBox(root, wxID_ANY, "Serial Port");
        wxStaticBoxSizer* sizer = new wxStaticBoxSizer(box, wxHORIZONTAL);

        sizer->Add(new wxStaticText(box, wxID_ANY, "Port:"),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

        m_portChoice = new wxChoice(box, wxID_ANY,
                                    wxDefaultPosition, wxSize(180, -1));
        sizer->Add(m_portChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

        m_btnRefresh = new wxButton(box, ID_REFRESH_PORTS, "Refresh",
                                    wxDefaultPosition, wxSize(72, -1));
        sizer->Add(m_btnRefresh, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);

        sizer->Add(new wxStaticText(box, wxID_ANY, "Poll (ms):"),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

        m_spinDelay = new wxSpinCtrl(box, wxID_ANY, "250",
                                     wxDefaultPosition, wxDefaultSize,
                                     wxSP_ARROW_KEYS, 200, 60000, 250);
        sizer->Add(m_spinDelay, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);

        m_btnConnect = new wxButton(box, ID_CONNECT, "Connect",
                                    wxDefaultPosition, wxSize(90, -1));
        m_btnConnect->SetForegroundColour(wxColour(0, 128, 0));
        sizer->Add(m_btnConnect, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

        m_btnDisconnect = new wxButton(box, ID_DISCONNECT, "Disconnect",
                                       wxDefaultPosition, wxSize(90, -1));
        m_btnDisconnect->SetForegroundColour(wxColour(180, 0, 0));
        m_btnDisconnect->Enable(false);
        sizer->Add(m_btnDisconnect, 0, wxALIGN_CENTER_VERTICAL);

        rootSizer->Add(sizer, 0, wxEXPAND | wxALL, 8);
    }

    // ----------------------------------------------------------
    // Live Reading
    // ----------------------------------------------------------
    {
        wxStaticBox*      box   = new wxStaticBox(root, wxID_ANY, "Live Reading");
        wxStaticBoxSizer* sizer = new wxStaticBoxSizer(box, wxVERTICAL);

        m_lblMode = new wxStaticText(box, wxID_ANY, "---",
                                     wxDefaultPosition, wxDefaultSize,
                                     wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
        m_lblMode->SetFont(wxFont(16, wxFONTFAMILY_DEFAULT,
                                  wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        m_lblMode->SetForegroundColour(wxColour(60, 60, 180));
        sizer->Add(m_lblMode, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 6);

        m_lblReading = new wxStaticText(box, wxID_ANY, "----",
                                        wxDefaultPosition, wxDefaultSize,
                                        wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
        m_lblReading->SetFont(wxFont(64, wxFONTFAMILY_TELETYPE,
                                     wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        m_lblReading->SetForegroundColour(wxColour(20, 160, 20));
        sizer->Add(m_lblReading, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

        m_lblUnits = new wxStaticText(box, wxID_ANY, "",
                                      wxDefaultPosition, wxDefaultSize,
                                      wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
        m_lblUnits->SetFont(wxFont(22, wxFONTFAMILY_DEFAULT,
                                   wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        m_lblUnits->SetForegroundColour(wxColour(100, 100, 100));
        sizer->Add(m_lblUnits, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 6);

        rootSizer->Add(sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    }

    // ----------------------------------------------------------
    // CSV Logging controls
    // ----------------------------------------------------------
    {
        wxStaticBox*      box   = new wxStaticBox(root, wxID_ANY, "CSV Logging");
        wxStaticBoxSizer* sizer = new wxStaticBoxSizer(box, wxHORIZONTAL);

        sizer->Add(new wxStaticText(box, wxID_ANY, "File:"),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

        m_txtLogFile = new wxTextCtrl(box, wxID_ANY, "Protek-506-log.csv",
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_READONLY);
        sizer->Add(m_txtLogFile, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

        m_btnChooseFile = new wxButton(box, ID_CHOOSE_FILE, "Browse...",
                                       wxDefaultPosition, wxSize(78, -1));
        sizer->Add(m_btnChooseFile, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

        m_btnToggleLog = new wxButton(box, ID_TOGGLE_LOG, "Start Logging",
                                      wxDefaultPosition, wxSize(110, -1));
        m_btnToggleLog->SetForegroundColour(wxColour(0, 128, 0));
        sizer->Add(m_btnToggleLog, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

        m_btnClearLog = new wxButton(box, ID_CLEAR_LOG, "Clear Table",
                                     wxDefaultPosition, wxSize(90, -1));
        sizer->Add(m_btnClearLog, 0, wxALIGN_CENTER_VERTICAL);

        rootSizer->Add(sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    }

    // ----------------------------------------------------------
    // Reading Log table
    // ----------------------------------------------------------
    {
        // Title label styled to match the other group boxes
        wxStaticText* lbl = new wxStaticText(root, wxID_ANY, "Reading Log");
        wxFont f = lbl->GetFont();
        f.SetWeight(wxFONTWEIGHT_BOLD);
        lbl->SetFont(f);
        rootSizer->Add(lbl, 0, wxLEFT | wxRIGHT | wxTOP, 8);

        // Container panel — plain GtkBox underneath, no GtkFrame
        wxPanel* tablePanel = new wxPanel(root, wxID_ANY,
                                          wxDefaultPosition, wxDefaultSize,
                                          wxBORDER_SUNKEN);

        // wxListCtrl directly inside the plain panel — safe on GTK3
        m_listLog = new wxListCtrl(tablePanel, wxID_ANY,
                                   wxDefaultPosition, wxDefaultSize,
                                   wxLC_REPORT | wxLC_HRULES | wxLC_VRULES |
                                   wxBORDER_NONE);

        m_listLog->InsertColumn(0, "#",       wxLIST_FORMAT_RIGHT,  50);
        m_listLog->InsertColumn(1, "Date",    wxLIST_FORMAT_LEFT,  100);
        m_listLog->InsertColumn(2, "Time",    wxLIST_FORMAT_LEFT,  115);
        m_listLog->InsertColumn(3, "Mode",    wxLIST_FORMAT_LEFT,   70);
        m_listLog->InsertColumn(4, "Reading", wxLIST_FORMAT_RIGHT, 110);
        m_listLog->InsertColumn(5, "Units",   wxLIST_FORMAT_LEFT,   90);

        wxBoxSizer* ps = new wxBoxSizer(wxVERTICAL);
        ps->Add(m_listLog, 1, wxEXPAND | wxALL, 2);
        tablePanel->SetSizer(ps);

        rootSizer->Add(tablePanel, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    }

    root->SetSizer(rootSizer);
    rootSizer->Fit(root);
}

// ============================================================
// Port list
// ============================================================
void MainFrame::UpdatePortList()
{
    m_portChoice->Clear();
    auto ports = SerialPort::ListPorts();
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
            label += " - " + wxString::FromUTF8(p.description.c_str());
        m_portChoice->Append(label,
            new wxStringClientData(wxString::FromUTF8(p.device.c_str())));
    }
    m_portChoice->SetSelection(0);
}

// ============================================================
// Connect / Disconnect
// ============================================================
void MainFrame::OnRefreshPorts(wxCommandEvent&) { UpdatePortList(); }

void MainFrame::OnConnect(wxCommandEvent&)
{
    wxString portStr = m_portChoice->GetStringSelection();
    if (portStr.IsEmpty())
    {
        wxMessageBox("Please select a serial port first.",
                     "No Port Selected", wxOK | wxICON_WARNING, this);
        return;
    }

    wxClientData* cd = m_portChoice->GetClientObject(m_portChoice->GetSelection());
    wxString device  = cd ? static_cast<wxStringClientData*>(cd)->GetData() : portStr;
    int pollMs       = m_spinDelay->GetValue();

    if (m_thread) StopReaderThread();

    m_thread = new ReaderThread(this, device.ToStdString(), pollMs);
    if (m_thread->Create() != wxTHREAD_NO_ERROR)
    {
        wxMessageBox("Cannot create reader thread.",
                     "Thread Error", wxOK | wxICON_ERROR, this);
        delete m_thread; m_thread = nullptr; return;
    }
    wxMilliSleep(50);
    if (m_thread->Run() != wxTHREAD_NO_ERROR)
    {
        wxMessageBox("Cannot start reader thread.",
                     "Thread Error", wxOK | wxICON_ERROR, this);
        delete m_thread; m_thread = nullptr; return;
    }
    SetConnected(true);
    m_statusBar->SetStatusText("Connecting to " + device + "...", 1);
}

void MainFrame::OnDisconnect(wxCommandEvent&)
{
    StopReaderThread();
    SetConnected(false);
}

void MainFrame::StopReaderThread()
{
    if (!m_thread) return;
    m_thread->RequestStop();
    m_thread->Wait();
    delete m_thread;
    m_thread = nullptr;
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
        wxString path = m_txtLogFile->GetValue();
        if (path.IsEmpty()) path = "Protek-506-log.csv";

        if (!m_logger.Open(path.ToStdString()))
        {
            wxMessageBox(
                wxString::Format("Cannot open log file:\n%s\n\n%s",
                    path, m_logger.LastError()),
                "Log Error", wxICON_ERROR | wxOK, this);
            return;
        }

        m_logging      = true;
        m_readingCount = 0;
        m_btnToggleLog->SetLabel("Stop Logging");
        m_btnToggleLog->SetForegroundColour(wxColour(180, 0, 0));
        m_btnChooseFile->Enable(false);
        m_statusBar->SetStatusText(
            "Logging -> " + wxFileName(path).GetFullName(), 2);
    }
    else
    {
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
                     "CSV files (*.csv)|*.csv|All files (*.*)|*.*",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_OK)
    {
        m_txtLogFile->SetValue(dlg.GetPath());
        SaveSettings();
    }
}

void MainFrame::OnClearLog(wxCommandEvent&)
{
    m_listLog->DeleteAllItems();
    m_readingCount = 0;
    UpdateStatusBar();
}

// ============================================================
// Readings from the reader thread
// ============================================================
void MainFrame::OnDmmReading(wxCommandEvent& evt)
{
    wxArrayString parts = wxSplit(evt.GetString(), '|');
    if (parts.GetCount() < 5) return;

    wxString date  = parts[0];
    wxString time  = parts[1];
    wxString mode  = parts[2];
    wxString value = parts[3];
    wxString units = parts[4];

    // Strip a single leading zero before a non-decimal digit
    if (value.Length() > 1 && value[0] == '0' && value[1] != '.')
        value = value.Mid(1);

    DisplayReading(mode, value, units);

    if (!m_logging || !m_logger.IsOpen()) return;

    m_logger.Write(date.ToStdString(), time.ToStdString(),
                   mode.ToStdString(), value.ToStdString(),
                   units.ToStdString());

    if (!m_logger.WriteOk())
    {
        m_logging = false;
        wxString err = m_logger.LastError();
        CallAfter([this, err]() {
            if (!IsBeingDeleted())
                wxMessageBox("CSV write error:\n\n" + err +
                             "\n\nLogging stopped.",
                             "Log Write Error", wxICON_ERROR | wxOK, this);
        });
        return;
    }

    AppendLogRow(date, time, mode, value, units);
    ++m_readingCount;
    UpdateStatusBar();
}

void MainFrame::OnDmmError(wxCommandEvent& evt)
{
    wxString msg = evt.GetString();
    m_statusBar->SetStatusText("Error: " + msg, 1);

    if (!m_connected) return;
    SetConnected(false);
    m_lblMode->SetLabel("ERROR");
    m_lblMode->SetForegroundColour(wxColour(200, 0, 0));
    m_lblReading->SetLabel("----");
    m_lblUnits->SetLabel("");

    CallAfter([this, msg]() {
        if (!IsBeingDeleted())
            wxMessageBox("Connection error:\n\n" + msg +
                         "\n\nCheck cable, port, and meter RS232 mode.",
                         "DMM Connection Error", wxICON_ERROR | wxOK, this);
    });
}

// ============================================================
// Live display
// ============================================================
void MainFrame::DisplayReading(const wxString& modeName,
                               const wxString& value,
                               const wxString& units)
{
    wxString friendly = modeName;
    if      (modeName == "DC")    friendly = "DC Voltage / Current";
    else if (modeName == "AC")    friendly = "AC Voltage / Current";
    else if (modeName == "RES")   friendly = "Resistance";
    else if (modeName == "FREQ")  friendly = "Frequency";
    else if (modeName == "CAP")   friendly = "Capacitance";
    else if (modeName == "IND")   friendly = "Inductance";
    else if (modeName == "TEMP")  friendly = "Temperature";
    else if (modeName == "DIODE") friendly = "Diode";
    else if (modeName == "CONT")  friendly = "Continuity";
    else if (modeName == "LOGIC") friendly = "Logic Level";

    m_lblMode->SetLabel(friendly);
    m_lblReading->SetLabel(value.IsEmpty() ? "----" : value);
    m_lblUnits->SetLabel(units);

    wxColour col(20, 160, 20);
    if (value == "OL" || value == "OPEN") col = wxColour(200, 120, 0);
    else if (value == "SHORT")            col = wxColour(180,   0, 0);
    else if (value == "High" || value == "Low" || value == "----")
                                          col = wxColour(  0, 120, 200);
    m_lblReading->SetForegroundColour(col);
    m_lblMode->SetForegroundColour(wxColour(60, 60, 180));

    m_lblReading->GetParent()->Layout();
    m_lblReading->GetParent()->Refresh();
}

// ============================================================
// Log table row append
// ============================================================
void MainFrame::AppendLogRow(const wxString& date, const wxString& time,
                              const wxString& mode, const wxString& reading,
                              const wxString& units)
{
    long row = m_listLog->GetItemCount();
    if (row >= 5000)
    {
        m_listLog->DeleteItem(0);
        row = m_listLog->GetItemCount();
    }

    // InsertItem returns -1 on failure; guard all subsequent calls.
    long idx = m_listLog->InsertItem(row,
                   wxString::Format("%ld", m_readingCount + 1));
    if (idx < 0) return;

    m_listLog->SetItem(idx, 1, date);
    m_listLog->SetItem(idx, 2, time);
    m_listLog->SetItem(idx, 3, mode);
    m_listLog->SetItem(idx, 4, reading);
    m_listLog->SetItem(idx, 5, units);

    // Dark-mode-safe alternating row colours from system palette
    wxColour base   = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);
    wxColour text   = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT);
    wxColour accent = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);

    auto blend = [](wxColour a, wxColour b, double t) -> wxColour {
        return wxColour(
            static_cast<unsigned char>(a.Red()   * (1-t) + b.Red()   * t),
            static_cast<unsigned char>(a.Green() * (1-t) + b.Green() * t),
            static_cast<unsigned char>(a.Blue()  * (1-t) + b.Blue()  * t));
    };
    wxColour stripe = blend(base, accent, 0.35);
    m_listLog->SetItemBackgroundColour(idx, (idx % 2 == 0) ? stripe : base);
    m_listLog->SetItemTextColour(idx, text);
    m_listLog->EnsureVisible(idx);
}

// ============================================================
// Status bar / Timer
// ============================================================
void MainFrame::UpdateStatusBar()
{
    m_statusBar->SetStatusText(
        wxString::Format("Readings: %ld", m_readingCount), 0);
}

void MainFrame::OnTimer(wxTimerEvent&)
{
    UpdateStatusBar();
    if (m_logging && m_logger.IsOpen())
        m_statusBar->SetStatusText(
            wxString::Format("Logging (%ld rows) -> %s",
                m_logger.RowCount(),
                wxFileName(m_txtLogFile->GetValue()).GetFullName()), 2);
}

// ============================================================
// INI persistence
// ============================================================
/*static*/ wxString MainFrame::IniPath()
{
    wxFileName ini;
    ini.AssignDir(wxStandardPaths::Get().GetUserDataDir());
    ini.SetFullName("Protek506Logger.ini");
    if (!ini.DirExists())
        wxFileName::Mkdir(ini.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return ini.GetFullPath();
}

void MainFrame::SaveSettings()
{
    wxFileConfig cfg("Protek506Logger", wxEmptyString, IniPath(),
                     wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
    int sel = m_portChoice->GetSelection();
    if (sel != wxNOT_FOUND)
    {
        auto* cd = dynamic_cast<wxStringClientData*>(
                       m_portChoice->GetClientObject(sel));
        cfg.Write("/Serial/LastPort",
                  cd ? cd->GetData() : m_portChoice->GetStringSelection());
    }
    cfg.Write("/Logging/LastFile", m_txtLogFile->GetValue());
    cfg.Flush();
}

void MainFrame::LoadSettings()
{
    wxFileConfig cfg("Protek506Logger", wxEmptyString, IniPath(),
                     wxEmptyString, wxCONFIG_USE_LOCAL_FILE);

    wxString lastPort;
    if (cfg.Read("/Serial/LastPort", &lastPort) && !lastPort.IsEmpty())
    {
        for (unsigned i = 0; i < m_portChoice->GetCount(); ++i)
        {
            auto* cd = dynamic_cast<wxStringClientData*>(
                           m_portChoice->GetClientObject(i));
            wxString dev = cd ? cd->GetData() : m_portChoice->GetString(i);
            if (dev == lastPort) { m_portChoice->SetSelection(i); break; }
        }
    }
    wxString lastFile;
    if (cfg.Read("/Logging/LastFile", &lastFile) && !lastFile.IsEmpty())
        m_txtLogFile->SetValue(lastFile);
}

// ============================================================
// About
// ============================================================
void MainFrame::OnAbout(wxCommandEvent&)
{
    wxAboutDialogInfo info;
    info.SetName("Protek 506 DMM Logger");
    info.SetVersion(APP_VERSION);
    info.SetDescription(
        "Cross-platform data logger for the Protek 506\n"
        "Digital Multimeter (DMM).\n\n"
        "Enable RS232 on meter:\n"
        "       MENU -> RS232 -> Enter");
    info.SetCopyright("(C) 2025-2026");
    info.AddDeveloper("m3p5 - C++ with wxWidgets");
    wxAboutBox(info, this);
}

// ============================================================
// Exit / Close
// ============================================================
void MainFrame::OnExit(wxCommandEvent&) { Close(true); }

void MainFrame::OnClose(wxCloseEvent& evt)
{
    SaveSettings();
    StopReaderThread();
    if (m_logging) m_logger.Close();
    evt.Skip();
}

// ============================================================
// Menu bar
// ============================================================
void MainFrame::BuildMenuBar()
{
    wxMenu* fileMenu = new wxMenu;
    fileMenu->Append(wxID_EXIT, "E&xit\tCtrl+Q");

    wxMenu* helpMenu = new wxMenu;
    helpMenu->Append(wxID_ABOUT, "&About...");

    wxMenuBar* bar = new wxMenuBar;
    bar->Append(fileMenu, "&File");
    bar->Append(helpMenu, "&Help");
    SetMenuBar(bar);
}
