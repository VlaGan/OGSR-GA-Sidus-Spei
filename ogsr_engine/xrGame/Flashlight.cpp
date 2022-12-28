//////////////////////////////////////////////////////////
// Date: 10.12.2022
// VlaGan: Ну, типо класс ручного фонарика
// Основано на тактическом фонаре ОГСР ГА
//////////////////////////////////////////////////////////
/*
#include "Flashlight.h"
#include"..\xr_3da\LightAnimLibrary.h"

CFlashlight::~CFlashlight()
{
    flashlight_render.destroy();
    flashlight_omni.destroy();
    flashlight_render.destroy();
}

void CFlashlight::Load(LPCSTR section)
{
    inherited::Load(section);

    if (!flashlight_render && pSettings->line_exist(section, "flashlight_section"))
    {
        has_flashlight = true;

        flashlight_attach_bone = pSettings->r_string(section, "torch_light_bone");
        flashlight_attach_offset = Fvector{pSettings->r_float(section, "torch_attach_offset_x"), pSettings->r_float(section, "torch_attach_offset_y"),
                                           pSettings->r_float(section, "torch_attach_offset_z")};
        flashlight_omni_attach_offset = Fvector{pSettings->r_float(section, "torch_omni_attach_offset_x"), pSettings->r_float(section, "torch_omni_attach_offset_y"),
                                                pSettings->r_float(section, "torch_omni_attach_offset_z")};
        flashlight_world_attach_offset = Fvector{pSettings->r_float(section, "torch_world_attach_offset_x"), pSettings->r_float(section, "torch_world_attach_offset_y"),
                                                 pSettings->r_float(section, "torch_world_attach_offset_z")};
        flashlight_omni_world_attach_offset =
            Fvector{pSettings->r_float(section, "torch_omni_world_attach_offset_x"), pSettings->r_float(section, "torch_omni_world_attach_offset_y"),
                    pSettings->r_float(section, "torch_omni_world_attach_offset_z")};

        const bool b_r2 = psDeviceFlags.test(rsR2) || psDeviceFlags.test(rsR3) || psDeviceFlags.test(rsR4);

        const char* m_light_section = pSettings->r_string(section, "flashlight_section");

        flashlight_lanim = LALib.FindItem(READ_IF_EXISTS(pSettings, r_string, m_light_section, "color_animator", ""));

        flashlight_render = ::Render->light_create();
        flashlight_render->set_type(IRender_Light::SPOT);
        flashlight_render->set_shadow(true);

        const Fcolor clr = READ_IF_EXISTS(pSettings, r_fcolor, m_light_section, b_r2 ? "color_r2" : "color", (Fcolor{0.6f, 0.55f, 0.55f, 1.0f}));
        flashlight_fBrightness = clr.intensity();
        flashlight_render->set_color(clr);
        const float range = READ_IF_EXISTS(pSettings, r_float, m_light_section, b_r2 ? "range_r2" : "range", 50.f);
        flashlight_render->set_range(range);
        flashlight_render->set_cone(deg2rad(READ_IF_EXISTS(pSettings, r_float, m_light_section, "spot_angle", 60.f)));
        flashlight_render->set_texture(READ_IF_EXISTS(pSettings, r_string, m_light_section, "spot_texture", nullptr));

        flashlight_omni = ::Render->light_create();
        flashlight_omni->set_type(
            (IRender_Light::LT)(READ_IF_EXISTS(pSettings, r_u8, m_light_section, "omni_type",
                                               2))); // KRodin: вообще omni это обычно поинт, но поинт светит во все стороны от себя, поэтому тут спот используется по умолчанию.
        flashlight_omni->set_shadow(false);

        const Fcolor oclr = READ_IF_EXISTS(pSettings, r_fcolor, m_light_section, b_r2 ? "omni_color_r2" : "omni_color", (Fcolor{1.0f, 1.0f, 1.0f, 0.0f}));
        flashlight_omni->set_color(oclr);
        const float orange = READ_IF_EXISTS(pSettings, r_float, m_light_section, b_r2 ? "omni_range_r2" : "omni_range", 0.25f);
        flashlight_omni->set_range(orange);

        flashlight_glow = ::Render->glow_create();
        flashlight_glow->set_texture(READ_IF_EXISTS(pSettings, r_string, m_light_section, "glow_texture", "glow\\glow_torch_r2"));
        flashlight_glow->set_color(clr);
        flashlight_glow->set_radius(READ_IF_EXISTS(pSettings, r_float, m_light_section, "glow_radius", 0.3f));

        flashlight_light_bone = READ_IF_EXISTS(pSettings, r_string, section, "torch_cone_bones", "");
        flashlight_show_time = READ_IF_EXISTS(pSettings, r_float, section, "torch_enable_time_anm_show", 0.6f);
        flashlight_hide_time = READ_IF_EXISTS(pSettings, r_float, section, "torch_disable_time_anm_hide", 0.17f);
    }
}

void CFlashlight::UpdateCL()
{
    inherited::UpdateCL();

    if (has_flashlight && HudItemData())
        UpdateFlashlight();
    else
    {
        // Костыль на выключение при выбрасывании активного фонаря
        if (flashlight_render)
            if (flashlight_render->get_active())
            {
                flashlight_render->set_active(false);
                flashlight_omni->set_active(false);
                flashlight_glow->set_active(false);
            }
    }
}

void CFlashlight::UpdateFlashlight()
{
    /* if (auto active_item = g_actor->inventory().ActiveItem())
   {
       auto wpn = smart_cast<CWeapon*>(active_item);
       if (this->GetState() == eShowing && !IsShown)
           if (flashlight_show_hide_factor < flashlight_fast_show_time)
               flashlight_show_hide_factor += Device.fTimeDelta;
           else
           {
               flashlight_show_hide_factor = 0;
               IsShown = true;
           }
       else if (this->GetState() == eHiding) //&& IsShown)
           if (flashlight_show_hide_factor < flashlight_fast_hide_time)
               flashlight_show_hide_factor += Device.fTimeDelta;
           else
           {
               flashlight_show_hide_factor = 0;
               IsShown = false;
           }
   }
   else
   {*/
    if (this->GetState() == eShowing && !IsShown)
        if (flashlight_show_hide_factor < flashlight_show_time)
            flashlight_show_hide_factor += Device.fTimeDelta;
        else
        {
            flashlight_show_hide_factor = 0;
            IsShown = true;
        }
    else if (this->GetState() == eHiding) //&& IsShown)
        if (flashlight_show_hide_factor < flashlight_hide_time)
            flashlight_show_hide_factor += Device.fTimeDelta;
        else
        {
            flashlight_show_hide_factor = 0;
            IsShown = false;
        }
    //}

    if (flashlight_light_bone.size())
        HudItemData()->set_bone_visible(flashlight_light_bone, IsShown, TRUE);

    if (flashlight_render)
    {
        if (IsShown)
        {
            flashlight_render->set_active(true);
            flashlight_omni->set_active(true);
            flashlight_glow->set_active(true);
        }
        else
        {
            flashlight_render->set_active(false);
            flashlight_omni->set_active(false);
            flashlight_glow->set_active(false);
        }

        if (flashlight_render->get_active())
        {
            Fvector flashlight_pos_omni, flashlight_dir, flashlight_dir_omni;

            if (GetHUDmode())
            {
                GetBoneOffsetPosDir(flashlight_attach_bone, flashlight_pos, flashlight_dir, flashlight_attach_offset);
                CorrectDirFromWorldToHud(flashlight_dir);

                GetBoneOffsetPosDir(flashlight_attach_bone, flashlight_pos_omni, flashlight_dir_omni, flashlight_omni_attach_offset);
                CorrectDirFromWorldToHud(flashlight_dir_omni);
            }
            // VlaGan: А нужно ли это тут? Свет фонарика будем рендерить онли когда он худово активен
            /* else
            {
                firedeps dep;
                HudItemData()->setup_firedeps(dep);

                flashlight_dir = dep.vLastFP;
                XFORM().transform_tiny(flashlight_pos, flashlight_world_attach_offset);

                flashlight_dir_omni = dep.vLastFP;
                XFORM().transform_tiny(flashlight_pos_omni, flashlight_omni_world_attach_offset);
            }

            Fmatrix flashlightXForm;
            flashlightXForm.identity();
            flashlightXForm.k.set(flashlight_dir);
            Fvector::generate_orthonormal_basis_normalized(flashlightXForm.k, flashlightXForm.j, flashlightXForm.i);
            flashlight_render->set_position(flashlight_pos);
            flashlight_render->set_rotation(flashlightXForm.k, flashlightXForm.i);

            flashlight_glow->set_position(flashlight_pos);
            flashlight_glow->set_direction(flashlightXForm.k);

            Fmatrix flashlightomniXForm;
            flashlightomniXForm.identity();
            flashlightomniXForm.k.set(flashlight_dir_omni);
            Fvector::generate_orthonormal_basis_normalized(flashlightomniXForm.k, flashlightomniXForm.j, flashlightomniXForm.i);
            flashlight_omni->set_position(flashlight_pos_omni);
            flashlight_omni->set_rotation(flashlightomniXForm.k, flashlightomniXForm.i);

            // calc color animator
            if (flashlight_lanim)
            {
                int frame;
                const u32 clr = flashlight_lanim->CalculateBGR(Device.fTimeGlobal, frame);

                Fcolor fclr{(float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f};
                fclr.mul_rgb(flashlight_fBrightness / 255.f);
                flashlight_render->set_color(fclr);
                flashlight_omni->set_color(fclr);
                flashlight_glow->set_color(fclr);
            }
        }
    }
}*/