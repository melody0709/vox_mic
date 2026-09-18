#pragma once

// The settings field table.
//
// Every option the settings window shows is declared exactly once, here. A row
// says WHAT the option is (label, control kind, where its value lives in Config
// and how it is validated) and never says WHERE it goes - the layout engine in
// settings_layout.cpp derives position, size, column and hint placement from
// settings_metrics.h.
//
// Adding an option therefore means adding one row to this file. If you find
// yourself writing a coordinate, a font or a CreateWindow call for a new option,
// the table is missing a concept - extend the table instead.
//
// Deliberately includes no Windows headers: this is pure data.
//
// Three rows carry behaviour that stays hand-written in settings_dialog.cpp
// because it cannot be expressed as a value binding:
//   * IDC_COMBO_DEVICE       - the option list comes from `adb devices`
//   * IDC_CHECK_STARTUP      - the value lives in the registry, not config.ini
//   * IDC_COMBO_ANDROID_APP  - the preset also rewrites socket + component
//
// Designated initializers must follow the member order of FieldSpec, so keep
// each row's designators in the order the struct declares them.

#include <string>

#include "config.h"
#include "settings_control_ids.h"

namespace settings {

enum class FieldKind {
    Text,    // single-line edit
    Number,  // integer edit
    Toggle,  // checkbox
    Slider,  // trackbar + value label
    Choice,  // combo box
    Status,  // read-only text, rewritten at runtime
    Note,    // static hint line with no bound control
};

// Where the row's value lives. Only the member matching `bind` is read.
enum class Bind : unsigned char { None, Str, Int, Bool, Float, StrEnum };

struct SliderSpec {
    int min;         // trackbar range, in trackbar units
    int max;
    int tic;
    float scale;     // config value = position * scale
    float lo;        // clamp applied when saving
    float hi;
    const char* fmt; // printf format for the value label, in config units
};

struct FieldSpec {
    int id;                     // control id, 0 for a decorative row
    FieldKind kind;
    Bind bind;
    const char* label;          // left column; nullptr = no label column
    const char* hint;           // static hint line; nullptr = none
    int hintLines;              // hint lines reserved below the row (0, 1, 2)
    int hintId;                 // give the hint line this id; 0 = none
    int dependsOn;              // id of the gating toggle; 0 = never gated
    bool sameRowAsPrevious;     // share the previous row's line
    bool ownerDraw;             // Status rows that carry a semantic colour
    int intMin;                 // Number rows: accepted range, inclusive
    int intMax;
    int trailId;                // trailing action button id; 0 = none
    const char* trailLabel;
    int valueId;                // slider value label id; 0 = none

    std::string Config::* bStr;
    int Config::* bInt;
    bool Config::* bBool;
    float Config::* bFloat;

    const char* const* enumValues;  // Bind::StrEnum: Config value per combo index
    int enumCount;
    const char* const* options;     // combo item labels; nullptr = runtime-filled
    int optionCount;
    SliderSpec slider;

