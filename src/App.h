#pragma once
// ============================================================
//  Protek506Logger — App.h
//  wxWidgets application entry point
// ============================================================
#include <wx/wx.h>

class Protek506App : public wxApp
{
public:
    virtual bool OnInit() override;
    virtual int  OnExit() override;
};

wxDECLARE_APP(Protek506App);
