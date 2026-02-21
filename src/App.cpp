// ============================================================
//  Protek506Logger — App.cpp
// ============================================================
#include "App.h"
#include "MainFrame.h"

wxIMPLEMENT_APP(Protek506App);

bool Protek506App::OnInit()
{
    if (!wxApp::OnInit())
        return false;

    SetAppName("Protek506Logger");
    SetAppDisplayName("Protek 506 Logger");
    SetVendorName("OpenSource");

    MainFrame* frame = new MainFrame("Protek 506 DMM Logger");
    frame->Show(true);
    return true;
}

int Protek506App::OnExit()
{
    return wxApp::OnExit();
}
