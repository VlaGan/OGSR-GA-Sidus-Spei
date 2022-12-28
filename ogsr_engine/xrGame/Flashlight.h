/* #pragma once

#include "CustomDetector.h"


class CFlashlight : public CCustomDetector
{
private:
    typedef CCustomDetector inherited;

    shared_str flashlight_attach_bone;
    Fvector flashlight_omni_attach_offset{}, flashlight_world_attach_offset{}, flashlight_omni_world_attach_offset{};
    ref_light flashlight_render;
    ref_light flashlight_omni;
    ref_glow flashlight_glow;
    CLAItem* flashlight_lanim{};
    float flashlight_fBrightness{1.f};

    // firedeps m_current_firedeps{};
    Fvector flashlight_attach_offset{}, flashlight_pos{};
    bool has_flashlight{};

public:
    shared_str flashlight_light_bone;
    float flashlight_show_time{}, flashlight_show_hide_factor{};
    float flashlight_hide_time{};

    bool IsShown{}, isHidden{true};

    CFlashlight() = default;
    ~CFlashlight();
    virtual void Load(LPCSTR section);
    virtual void UpdateCL();
    void UpdateFlashlight();

    CFlashlight* cast_toch() { return this; }
};*/