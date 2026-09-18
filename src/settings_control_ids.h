#pragma once

// Control ids for the settings window.
//
// Split out of settings_dialog.cpp so that settings_fields.h can name controls
// without pulling in a Windows header - the field table is pure data.
//
// Ids are stable: they are the negotiation handle between the field table, the
// layout engine and the message handlers, all of which look widgets up by id.

// General page
#define IDC_COMBO_DEVICE       2001
#define IDC_HOST_EDIT          2002
#define IDC_PORT_EDIT          2003
#define IDC_BTN_REFRESH        2004
#define IDC_BTN_OK             2005
#define IDC_BTN_CANCEL         2006
#define IDC_COMBO_ANDROID_APP  2007
#define IDC_TRACKBAR_GAIN      2008
#define IDC_LABEL_GAIN         2009
#define IDC_CHECK_NS           2010
#define IDC_CHECK_AEC          2011
#define IDC_CHECK_AGC          2012
#define IDC_CHECK_EQ           2013
#define IDC_TRACKBAR_PRES      2014
#define IDC_LABEL_PRES         2015
#define IDC_TRACKBAR_BASS      2016
#define IDC_LABEL_BASS         2017
#define IDC_CHECK_COMP         2018
#define IDC_CHECK_NR           2019
#define IDC_TAB_MAIN           2020
#define IDC_BTN_RESET          2021
#define IDC_CHECK_DEBUG        2022
#define IDC_TRACKBAR_NRSTR     2023
#define IDC_LABEL_NRSTR        2024
#define IDC_CHECK_STARTUP      2025
#define IDC_LABEL_STARTUP_HINT 2026
#define IDC_COMBO_NR_BACKEND   2027
#define IDC_LABEL_NR_BACKEND_STATUS 2028
#define IDC_BTN_APPLY          2029
#define IDC_LABEL_DSP_CHAIN_STATUS 2031
#define IDC_LABEL_COMP_HINT    2032

// Timers
#define ID_TIMER_BACKEND_STATUS 2