    bool gated() const { return dependsOn != 0; }
};

struct SectionSpec {
    const char* title;  // nullptr = no section header
    const FieldSpec* fields;
    int count;
};

struct PageSpec {
    const char* tabTitle;
    const SectionSpec* sections;
    int count;
};

// --------------------------------------------------------------------------
// Option strings
// --------------------------------------------------------------------------

inline const char* const kAndroidAppOptions[] = {
    "Legacy AudioSource",
    "VoxMic Source (48 kHz)",
};

inline const char* const kDenoiseBackendOptions[] = {
    "RNNoise (built-in)",
    "DPDFNet (48 kHz model)",
};

// Values written to Config::denoiseBackend, indexed like kDenoiseBackendOptions.
inline const char* const kDenoiseBackendValues[] = {
    "rnnoise",
    "dpdfnet",
};

// --------------------------------------------------------------------------
// General page
// --------------------------------------------------------------------------

inline const FieldSpec kGeneralConnection[] = {
    {.id = IDC_COMBO_DEVICE, .kind = FieldKind::Choice, .bind = Bind::Str,
     .label = "ADB Device", .trailId = IDC_BTN_REFRESH, .trailLabel = "Refresh",
     .bStr = &Config::serial},
    {.id = IDC_HOST_EDIT, .kind = FieldKind::Text, .bind = Bind::Str,
     .label = "Host", .bStr = &Config::host},
    {.id = IDC_PORT_EDIT, .kind = FieldKind::Number, .bind = Bind::Int,
     .label = "Port", .intMin = 1, .intMax = 65535, .bInt = &Config::port},
};

inline const FieldSpec kGeneralSource[] = {
    {.id = IDC_COMBO_ANDROID_APP, .kind = FieldKind::Choice, .bind = Bind::Int,
     .label = "Android App", .bInt = &Config::androidAppPreset,
     .options = kAndroidAppOptions, .optionCount = 2},
    {.id = IDC_TRACKBAR_GAIN, .kind = FieldKind::Slider, .bind = Bind::Float,
     .label = "Gain", .valueId = IDC_LABEL_GAIN, .bFloat = &Config::gain,
     .slider = {25, 400, 25, 0.01f, 0.25f, 4.0f, "%.2fx"}},
};

inline const FieldSpec kGeneralEffects[] = {
    {.id = IDC_CHECK_NS, .kind = FieldKind::Toggle, .bind = Bind::Bool,
     .label = "NoiseSuppressor", .bBool = &Config::nsEnabled},
    {.id = IDC_CHECK_AEC, .kind = FieldKind::Toggle, .bind = Bind::Bool,
     .label = "AcousticEchoCanceler", .sameRowAsPrevious = true,
     .bBool = &Config::aecEnabled},
    {.id = IDC_CHECK_AGC, .kind = FieldKind::Toggle, .bind = Bind::Bool,
     .label = "AutomaticGainControl", .bBool = &Config::agcEnabled},
    {.id = 0, .kind = FieldKind::Note, .bind = Bind::None,
     .hint = "Connection, Android effects and debug console changes apply after restart.",
     .hintLines = 1},
};

inline const FieldSpec kGeneralApplication[] = {
    {.id = IDC_CHECK_DEBUG, .kind = FieldKind::Toggle, .bind = Bind::Bool,
     .label = "Show Debug Console", .bBool = &Config::debugConsole},
    // Registry-backed rather than config.ini-backed; see the file header.
    {.id = IDC_CHECK_STARTUP, .kind = FieldKind::Toggle, .bind = Bind::None,
     .label = "Start VoxMic with Windows", .hintLines = 1,
     .hintId = IDC_LABEL_STARTUP_HINT},
};

inline const SectionSpec kGeneralSections[] = {
    {"Connection", kGeneralConnection, 3},
    {"Android Source", kGeneralSource, 2},
    {"Android Audio Effects", kGeneralEffects, 4},
    {"Application", kGeneralApplication, 2},
};

// --------------------------------------------------------------------------
// DSP page
// --------------------------------------------------------------------------

inline const FieldSpec kDspStatus[] = {
    {.id = IDC_LABEL_DSP_CHAIN_STATUS, .kind = FieldKind::Status, .bind = Bind::None},
};

inline const FieldSpec kDspNoiseReduction[] = {
    {.id = IDC_CHECK_NR, .kind = FieldKind::Toggle, .bind = Bind::Bool,
     .label = "Enable noise reduction", .bBool = &Config::nrEnabled},
    {.id = IDC_COMBO_NR_BACKEND, .kind = FieldKind::Choice, .bind = Bind::StrEnum,
     .label = "Backend", .dependsOn = IDC_CHECK_NR,
     .bStr = &Config::denoiseBackend,
     .enumValues = kDenoiseBackendValues, .enumCount = 2,
     .options = kDenoiseBackendOptions, .optionCount = 2},
    {.id = IDC_LABEL_NR_BACKEND_STATUS, .kind = FieldKind::Status, .bind = Bind::None,
     .hintLines = 2, .ownerDraw = true},
    {.id = IDC_TRACKBAR_NRSTR, .kind = FieldKind::Slider, .bind = Bind::Float,
     .label = "NR Strength", .hintLines = 1, .dependsOn = IDC_CHECK_NR,
     .valueId = IDC_LABEL_NRSTR, .bFloat = &Config::nrStrength,
     .slider = {30, 95, 10, 0.01f, 0.3f, 0.95f, "%.2f"}},
};

inline const FieldSpec kDspTone[] = {
    {.id = IDC_CHECK_EQ, .kind = FieldKind::Toggle, .bind = Bind::Bool,
     .label = "EQ Enable", .bBool = &Config::eqEnabled},
    {.id = IDC_TRACKBAR_PRES, .kind = FieldKind::Slider, .bind = Bind::Float,
     .label = "Presence", .hint = "Boost vocal presence and articulation (1.7-3.7 kHz)",
     .hintLines = 1, .dependsOn = IDC_CHECK_EQ,
     .valueId = IDC_LABEL_PRES, .bFloat = &Config::eqPresence,
     .slider = {0, 80, 10, 0.1f, 0.0f, 8.0f, "+%.1f dB"}},
    {.id = IDC_TRACKBAR_BASS, .kind = FieldKind::Slider, .bind = Bind::Float,
     .label = "Bass Cut", .hint = "Reduce low-frequency rumble below 250 Hz",
     .hintLines = 1, .dependsOn = IDC_CHECK_EQ,
     .valueId = IDC_LABEL_BASS, .bFloat = &Config::eqBassCut,
     .slider = {0, 60, 10, -0.1f, -6.0f, 0.0f, "%.1f dB"}},
};

inline const FieldSpec kDspDynamics[] = {
    {.id = IDC_CHECK_COMP, .kind = FieldKind::Toggle, .bind = Bind::Bool,
     .label = "Compressor Enable",
     .hint = "Stabilizes voice volume with a fixed voice preset.", .hintLines = 1,
     .bBool = &Config::compressorEnabled},
};

inline const SectionSpec kDspSections[] = {
    {nullptr, kDspStatus, 1},
    {"Noise Reduction", kDspNoiseReduction, 4},
    {"Tone / EQ", kDspTone, 3},
    {"Dynamics", kDspDynamics, 1},
};

inline const PageSpec kPages[] = {
    {"General", kGeneralSections, 4},
    {"DSP", kDspSections, 4},
};

inline constexpr int kPageCount = 2;

}  // namespace settings
