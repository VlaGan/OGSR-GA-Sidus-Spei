// Weapon.cpp: implementation of the CWeapon class.
//
//////////////////////////////////////////////////////////////////////
#include "stdafx.h"

#include "Weapon.h"
#include "ParticlesObject.h"
#include "entity_alive.h"
#include "player_hud.h"
#include "inventory_item_impl.h"

#include "inventory.h"
#include "xrserver_objects_alife_items.h"

#include "actor.h"
#include "actoreffector.h"
#include "level.h"

#include "xr_level_controller.h"
#include "game_cl_base.h"
#include "../Include/xrRender/Kinematics.h"
#include "ai_object_location.h"
#include "clsid_game.h"
#include "object_broker.h"
#include "../xr_3da/LightAnimLibrary.h"
#include "game_object_space.h"
#include "script_game_object.h"

#include "GamePersistent.h"
#include "../xr_3da/x_ray.h"

#define ROTATION_TIME 0.25f

extern ENGINE_API Fvector4 w_states;
extern ENGINE_API Fvector3 w_timers;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CWeapon::CWeapon(LPCSTR name)
{
    SetState(eHidden);
    SetNextState(eHidden);
    m_sub_state = eSubstateReloadBegin;
    m_idle_state = eIdle;
    m_bTriStateReload = false;
    SetDefaults();

    m_Offset.identity();
    m_StrapOffset.identity();

    iAmmoCurrent = -1;
    m_dwAmmoCurrentCalcFrame = 0;

    iAmmoElapsed = -1;
    iMagazineSize = -1;
    m_ammoType = 0;

    eHandDependence = hdNone;

    m_fZoomFactor = Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system) ? 1.f : g_fov;

    m_fZoomRotationFactor = 0.f;

    m_pAmmo = nullptr;

    m_pFlameParticles2 = nullptr;
    m_sFlameParticles2 = nullptr;

    m_fCurrentCartirdgeDisp = 1.f;

    m_strap_bone0 = nullptr;
    m_strap_bone1 = nullptr;
    m_StrapOffset.identity();
    m_strapped_mode = false;
    m_can_be_strapped = false;
    m_ef_main_weapon_type = u32(-1);
    m_ef_weapon_type = u32(-1);
    m_UIScope = nullptr;
    m_set_next_ammoType_on_reload = u32(-1);
}

CWeapon::~CWeapon()
{
    xr_delete(m_UIScope);

    laser_light_render.destroy();
    flashlight_render.destroy();
    flashlight_omni.destroy();
    flashlight_glow.destroy();
}

void CWeapon::Hit(SHit* pHDS) { inherited::Hit(pHDS); }

void CWeapon::UpdateXForm()
{
    if (Device.dwFrame != dwXF_Frame)
    {
        dwXF_Frame = Device.dwFrame;

        if (0 == H_Parent())
            return;

        // Get access to entity and its visual
        CEntityAlive* E = smart_cast<CEntityAlive*>(H_Parent());

        if (!E)
            return;

        const CInventoryOwner* parent = smart_cast<const CInventoryOwner*>(E);
        if (!parent || parent && parent->use_simplified_visual())
            return;

        if (parent->attached(this))
            return;

        R_ASSERT(E);
        IKinematics* V = smart_cast<IKinematics*>(E->Visual());
        VERIFY(V);

        // Get matrices
        int boneL{BI_NONE}, boneR{BI_NONE}, boneR2{BI_NONE};

        E->g_WeaponBones(boneL, boneR, boneR2);

        if ((HandDependence() == hd1Hand) || (GetState() == eReload) || (!E->g_Alive()))
            boneL = boneR2;

        // KRodin: видимо такое случается иногда у некоторых визуалов нпс. Например если создать нпс с визуалом монстра наверно.
        if (boneL == BI_NONE || boneR == BI_NONE)
            return;

        // от mortan:
        // https://www.gameru.net/forum/index.php?s=&showtopic=23443&view=findpost&p=1677678
        V->CalculateBones_Invalidate();
        V->CalculateBones(true); // V->CalculateBones	();
        Fmatrix& mL = V->LL_GetTransform(u16(boneL));
        Fmatrix& mR = V->LL_GetTransform(u16(boneR));
        // Calculate
        Fmatrix mRes;
        Fvector R, D, N;
        D.sub(mL.c, mR.c);

        if (fis_zero(D.magnitude()))
        {
            mRes.set(E->XFORM());
            mRes.c.set(mR.c);
        }
        else
        {
            D.normalize();
            R.crossproduct(mR.j, D);

            N.crossproduct(D, R);
            N.normalize();

            mRes.set(R, N, D, mR.c);
            mRes.mulA_43(E->XFORM());
        }

        UpdatePosition(mRes);
    }
}

void CWeapon::UpdateFireDependencies_internal()
{
    if (skip_updated_frame == Device.dwFrame || Device.dwFrame != dwFP_Frame)
    {
        dwFP_Frame = Device.dwFrame;

        UpdateXForm();

        if (GetHUDmode())
        {
            HudItemData()->setup_firedeps(m_current_firedeps);
            VERIFY(_valid(m_current_firedeps.m_FireParticlesXForm));
        }
        else
        {
            // 3rd person or no parent
            Fmatrix& parent = XFORM();
            Fvector& fp = vLoadedFirePoint;
            Fvector& fp2 = vLoadedFirePoint2;
            Fvector& sp = vLoadedShellPoint;

            parent.transform_tiny(m_current_firedeps.vLastFP, fp);
            parent.transform_tiny(m_current_firedeps.vLastFP2, fp2);
            parent.transform_tiny(m_current_firedeps.vLastSP, sp);
            parent.transform_tiny(m_current_firedeps.vLastShootPoint, fp);

            m_current_firedeps.vLastFD.set(0.f, 0.f, 1.f);
            parent.transform_dir(m_current_firedeps.vLastFD);

            m_current_firedeps.m_FireParticlesXForm.set(parent);
            VERIFY(_valid(m_current_firedeps.m_FireParticlesXForm));
        }
    }
}

void CWeapon::ForceUpdateFireParticles()
{
    if (!GetHUDmode())
    { // update particlesXFORM real bullet direction

        if (!H_Parent())
            return;

        Fvector p, d;
        smart_cast<CEntity*>(H_Parent())->g_fireParams(this, p, d);

        Fmatrix _pxf;
        _pxf.k = d;
        _pxf.i.crossproduct(Fvector().set(0.0f, 1.0f, 0.0f), _pxf.k);
        _pxf.j.crossproduct(_pxf.k, _pxf.i);
        _pxf.c = XFORM().c;

        m_current_firedeps.m_FireParticlesXForm.set(_pxf);
    }
}

constexpr const char* wpn_scope_def_bone = "wpn_scope";
constexpr const char* wpn_silencer_def_bone = "wpn_silencer";
constexpr const char* wpn_launcher_def_bone_shoc = "wpn_launcher";
constexpr const char* wpn_launcher_def_bone_cop = "wpn_grenade_launcher";

//constexpr const char* wpn_bayonet_bone = "wpn_bayonet";

void CWeapon::Load(LPCSTR section)
{
    inherited::Load(section);
    CShootingObject::Load(section);

    if (pSettings->line_exist(section, "flame_particles_2"))
        m_sFlameParticles2 = pSettings->r_string(section, "flame_particles_2");

    if (!m_bForcedParticlesHudMode)
        m_bParticlesHudMode = !!pSettings->line_exist(hud_sect, "item_visual");

#ifdef DEBUG
    {
        Fvector pos, ypr;
        pos = pSettings->r_fvector3(section, "position");
        ypr = pSettings->r_fvector3(section, "orientation");
        ypr.mul(PI / 180.f);

        m_Offset.setHPB(ypr.x, ypr.y, ypr.z);
        m_Offset.translate_over(pos);
    }

    m_StrapOffset = m_Offset;
    if (pSettings->line_exist(section, "strap_position") && pSettings->line_exist(section, "strap_orientation"))
    {
        Fvector pos, ypr;
        pos = pSettings->r_fvector3(section, "strap_position");
        ypr = pSettings->r_fvector3(section, "strap_orientation");
        ypr.mul(PI / 180.f);

        m_StrapOffset.setHPB(ypr.x, ypr.y, ypr.z);
        m_StrapOffset.translate_over(pos);
    }
#endif

    // load ammo classes
    m_ammoTypes.clear();
    LPCSTR S = pSettings->r_string(section, "ammo_class");
    if (S && S[0])
    {
        string128 _ammoItem;
        int count = _GetItemCount(S);
        for (int it = 0; it < count; ++it)
        {
            _GetItem(S, it, _ammoItem);
            m_ammoTypes.push_back(_ammoItem);
        }
    }

    iAmmoElapsed = pSettings->r_s32(section, "ammo_elapsed");
    iMagazineSize = pSettings->r_s32(section, "ammo_mag_size");

    ////////////////////////////////////////////////////
    // дисперсия стрельбы

    //подбрасывание камеры во время отдачи
    camMaxAngle = pSettings->r_float(section, "cam_max_angle");
    camMaxAngle = deg2rad(camMaxAngle);
    camRelaxSpeed = pSettings->r_float(section, "cam_relax_speed");
    camRelaxSpeed = deg2rad(camRelaxSpeed);
    if (pSettings->line_exist(section, "cam_relax_speed_ai"))
    {
        camRelaxSpeed_AI = pSettings->r_float(section, "cam_relax_speed_ai");
        camRelaxSpeed_AI = deg2rad(camRelaxSpeed_AI);
    }
    else
    {
        camRelaxSpeed_AI = camRelaxSpeed;
    }

    //	camDispersion		= pSettings->r_float		(section,"cam_dispersion"	);
    //	camDispersion		= deg2rad					(camDispersion);

    camMaxAngleHorz = pSettings->r_float(section, "cam_max_angle_horz");
    camMaxAngleHorz = deg2rad(camMaxAngleHorz);
    camStepAngleHorz = pSettings->r_float(section, "cam_step_angle_horz");
    camStepAngleHorz = deg2rad(camStepAngleHorz);
    camDispertionFrac = READ_IF_EXISTS(pSettings, r_float, section, "cam_dispertion_frac", 0.7f);
    //  [8/2/2005]
    // m_fParentDispersionModifier = READ_IF_EXISTS(pSettings, r_float, section, "parent_dispersion_modifier",1.0f);
    m_fPDM_disp_base = READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_base", 1.0f);
    m_fPDM_disp_vel_factor = READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_vel_factor", 1.0f);
    m_fPDM_disp_accel_factor = READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_accel_factor", 1.0f);
    m_fPDM_disp_crouch = READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_crouch", 1.0f);
    m_fPDM_disp_crouch_no_acc = READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_crouch_no_acc", 1.0f);
    //  [8/2/2005]

    fireDispersionConditionFactor = pSettings->r_float(section, "fire_dispersion_condition_factor");
    misfireProbability = pSettings->r_float(section, "misfire_probability");
    misfireConditionK = READ_IF_EXISTS(pSettings, r_float, section, "misfire_condition_k", 1.0f);
    conditionDecreasePerShot = pSettings->r_float(section, "condition_shot_dec");
    conditionDecreasePerShotOnHit = READ_IF_EXISTS(pSettings, r_float, section, "condition_shot_dec_on_hit", 0.f);
    conditionDecreasePerShotSilencer = READ_IF_EXISTS(pSettings, r_float, section, "condition_shot_dec_silencer", conditionDecreasePerShot);

    vLoadedFirePoint = pSettings->r_fvector3(section, "fire_point");

    if (pSettings->line_exist(section, "fire_point2"))
        vLoadedFirePoint2 = pSettings->r_fvector3(section, "fire_point2");
    else
        vLoadedFirePoint2 = vLoadedFirePoint;

    // hands
    eHandDependence = EHandDependence(pSettings->r_s32(section, "hand_dependence"));
    m_bIsSingleHanded = true;
    if (pSettings->line_exist(section, "single_handed"))
        m_bIsSingleHanded = !!pSettings->r_bool(section, "single_handed");
    //
    m_fMinRadius = pSettings->r_float(section, "min_radius");
    m_fMaxRadius = pSettings->r_float(section, "max_radius");

    // информация о возможных апгрейдах и их визуализации в инвентаре
    m_eScopeStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "scope_status");
    m_eSilencerStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "silencer_status");
    m_eGrenadeLauncherStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "grenade_launcher_status");
    m_eBayonetStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "bayonet_status");
    m_eRailStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "rail_status");
    m_eLaserStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "laser_status");
    m_eTorchStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "torch_status");
    m_eTactHandlerStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "tact_handler_status");

    m_bZoomEnabled = !!pSettings->r_bool(section, "zoom_enabled");
    m_bUseScopeZoom = !!READ_IF_EXISTS(pSettings, r_bool, section, "use_scope_zoom", false);
    m_bUseScopeGrenadeZoom = !!READ_IF_EXISTS(pSettings, r_bool, section, "use_scope_grenade_zoom", false);
    m_bUseScopeDOF = !!READ_IF_EXISTS(pSettings, r_bool, section, "use_scope_dof", true);
    m_bForceScopeDOF = !!READ_IF_EXISTS(pSettings, r_bool, section, "force_scope_dof", false);
    m_bScopeShowIndicators = !!READ_IF_EXISTS(pSettings, r_bool, section, "scope_show_indicators", true);
    m_bIgnoreScopeTexture = !!READ_IF_EXISTS(pSettings, r_bool, section, "ignore_scope_texture", false);

    m_fZoomRotateTime = READ_IF_EXISTS(pSettings, r_float, hud_sect, "zoom_rotate_time", ROTATION_TIME);

    m_bScopeDynamicZoom = false;
    m_fScopeZoomFactor = 0;
    m_fRTZoomFactor = 0;

    m_fZoomFactor = CurrentZoomFactor();

    m_allScopeNames.clear();
    m_highlightAddons.clear();
    if (m_eScopeStatus == ALife::eAddonAttachable)
    {
        m_sScopeName = pSettings->r_string(section, "scope_name");
        m_iScopeX = pSettings->r_s32(section, "scope_x");
        m_iScopeY = pSettings->r_s32(section, "scope_y");

        m_allScopeNames.push_back(m_sScopeName);
        if (pSettings->line_exist(section, "scope_names"))
        {
            LPCSTR S = pSettings->r_string(section, "scope_names");
            if (S && S[0])
            {
                string128 _scopeItem;
                int count = _GetItemCount(S);
                for (int it = 0; it < count; ++it)
                {
                    _GetItem(S, it, _scopeItem);
                    m_allScopeNames.push_back(_scopeItem);
                    m_highlightAddons.push_back(_scopeItem);
                }
            }
        }
    }

    if (m_eSilencerStatus == ALife::eAddonAttachable)
    {
        m_sSilencerName = pSettings->r_string(section, "silencer_name");
        m_iSilencerX = pSettings->r_s32(section, "silencer_x");
        m_iSilencerY = pSettings->r_s32(section, "silencer_y");
    }

    if (m_eGrenadeLauncherStatus == ALife::eAddonAttachable)
    {
        m_sGrenadeLauncherName = pSettings->r_string(section, "grenade_launcher_name");
        m_iGrenadeLauncherX = pSettings->r_s32(section, "grenade_launcher_x");
        m_iGrenadeLauncherY = pSettings->r_s32(section, "grenade_launcher_y");
    }

    if (m_eBayonetStatus == ALife::eAddonAttachable)
    {
        m_sBayonetName = pSettings->r_string(section, "bayonet_name");
        m_iBayonetX = pSettings->r_s32(section, "bayonet_x");
        m_iBayonetY = pSettings->r_s32(section, "bayonet_y");
    }

    if (m_eRailStatus == ALife::eAddonAttachable)
    {
        m_sRailName = pSettings->r_string(section, "rail_name");
        m_iRailX = pSettings->r_s32(section, "rail_x");
        m_iRailY = pSettings->r_s32(section, "rail_y");
    }
    if (m_eLaserStatus == ALife::eAddonAttachable)
    {
        m_sLLaserName = pSettings->r_string(section, "laser_name");
        m_iLaserX = pSettings->r_s32(section, "laser_x");
        m_iLaserY = pSettings->r_s32(section, "laser_y");
    }
    if (m_eTorchStatus == ALife::eAddonAttachable)
    {
        m_sTorchName = pSettings->r_string(section, "torch_name");
        m_iTorchX = pSettings->r_s32(section, "torch_x");
        m_iTorchY = pSettings->r_s32(section, "torch_y");
    }
    if (m_eTactHandlerStatus == ALife::eAddonAttachable)
    {
        m_sTactHandlerName = pSettings->r_string(section, "tact_handler_name");
        m_iTactHandlerX = pSettings->r_s32(section, "tact_handler_x");
        m_iTactHandlerY = pSettings->r_s32(section, "tact_handler_y");
        tact_handler_disp_base = pSettings->r_float(m_sTactHandlerName, "PDM_disp_base");
        tact_handler_vel_factor = pSettings->r_float(m_sTactHandlerName, "PDM_vel_factor");
        tact_handler_accel_factor = pSettings->r_float(m_sTactHandlerName, "PDM_accel_factor");

        tact_handler_camRelaxSpeed = pSettings->r_u32(m_sTactHandlerName, "cam_relax_speed");
        tact_handler_camDispersion = pSettings->r_u32(m_sTactHandlerName, "cam_disp");
        tact_handler_camDispersionInc = pSettings->r_u32(m_sTactHandlerName, "cam_disp_inc");

        tact_handler_camMaxAngle = pSettings->r_u32(m_sTactHandlerName, "cam_max_angle");
        tact_handler_camMaxAngleHorz = pSettings->r_u32(m_sTactHandlerName, "cam_max_angle_horz");
        tact_handler_camStepAngleHorz = pSettings->r_u32(m_sTactHandlerName, "cam_step_angle_horz");

        //TODO: Need zoom params
    }

    // Кости мировой модели оружия
    if (pSettings->line_exist(section, "scope_bone"))
    {
        const char* S = pSettings->r_string(section, "scope_bone");
        if (S && strlen(S))
        {
            const int count = _GetItemCount(S);
            string128 _scope_bone{};
            for (int it = 0; it < count; ++it)
            {
                _GetItem(S, it, _scope_bone);
                m_sWpn_scope_bones.push_back(_scope_bone);
            }
        }
        else
            m_sWpn_scope_bones.push_back(wpn_scope_def_bone);
    }
    else
        m_sWpn_scope_bones.push_back(wpn_scope_def_bone);

    m_sWpn_silencer_bone = READ_IF_EXISTS(pSettings, r_string, section, "silencer_bone", wpn_silencer_def_bone);
    m_sWpn_launcher_bone = READ_IF_EXISTS(pSettings, r_string, section, "launcher_bone", wpn_launcher_def_bone_shoc);
    m_sWpn_laser_bone = READ_IF_EXISTS(pSettings, r_string, section, "laser_ray_bones", "");
    m_sWpn_flashlight_bone = READ_IF_EXISTS(pSettings, r_string, section, "torch_cone_bones", "");
    m_sWpn_bayonet_bone = READ_IF_EXISTS(pSettings, r_string, section, "bayonet_bone", "");
    m_sWpn_rail_bone = READ_IF_EXISTS(pSettings, r_string, section, "rail_bone", "");
    m_sWpn_llaser_bone = READ_IF_EXISTS(pSettings, r_string, section, "laser_bone", "");
    m_sWpn_torch_bone = READ_IF_EXISTS(pSettings, r_string, section, "torch_bone", "");
    m_sWpn_tacthandler_bone = READ_IF_EXISTS(pSettings, r_string, section, "tact_handler_bone", "");

    if (pSettings->line_exist(section, "hidden_bones"))
    {
        const char* S = pSettings->r_string(section, "hidden_bones");
        if (S && strlen(S))
        {
            const int count = _GetItemCount(S);
            string128 _hidden_bone{};
            for (int it = 0; it < count; ++it)
            {
                _GetItem(S, it, _hidden_bone);
                hidden_bones.push_back(_hidden_bone);
            }
        }
    }

    // Кости худовой модели оружия - если не прописаны, используются имена из конфига мировой модели.
    if (pSettings->line_exist(hud_sect, "scope_bone"))
    {
        const char* S = pSettings->r_string(hud_sect, "scope_bone");
        if (S && strlen(S))
        {
            const int count = _GetItemCount(S);
            string128 _scope_bone{};
            for (int it = 0; it < count; ++it)
            {
                _GetItem(S, it, _scope_bone);
                m_sHud_wpn_scope_bones.push_back(_scope_bone);
            }
        }
        else
            m_sHud_wpn_scope_bones = m_sWpn_scope_bones;
    }
    else
    m_sHud_wpn_scope_bones = m_sWpn_scope_bones;
    m_sHud_wpn_silencer_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "silencer_bone", m_sWpn_silencer_bone);
    m_sHud_wpn_launcher_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "launcher_bone", m_sWpn_launcher_bone);
    m_sHud_wpn_laser_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "laser_ray_bones", m_sWpn_laser_bone);
    m_sHud_wpn_flashlight_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "torch_cone_bones", m_sWpn_flashlight_bone);
    m_sHud_wpn_bayonet_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "bayonet_bone", m_sWpn_bayonet_bone);
    m_sHud_wpn_rail_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "rail_bone", m_sWpn_rail_bone);
    m_sHud_wpn_llaser_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "laser_bone", m_sWpn_llaser_bone);
    m_sHud_wpn_torch_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "torch_bone", m_sWpn_torch_bone);
    m_sHud_wpn_tacthandler_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "tact_handler_bone", m_sWpn_tacthandler_bone);

    if (pSettings->line_exist(hud_sect, "hidden_bones"))
    {
        const char* S = pSettings->r_string(hud_sect, "hidden_bones");
        if (S && strlen(S))
        {
            const int count = _GetItemCount(S);
            string128 _hidden_bone{};
            for (int it = 0; it < count; ++it)
            {
                _GetItem(S, it, _hidden_bone);
                hud_hidden_bones.push_back(_hidden_bone);
            }
        }
    }
    else
        hud_hidden_bones = hidden_bones;

    //Можно и из конфига прицела читать и наоборот! Пока так.
    m_fSecondVPZoomFactor = 0.0f;
    m_fZoomHudFov = 0.0f;
    m_fSecondVPHudFov = 0.0f;
    m_fScopeInertionFactor = m_fControlInertionFactor;

    InitAddons();

    m_bHideCrosshairInZoom = true;
    if (pSettings->line_exist(hud_sect, "zoom_hide_crosshair"))
        m_bHideCrosshairInZoom = !!pSettings->r_bool(hud_sect, "zoom_hide_crosshair");

    m_bZoomInertionAllow =
        READ_IF_EXISTS(pSettings, r_bool, hud_sect, "allow_zoom_inertion", IS_OGSR_GA ? true : READ_IF_EXISTS(pSettings, r_bool, "features", "default_allow_zoom_inertion", true));
    m_bScopeZoomInertionAllow = READ_IF_EXISTS(pSettings, r_bool, hud_sect, "allow_scope_zoom_inertion",
                                               IS_OGSR_GA ? true : READ_IF_EXISTS(pSettings, r_bool, "features", "default_allow_scope_zoom_inertion", true));

    //////////////////////////////////////////////////////////

    m_bHasTracers = READ_IF_EXISTS(pSettings, r_bool, section, "tracers", true);
    m_u8TracerColorID = READ_IF_EXISTS(pSettings, r_u8, section, "tracers_color_ID", u8(-1));

    string256 temp;
    for (int i = egdNovice; i < egdCount; ++i)
    {
        strconcat(sizeof(temp), temp, "hit_probability_", get_token_name(difficulty_type_token, i));
        m_hit_probability[i] = READ_IF_EXISTS(pSettings, r_float, section, temp, 1.f);
    }

    if (pSettings->line_exist(section, "highlight_addons"))
    {
        LPCSTR S = pSettings->r_string(section, "highlight_addons");
        if (S && S[0])
        {
            string128 _addonItem;
            int count = _GetItemCount(S);
            for (int it = 0; it < count; ++it)
            {
                _GetItem(S, it, _addonItem);
                ASSERT_FMT(pSettings->section_exist(_addonItem), "Section [%s] not found!", _addonItem);
                m_highlightAddons.emplace_back(_addonItem);
            }
        }
    }

    if (!laser_light_render && pSettings->line_exist(section, "laser_light_section"))
    {
        has_laser = true;

        laserdot_attach_bone = READ_IF_EXISTS(pSettings, r_string, section, "laserdot_attach_bone", "");
        laserdot_attach_offset =
            Fvector{READ_IF_EXISTS(pSettings, r_float, section, "laserdot_attach_offset_x", 0.0f), READ_IF_EXISTS(pSettings, r_float, section, "laserdot_attach_offset_y", 0.0f),
                    READ_IF_EXISTS(pSettings, r_float, section, "laserdot_attach_offset_z", 0.0f)};
        laserdot_world_attach_offset = Fvector{READ_IF_EXISTS(pSettings, r_float, section, "laserdot_world_attach_offset_x", 0.0f),
                                               READ_IF_EXISTS(pSettings, r_float, section, "laserdot_world_attach_offset_y", 0.0f),
                                               READ_IF_EXISTS(pSettings, r_float, section, "laserdot_world_attach_offset_z", 0.0f)};

        const bool b_r2 = psDeviceFlags.test(rsR2) || psDeviceFlags.test(rsR3) || psDeviceFlags.test(rsR4);

        const char* m_light_section = pSettings->r_string(section, "laser_light_section");

        laser_lanim = LALib.FindItem(READ_IF_EXISTS(pSettings, r_string, m_light_section, "color_animator", ""));

        laser_light_render = ::Render->light_create();
        laser_light_render->set_type(IRender_Light::SPOT);
        laser_light_render->set_shadow(true);

        const Fcolor clr = READ_IF_EXISTS(pSettings, r_fcolor, m_light_section, b_r2 ? "color_r2" : "color", (Fcolor{1.0f, 0.0f, 0.0f, 1.0f}));
        laser_fBrightness = clr.intensity();
        laser_light_render->set_color(clr);
        const float range = READ_IF_EXISTS(pSettings, r_float, m_light_section, b_r2 ? "range_r2" : "range", 100.f);
        laser_light_render->set_range(range);
        laser_light_render->set_cone(deg2rad(READ_IF_EXISTS(pSettings, r_float, m_light_section, "spot_angle", 1.f)));
        laser_light_render->set_texture(READ_IF_EXISTS(pSettings, r_string, m_light_section, "spot_texture", nullptr));
    }

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
    }

    NeedRail = READ_IF_EXISTS(pSettings, r_bool, section, "need_laser_torch_rail", false);
}

void CWeapon::LoadFireParams(LPCSTR section, LPCSTR prefix)
{
    camDispersion = pSettings->r_float(section, "cam_dispersion");
    camDispersion = deg2rad(camDispersion);

    if (pSettings->line_exist(section, "cam_dispersion_inc"))
    {
        camDispersionInc = pSettings->r_float(section, "cam_dispersion_inc");
        camDispersionInc = deg2rad(camDispersionInc);
    }
    else
        camDispersionInc = 0;

    CShootingObject::LoadFireParams(section, prefix);
}

BOOL CWeapon::net_Spawn(CSE_Abstract* DC)
{
    BOOL bResult = inherited::net_Spawn(DC);

    auto E = smart_cast<CSE_ALifeItemWeapon*>(DC);

    // iAmmoCurrent					= E->a_current;
    iAmmoElapsed = E->a_elapsed;
    m_flagsAddOnState = E->m_addon_flags.get();
    m_ammoType = E->ammo_type;
    SetState(E->wpn_state);
    SetNextState(E->wpn_state);

    if (m_ammoType >= m_ammoTypes.size())
    {
        Msg("! [%s]: %s: wrong m_ammoType[%u/%u]", __FUNCTION__, cName().c_str(), m_ammoType, m_ammoTypes.size() - 1);
        m_ammoType = 0;
        auto se_obj = alife_object();
        if (se_obj)
        {
            auto W = smart_cast<CSE_ALifeItemWeapon*>(se_obj);
            W->ammo_type = m_ammoType;
        }
    }

    m_DefaultCartridge.Load(*m_ammoTypes[m_ammoType], u8(m_ammoType));
    if (iAmmoElapsed)
    {
        // нож автоматически заряжается двумя патронами, хотя
        // размер магазина у него 0. Что бы зря не ругаться, проверим
        // что в конфиге размер магазина не нулевой.
        if (iMagazineSize && iAmmoElapsed > (iMagazineSize + 1))
        {
            Msg("! [%s]: %s: wrong iAmmoElapsed[%u/%u]", __FUNCTION__, cName().c_str(), iAmmoElapsed, iMagazineSize);
            iAmmoElapsed = iMagazineSize;
            auto se_obj = alife_object();
            if (se_obj)
            {
                auto W = smart_cast<CSE_ALifeItemWeapon*>(se_obj);
                W->a_elapsed = iAmmoElapsed;
            }
        }
        m_fCurrentCartirdgeDisp = m_DefaultCartridge.m_kDisp;
        for (int i = 0; i < iAmmoElapsed; ++i)
            m_magazine.push_back(m_DefaultCartridge);
    }

    UpdateAddonsVisibility();
    InitAddons();

    VERIFY((u32)iAmmoElapsed == m_magazine.size());

    return bResult;
}

void CWeapon::net_Destroy()
{
    inherited::net_Destroy();

    //удалить объекты партиклов
    StopFlameParticles();
    StopFlameParticles2();
    StopLight();
    Light_Destroy();

    m_magazine.clear();
    m_magazine.shrink_to_fit();
}

BOOL CWeapon::IsUpdating()
{
    bool bIsActiveItem = m_pCurrentInventory && m_pCurrentInventory->ActiveItem() == this;
    return bIsActiveItem || bWorking || IsPending() || getVisible();
}

void CWeapon::net_Export(CSE_Abstract* E)
{
    inherited::net_Export(E);

    CSE_ALifeInventoryItem* itm = smart_cast<CSE_ALifeInventoryItem*>(E);
    itm->m_fCondition = m_fCondition;

    CSE_ALifeItemWeapon* wpn = smart_cast<CSE_ALifeItemWeapon*>(E);
    wpn->wpn_flags = IsUpdating() ? 1 : 0;
    wpn->a_elapsed = u16(iAmmoElapsed);
    wpn->m_addon_flags.flags = m_flagsAddOnState;
    wpn->ammo_type = (u8)m_ammoType;
    wpn->wpn_state = (u8)GetState();
    wpn->m_bZoom = (u8)m_bZoomMode;
}

void CWeapon::save(NET_Packet& output_packet)
{
    inherited::save(output_packet);
    save_data(iAmmoElapsed, output_packet);
    save_data(m_flagsAddOnState, output_packet);
    save_data(m_ammoType, output_packet);
    save_data(m_bZoomMode, output_packet);
}

void CWeapon::load(IReader& input_packet)
{
    inherited::load(input_packet);
    load_data(iAmmoElapsed, input_packet);
    load_data(m_flagsAddOnState, input_packet);
    UpdateAddonsVisibility();
    load_data(m_ammoType, input_packet);
    load_data(m_bZoomMode, input_packet);

    if (m_bZoomMode)
        OnZoomIn();
    else
        OnZoomOut();
}

void CWeapon::OnEvent(NET_Packet& P, u16 type)
{
    switch (type)
    {
    case GE_WPN_STATE_CHANGE: {
        u8 state;
        P.r_u8(state);
        P.r_u8(m_sub_state);
        //			u8 NewAmmoType =
        P.r_u8();
        u8 AmmoElapsed = P.r_u8();
        u8 NextAmmo = P.r_u8();
        if (NextAmmo == u8(-1))
            m_set_next_ammoType_on_reload = u32(-1);
        else
            m_set_next_ammoType_on_reload = u8(NextAmmo);

        if (OnClient())
            SetAmmoElapsed(int(AmmoElapsed));
        OnStateSwitch(u32(state), GetState());
    }
    break;
    default: {
        inherited::OnEvent(P, type);
    }
    break;
    }
};

void CWeapon::shedule_Update(u32 dT)
{
    // Inherited
    inherited::shedule_Update(dT);
}

void CWeapon::OnH_B_Independent(bool just_before_destroy)
{
    RemoveShotEffector();

    inherited::OnH_B_Independent(just_before_destroy);

    //завершить принудительно все процессы что шли
    FireEnd();
    SetPending(FALSE);
    SwitchState(eIdle);

    m_strapped_mode = false;
    OnZoomOut();
    m_fZoomRotationFactor = 0.f;
    UpdateXForm();
}

void CWeapon::OnH_A_Independent()
{
    inherited::OnH_A_Independent();
    Light_Destroy();
};

void CWeapon::OnH_A_Chield()
{
    inherited::OnH_A_Chield();

    UpdateAddonsVisibility();
};

void CWeapon::OnActiveItem()
{
    inherited::OnActiveItem();
    //если мы занружаемся и оружие было в руках
    SetState(eIdle);
    SetNextState(eIdle);
}

void CWeapon::OnHiddenItem()
{
    inherited::OnHiddenItem();
    SetState(eHidden);
    SetNextState(eHidden);
    m_set_next_ammoType_on_reload = u32(-1);
}

void CWeapon::OnH_B_Chield()
{
    inherited::OnH_B_Chield();

    OnZoomOut();
    m_set_next_ammoType_on_reload = u32(-1);
}

static float state_time = 0; // таймер нахождения оружия в текущем состоянии
static float state_time_heat = 0; // таймер нагрева оружия
static float previous_heating = 0; // "нагретость" оружия в предыдущем состоянии

#include "WeaponBinoculars.h"

void CWeapon::UpdateWeaponParams()
{
#pragma todo("KRodin: адаптировать тепловизор и тп. под новый рендер, если это возможно.")

    if (!IsHidden())
    {
        w_states.x = m_fZoomRotationFactor; // x = zoom mode, y - текущее состояние, z - старое состояние
        if (psActorFlags.test(AF_DOF_SCOPE) && !(IsZoomed() && !IsRotatingToZoom() && (IsScopeAttached() || m_bForceScopeDOF) && !IsGrenadeMode() && m_bUseScopeDOF))
            w_states.x = 0.f;
        if (w_states.y != GetState()) // первый апдейт или стейт изменился
        {
            w_states.z = w_states.y; // записываем старое состояние
            state_time_heat = state_time = Device.fTimeGlobal; // инитим счетчики времени
            previous_heating = w_timers.z; // сохраняем "нагретость" оружия
            w_timers.y = w_timers.x; // записываем время нахождения в предыдущем состоянии
            w_states.y = (float)GetState(); // обновляем состояние
        }
        // флаг бинокля в руках (в этом режиме не нужно размытие)
        if (smart_cast<CWeaponBinoculars*>(this))
            w_states.w = 0;
        else
            w_states.w = 1;
        if (w_states.y == static_cast<float>(eFire) || w_states.y == static_cast<float>(eFire2)) //стреляем, значит оружие греется
        {
            w_timers.z = Device.fTimeGlobal - state_time_heat + previous_heating;
        }
        else // не стреляем - оружие охлаждается
        {
            if (w_timers.z > EPS) // оружие все еще нагрето
            {
                float tm = state_time_heat + previous_heating - Device.fTimeGlobal;
                w_timers.z = (tm < EPS) ? 0.f : tm;
            }
        }
        w_timers.x = Device.fTimeGlobal - state_time; // обновляем таймер текущего состояния
    }
}

u8 CWeapon::idle_state()
{
    auto* actor = smart_cast<CActor*>(H_Parent());

    if (actor)
    {
        u32 st = actor->get_state();
        if (st & mcSprint)
            return eSubstateIdleSprint;
        else if (st & mcAnyAction && !(st & mcJump) && !(st & mcFall))
            return eSubstateIdleMoving;
    }

    return eIdle;
}

void CWeapon::UpdateCL()
{
    inherited::UpdateCL();

    UpdateHUDAddonsVisibility();

    //подсветка от выстрела
    UpdateLight();

    if (ParentIsActor())
        UpdateWeaponParams(); // параметры для рендера оружия в режиме тепловидения

    //нарисовать партиклы
    UpdateFlameParticles();
    UpdateFlameParticles2();

    VERIFY(smart_cast<IKinematics*>(Visual()));

    if (GetState() == eIdle)
    {
        auto state = idle_state();
        if (m_idle_state != state)
        {
            m_idle_state = state;
            if (GetNextState() != eMagEmpty && GetNextState() != eReload)
            {
                SwitchState(eIdle);
            }
        }
    }
    else
        m_idle_state = eIdle;

    UpdateTacthandler();
    UpdateLaser();
    UpdateFlashlight();
}

void CWeapon::UpdateTacthandler()
{
    if (TactHandlerAttachable())
    {
        if (IsTactHandlerAttached() && !t_handler)
        {
            hud_sect = pSettings->r_string(GetWorldSect(), "tact_handler_hud");

            m_fPDM_disp_base += tact_handler_disp_base;
            m_fPDM_disp_vel_factor += tact_handler_vel_factor;
            m_fPDM_disp_accel_factor += tact_handler_accel_factor;

            camRelaxSpeed = deg2rad(rad2deg(camRelaxSpeed) - tact_handler_camRelaxSpeed);
            camDispersion = deg2rad(rad2deg(camDispersion) - tact_handler_camDispersion);
            camDispersionInc = deg2rad(rad2deg(camDispersionInc) - tact_handler_camDispersionInc);

            camMaxAngle = deg2rad(rad2deg(camMaxAngle) - tact_handler_camMaxAngle);
            camMaxAngleHorz = deg2rad(rad2deg(camMaxAngleHorz) - tact_handler_camMaxAngleHorz);
            camStepAngleHorz = deg2rad(rad2deg(camStepAngleHorz) - tact_handler_camStepAngleHorz);

            t_handler = true;
        }
        else
        {
            if (t_handler && !IsTactHandlerAttached())
            {
                hud_sect = pSettings->r_string(GetWorldSect(), "hud");

                m_fPDM_disp_base -= tact_handler_disp_base;
                m_fPDM_disp_vel_factor -= tact_handler_vel_factor;
                m_fPDM_disp_accel_factor -= tact_handler_accel_factor;

                camRelaxSpeed = deg2rad(rad2deg(camRelaxSpeed) + tact_handler_camRelaxSpeed);
                camDispersion = deg2rad(rad2deg(camDispersion) + tact_handler_camDispersion);
                camDispersionInc = deg2rad(rad2deg(camDispersionInc) + tact_handler_camDispersionInc);

                camMaxAngle = deg2rad(rad2deg(camMaxAngle) + tact_handler_camMaxAngle);
                camMaxAngleHorz = deg2rad(rad2deg(camMaxAngleHorz) + tact_handler_camMaxAngleHorz);
                camStepAngleHorz = deg2rad(rad2deg(camStepAngleHorz) + tact_handler_camStepAngleHorz);

                t_handler = false;
            }
        }
    }


}

void CWeapon::UpdateLaser()
{
    if (laser_light_render && !IsLaserAttached())
        if (laser_light_render->get_active())
        {
            SwitchLaser(false);
            laser_light_render->set_active(false);
        }

    if (laser_light_render)
    {
        auto io = smart_cast<CInventoryOwner*>(H_Parent());
        if (!laser_light_render->get_active() && IsLaserOn() && (!H_Parent() || (io && this == io->inventory().ActiveItem())))
        {
            laser_light_render->set_active(true);
            UpdateAddonsVisibility();
        }
        else if (laser_light_render->get_active() && (!IsLaserOn() || !(!H_Parent() || (io && this == io->inventory().ActiveItem()))))
        {
            laser_light_render->set_active(false);
            UpdateAddonsVisibility();
        }

        if (laser_light_render->get_active())
        {
            laser_pos = get_LastFP();
            Fvector laser_dir = get_LastFD();

            if (GetHUDmode())
            {
                if (laserdot_attach_bone.size())
                {
                    GetBoneOffsetPosDir(laserdot_attach_bone, laser_pos, laser_dir, laserdot_attach_offset);
                    CorrectDirFromWorldToHud(laser_dir);
                }
            }
            else
            {
                XFORM().transform_tiny(laser_pos, laserdot_world_attach_offset);
            }

            Fmatrix laserXForm;
            laserXForm.identity();
            laserXForm.k.set(laser_dir);
            Fvector::generate_orthonormal_basis_normalized(laserXForm.k, laserXForm.j, laserXForm.i);

            laser_light_render->set_position(laser_pos);
            laser_light_render->set_rotation(laserXForm.k, laserXForm.i);

            // calc color animator
            if (laser_lanim)
            {
                int frame;
                const u32 clr = laser_lanim->CalculateBGR(Device.fTimeGlobal, frame);

                Fcolor fclr{(float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f};
                fclr.mul_rgb(laser_fBrightness / 255.f);
                laser_light_render->set_color(fclr);
            }
        }
    }
}

void CWeapon::UpdateFlashlight()
{

    if (flashlight_render && !IsTorchAttached())
        if (flashlight_render->get_active())
        {
            SwitchFlashlight(false);
            flashlight_render->set_active(false);
            flashlight_omni->set_active(false);
            flashlight_glow->set_active(false);
            UpdateAddonsVisibility();
        }

    if (flashlight_render)
    {
        auto io = smart_cast<CInventoryOwner*>(H_Parent());
        if (!flashlight_render->get_active() && IsFlashlightOn() && (!H_Parent() || (io && this == io->inventory().ActiveItem())))
        {
            flashlight_render->set_active(true);
            flashlight_omni->set_active(true);
            flashlight_glow->set_active(true);
            UpdateAddonsVisibility();
        }
        else if (flashlight_render->get_active() && (!IsFlashlightOn() || !(!H_Parent() || (io && this == io->inventory().ActiveItem()))))
        {
            flashlight_render->set_active(false);
            flashlight_omni->set_active(false);
            flashlight_glow->set_active(false);
            UpdateAddonsVisibility();
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
            else
            {
                flashlight_dir = get_LastFD();
                XFORM().transform_tiny(flashlight_pos, flashlight_world_attach_offset);

                flashlight_dir_omni = get_LastFD();
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
}

void CWeapon::renderable_Render()
{
    UpdateXForm();

    //нарисовать подсветку
    RenderLight();

    //если мы в режиме снайперки, то сам HUD рисовать не надо
    if (IsZoomed() && !IsRotatingToZoom() && ZoomTexture())
        RenderHud(FALSE);
    else
        RenderHud(TRUE);

    inherited::renderable_Render();
}

bool CWeapon::need_renderable() { return !Device.m_SecondViewport.IsSVPFrame() && !(IsZoomed() && ZoomTexture() && !IsRotatingToZoom()); }

void CWeapon::signal_HideComplete()
{
    if (H_Parent())
        setVisible(FALSE);
    SetPending(FALSE);
}

void CWeapon::SetDefaults()
{
    bWorking2 = false;
    SetPending(FALSE);

    m_flags.set(FUsingCondition, TRUE);
    m_flagsAddOnState = 0;
    m_bZoomMode = false;
}

void CWeapon::UpdatePosition(const Fmatrix& trans)
{
    Position().set(trans.c);
    XFORM().mul(trans, m_strapped_mode ? m_StrapOffset : m_Offset);
    VERIFY(!fis_zero(DET(renderable.xform)));
}

bool CWeapon::Action(s32 cmd, u32 flags)
{
    if (inherited::Action(cmd, flags))
        return true;

    switch (cmd)
    {
    case kWPN_FIRE: {
        //если оружие чем-то занято, то ничего не делать
        {
            /* auto* pAct = smart_cast<CActor*>(H_Parent());
            bool issafemode = pAct->GetSafemode();
            if (issafemode)
            {
                pAct->SetSafemode(false);
                return false;
            }*/
            if (flags & CMD_START)
            {
                if (IsPending())
                    return false;
                FireStart();
            }
            else
                FireEnd();
        };
    }
        return true;
    case kWPN_NEXT: {
        if (IsPending() || OnClient())
        {
            return false;
        }

        if (Core.Features.test(xrCore::Feature::lock_reload_in_sprint) && ParentIsActor() && g_actor->get_state() & mcSprint)
            return true;

        if (flags & CMD_START)
        {
            u32 l_newType = m_ammoType;
            bool b1, b2;
            do
            {
                l_newType = (l_newType + 1) % m_ammoTypes.size();
                b1 = l_newType != m_ammoType;
                b2 = unlimited_ammo() ? false : (!m_pCurrentInventory->GetAmmo(*m_ammoTypes[l_newType], ParentIsActor()));
            } while (b1 && b2);

            if (l_newType != m_ammoType)
            {
                m_set_next_ammoType_on_reload = l_newType;
                if (OnServer())
                    Reload();
            }
        }
    }
        return true;

    case kWPN_ZOOM: {
        if (IsZoomEnabled() && !is_second_scope)
        {
            if (flags & CMD_START && !IsPending())
            {
                if (psActorFlags.is(AF_WPN_AIM_TOGGLE) && IsZoomed())
                {
                    OnZoomOut();
                }
                else
                    OnZoomIn();
            }
            else if (IsZoomed() && !psActorFlags.is(AF_WPN_AIM_TOGGLE))
            {
                OnZoomOut();
            }
            return true;
        }
        else
            return false;
    }

    case kSECONDSCOPE: {
        if (m_second_scope_enable)
        {
            if (is_second_scope)
            {
                OnZoomOut();
                is_second_scope = false;
            }
            else
            {
                OnZoomIn();
                is_second_scope = true;
            }
        }

        return true;
    }

    case kWPN_ZOOM_INC:
    case kWPN_ZOOM_DEC: {
        if (IsZoomEnabled() && IsZoomed() && m_bScopeDynamicZoom && IsScopeAttached() && (flags & CMD_START))
        {
            // если в режиме ПГ - не будем давать использовать динамический зум
            if (IsGrenadeMode())
                return false;

            ZoomChange(cmd == kWPN_ZOOM_INC);

            return true;
        }
        else
            return false;
    }
    }
    return false;
}

void CWeapon::GetZoomData(const float scope_factor, float& delta, float& min_zoom_factor)
{
    float def_fov = Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system) ? 1.f : g_fov;
    float delta_factor_total = def_fov - scope_factor;
    VERIFY(delta_factor_total > 0);
    min_zoom_factor = def_fov - delta_factor_total * m_fMinZoomK;
    delta = (delta_factor_total * (1 - m_fMinZoomK)) / m_fZoomStepCount;
}

void CWeapon::ZoomChange(bool inc)
{
    bool wasChanged = false;

    if (SecondVPEnabled())
    {
        float delta, min_zoom_factor;
        GetZoomData(m_fSecondVPZoomFactor, delta, min_zoom_factor);

        const float currentZoomFactor = m_fRTZoomFactor;

        if (Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system))
        {
            m_fRTZoomFactor += delta * (inc ? 1 : -1);
            clamp(m_fRTZoomFactor, min_zoom_factor, m_fSecondVPZoomFactor);
        }
        else
        {
            m_fRTZoomFactor += delta * (inc ? 1 : -1);
            clamp(m_fRTZoomFactor, m_fSecondVPZoomFactor, min_zoom_factor);
        }

        wasChanged = !fsimilar(currentZoomFactor, m_fRTZoomFactor);
    }
    else
    {
        if (!is_second_scope)
        {
            float delta, min_zoom_factor;
            GetZoomData(m_fScopeZoomFactor, delta, min_zoom_factor);

            const float currentZoomFactor = m_fZoomFactor;

            if (Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system))
            {
                m_fZoomFactor += delta * (inc ? 1 : -1);
                clamp(m_fZoomFactor, min_zoom_factor, m_fScopeZoomFactor);
            }
            else
            {
                m_fZoomFactor -= delta * (inc ? 1 : -1);
                clamp(m_fZoomFactor, m_fScopeZoomFactor, min_zoom_factor);
            }

            wasChanged = !fsimilar(currentZoomFactor, m_fZoomFactor);

            if (H_Parent() && !IsRotatingToZoom() && !SecondVPEnabled())
                m_fRTZoomFactor = m_fZoomFactor; // store current
        }
    }

    if (wasChanged)
    {
        OnZoomChanged();
    }
}

void CWeapon::SpawnAmmo(u32 boxCurr, LPCSTR ammoSect, u32 ParentID)
{
    if (!m_ammoTypes.size())
        return;
    if (OnClient())
        return;

    if (!ammoSect)
        ammoSect = m_ammoTypes.front().c_str();

    CSE_Abstract* D = F_entity_Create(ammoSect);

    // KRodin: GOVNOKOD DETECTED!
    if (D->m_tClassID == CLSID_OBJECT_AMMO || D->m_tClassID == CLSID_OBJECT_A_M209 || D->m_tClassID == CLSID_OBJECT_A_VOG25 || D->m_tClassID == CLSID_OBJECT_A_OG7B)
    {
        CSE_ALifeItemAmmo* l_pA = smart_cast<CSE_ALifeItemAmmo*>(D);
        R_ASSERT(l_pA);
        l_pA->m_boxSize = (u16)pSettings->r_s32(ammoSect, "box_size");
        D->s_name = ammoSect;
        D->set_name_replace("");
        D->s_gameid = u8(GameID());
        D->s_RP = 0xff;
        D->ID = 0xffff;
        if (ParentID == 0xffffffff)
            D->ID_Parent = (u16)H_Parent()->ID();
        else
            D->ID_Parent = (u16)ParentID;

        D->ID_Phantom = 0xffff;
        D->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
        D->RespawnTime = 0;
        l_pA->m_tNodeID = ai_location().level_vertex_id();

        if (boxCurr == 0xffffffff)
            boxCurr = l_pA->m_boxSize;

        while (boxCurr)
        {
            l_pA->a_elapsed = (u16)(boxCurr > l_pA->m_boxSize ? l_pA->m_boxSize : boxCurr);
            NET_Packet P;
            D->Spawn_Write(P, TRUE);
            Level().Send(P, net_flags(TRUE));

            if (boxCurr > l_pA->m_boxSize)
                boxCurr -= l_pA->m_boxSize;
            else
                boxCurr = 0;
        }
    };
    F_entity_Destroy(D);
}

int CWeapon::GetAmmoCurrent(bool use_item_to_spawn) const
{
    int l_count = iAmmoElapsed;
    if (!m_pCurrentInventory)
        return l_count;

    //чтоб не делать лишних пересчетов
    if (m_pCurrentInventory->ModifyFrame() <= m_dwAmmoCurrentCalcFrame)
        return l_count + iAmmoCurrent;

    m_dwAmmoCurrentCalcFrame = Device.dwFrame;
    iAmmoCurrent = 0;

    for (int i = 0; i < (int)m_ammoTypes.size(); ++i)
    {
        iAmmoCurrent += GetAmmoCount_forType(m_ammoTypes[i]);

        if (!use_item_to_spawn)
            continue;

        if (!inventory_owner().item_to_spawn())
            continue;

        iAmmoCurrent += inventory_owner().ammo_in_box_to_spawn();
    }
    return l_count + iAmmoCurrent;
}

int CWeapon::GetAmmoCount(u8 ammo_type, u32 max) const
{
    VERIFY(m_pInventory);
    R_ASSERT(ammo_type < m_ammoTypes.size());

    return GetAmmoCount_forType(m_ammoTypes[ammo_type], max);
}

int CWeapon::GetAmmoCount_forType(shared_str const& ammo_type, u32 max) const
{
    u32 res = 0;
    auto callback = [&](const auto pIItem) -> bool {
        auto* ammo = smart_cast<CWeaponAmmo*>(pIItem);
        if (ammo->cNameSect() == ammo_type)
            res += ammo->m_boxCurr;
        return (max > 0 && res >= max);
    };

    m_pCurrentInventory->IterateAmmo(false, callback);
    if (max == 0 || res < max)
        if (!smart_cast<const CActor*>(H_Parent()) || !psActorFlags.test(AF_AMMO_ON_BELT))
            m_pCurrentInventory->IterateAmmo(true, callback);

    return res;
}

float CWeapon::GetConditionMisfireProbability() const
{
    if (GetCondition() > 0.95f)
        return 0.0f;

    float mis = misfireProbability + powf(1.f - GetCondition(), 3.f) * misfireConditionK;
    clamp(mis, 0.0f, 0.99f);
    return mis;
}

BOOL CWeapon::CheckForMisfire()
{
    if (OnClient())
        return FALSE;

    if (!smart_cast<CActor*>(H_Parent())) // KRodin: НПС не нужны осечки.
        return FALSE;

    float rnd = ::Random.randF(0.f, 1.f);
    float mp = GetConditionMisfireProbability();
    if (rnd < mp)
    {
        FireEnd();

        SwitchMisfire(true);

        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

void CWeapon::Reload() { OnZoomOut(); }

void CWeapon::DeviceSwitch() { OnZoomOut(); }

bool CWeapon::IsGrenadeLauncherAttached() const
{
    return (CSE_ALifeItemWeapon::eAddonAttachable == m_eGrenadeLauncherStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher)) ||
        CSE_ALifeItemWeapon::eAddonPermanent == m_eGrenadeLauncherStatus;
}

bool CWeapon::IsScopeAttached() const
{
    return (CSE_ALifeItemWeapon::eAddonAttachable == m_eScopeStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope)) ||
        CSE_ALifeItemWeapon::eAddonPermanent == m_eScopeStatus;
}

bool CWeapon::IsSilencerAttached() const
{
    return (CSE_ALifeItemWeapon::eAddonAttachable == m_eSilencerStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer)) ||
        CSE_ALifeItemWeapon::eAddonPermanent == m_eSilencerStatus;
}

bool CWeapon::IsBayonetAttached() const
{
    return (CSE_ALifeItemWeapon::eAddonAttachable == m_eBayonetStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonBayonet)) ||
        CSE_ALifeItemWeapon::eAddonPermanent == m_eBayonetStatus;
}
bool CWeapon::IsRailAttached() const
{
    return (CSE_ALifeItemWeapon::eAddonAttachable == m_eRailStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonRail)) ||
        CSE_ALifeItemWeapon::eAddonPermanent == m_eRailStatus;
}
bool CWeapon::IsLaserAttached() const
{
    return (CSE_ALifeItemWeapon::eAddonAttachable == m_eLaserStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonLaser)) ||
        CSE_ALifeItemWeapon::eAddonPermanent == m_eLaserStatus;
}
bool CWeapon::IsTorchAttached() const
{
    return (CSE_ALifeItemWeapon::eAddonAttachable == m_eTorchStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonTorch)) ||
        CSE_ALifeItemWeapon::eAddonPermanent == m_eTorchStatus;
}
bool CWeapon::IsTactHandlerAttached() const
{
    return (CSE_ALifeItemWeapon::eAddonAttachable == m_eTactHandlerStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonTactHandler)) ||
        CSE_ALifeItemWeapon::eAddonPermanent == m_eTactHandlerStatus;
}

bool CWeapon::GrenadeLauncherAttachable() const { return (CSE_ALifeItemWeapon::eAddonAttachable == m_eGrenadeLauncherStatus); }
bool CWeapon::ScopeAttachable() const { return (CSE_ALifeItemWeapon::eAddonAttachable == m_eScopeStatus); }
bool CWeapon::SilencerAttachable() const { return (CSE_ALifeItemWeapon::eAddonAttachable == m_eSilencerStatus); }
bool CWeapon::BayonetAttachable() const { return CSE_ALifeItemWeapon::eAddonAttachable == m_eBayonetStatus; }
bool CWeapon::RailAttachable() const { return CSE_ALifeItemWeapon::eAddonAttachable == m_eRailStatus; }
bool CWeapon::LaserAttachable() const { return CSE_ALifeItemWeapon::eAddonAttachable == m_eLaserStatus; }
bool CWeapon::TorchAttachable() const { return CSE_ALifeItemWeapon::eAddonAttachable == m_eTorchStatus; }
bool CWeapon::TactHandlerAttachable() const { return CSE_ALifeItemWeapon::eAddonAttachable == m_eTactHandlerStatus; }

void CWeapon::UpdateHUDAddonsVisibility()
{  

    if (!GetHUDmode())
        return;

    if (ScopeAttachable())
        HudItemData()->set_bone_visible(m_sHud_wpn_scope_bones, IsScopeAttached());

    if (m_eScopeStatus == ALife::eAddonDisabled)
        HudItemData()->set_bone_visible(m_sHud_wpn_scope_bones, FALSE, TRUE);
    else if (m_eScopeStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(m_sHud_wpn_scope_bones, TRUE, TRUE);

    if (BayonetAttachable())
        HudItemData()->set_bone_visible(m_sHud_wpn_bayonet_bone, IsBayonetAttached());

    if (m_eBayonetStatus == ALife::eAddonDisabled)
        HudItemData()->set_bone_visible(m_sHud_wpn_bayonet_bone, FALSE, TRUE);
    else if (m_eBayonetStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(m_sHud_wpn_bayonet_bone, TRUE, TRUE);

    if (RailAttachable())
        HudItemData()->set_bone_visible(m_sHud_wpn_rail_bone, IsRailAttached());

    if (m_eRailStatus == ALife::eAddonDisabled)
        HudItemData()->set_bone_visible(m_sHud_wpn_rail_bone, FALSE, TRUE);
    else if (m_eRailStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(m_sHud_wpn_rail_bone, TRUE, TRUE);


    if (LaserAttachable())
        HudItemData()->set_bone_visible(m_sHud_wpn_llaser_bone, IsLaserAttached());

    if (m_eLaserStatus == ALife::eAddonDisabled)
        HudItemData()->set_bone_visible(m_sHud_wpn_llaser_bone, FALSE, TRUE);
    else if (m_eLaserStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(m_sHud_wpn_llaser_bone, TRUE, TRUE);

    if (TorchAttachable())
        HudItemData()->set_bone_visible(m_sHud_wpn_torch_bone, IsTorchAttached());

    if (m_eTorchStatus == ALife::eAddonDisabled)
        HudItemData()->set_bone_visible(m_sHud_wpn_torch_bone, FALSE, TRUE);
    else if (m_eTorchStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(m_sHud_wpn_torch_bone, TRUE, TRUE);


   if (TactHandlerAttachable())
        HudItemData()->set_bone_visible(m_sHud_wpn_tacthandler_bone, IsTactHandlerAttached());

    if (m_eTactHandlerStatus == ALife::eAddonDisabled)
       HudItemData()->set_bone_visible(m_sHud_wpn_tacthandler_bone, FALSE, TRUE);
    else if (m_eTactHandlerStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(m_sHud_wpn_tacthandler_bone, TRUE, TRUE);


    if (SilencerAttachable())
        HudItemData()->set_bone_visible(m_sHud_wpn_silencer_bone, IsSilencerAttached());

    if (m_eSilencerStatus == ALife::eAddonDisabled)
        HudItemData()->set_bone_visible(m_sHud_wpn_silencer_bone, FALSE, TRUE);
    else if (m_eSilencerStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(m_sHud_wpn_silencer_bone, TRUE, TRUE);

    if (!HudItemData()->has_bone(m_sHud_wpn_launcher_bone) && HudItemData()->has_bone(wpn_launcher_def_bone_cop))
        m_sHud_wpn_launcher_bone = wpn_launcher_def_bone_cop;

    if (GrenadeLauncherAttachable())
        HudItemData()->set_bone_visible(m_sHud_wpn_launcher_bone, IsGrenadeLauncherAttached());

    if (m_eGrenadeLauncherStatus == ALife::eAddonDisabled)
        HudItemData()->set_bone_visible(m_sHud_wpn_launcher_bone, FALSE, TRUE);
    else if (m_eGrenadeLauncherStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(m_sHud_wpn_launcher_bone, TRUE, TRUE);

    if (m_sHud_wpn_laser_bone.size() && has_laser)
        HudItemData()->set_bone_visible(m_sHud_wpn_laser_bone, IsLaserOn(), TRUE);

    if (m_sHud_wpn_flashlight_bone.size() && has_flashlight)
        HudItemData()->set_bone_visible(m_sHud_wpn_flashlight_bone, IsFlashlightOn(), TRUE);

    for (const shared_str& bone_name : hud_hidden_bones)
        HudItemData()->set_bone_visible(bone_name, FALSE, TRUE);

    callback(GameObject::eOnUpdateHUDAddonsVisibiility)();
}

void CWeapon::UpdateAddonsVisibility()
{
    auto pWeaponVisual = smart_cast<IKinematics*>(Visual());
    VERIFY(pWeaponVisual);

    UpdateHUDAddonsVisibility();

    ///////////////////////////////////////////////////////////////////
    u16 bone_id{};

    for (const auto& sbone : m_sWpn_scope_bones)
    {
        bone_id = pWeaponVisual->LL_BoneID(sbone);

        if (ScopeAttachable())
        {
            if (IsScopeAttached())
            {
                if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                    pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
            }
            else
            {
                if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                    pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
            }
        }

        if (m_eScopeStatus == CSE_ALifeItemWeapon::eAddonDisabled && bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
            pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        else if (m_eScopeStatus == CSE_ALifeItemWeapon::eAddonPermanent && bone_id != BI_NONE && !pWeaponVisual->LL_GetBoneVisible(bone_id))
            pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
    }
    ///////////////////////////////////////////////////////////////////

    bone_id = pWeaponVisual->LL_BoneID(m_sWpn_silencer_bone);

    if (SilencerAttachable())
    {
        if (IsSilencerAttached())
        {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
        else
        {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }

    if (m_eSilencerStatus == CSE_ALifeItemWeapon::eAddonDisabled && bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
    else if (m_eSilencerStatus == CSE_ALifeItemWeapon::eAddonPermanent && bone_id != BI_NONE && !pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);

    ///////////////////////////////////////////////////////////////////

    // Штык-нож
    bone_id = pWeaponVisual->LL_BoneID(m_sWpn_bayonet_bone);
    if (BayonetAttachable())
    {
        if (IsBayonetAttached())
        {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
        else
        {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }
    if (m_eBayonetStatus == CSE_ALifeItemWeapon::eAddonDisabled && bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
    else if (m_eBayonetStatus == CSE_ALifeItemWeapon::eAddonPermanent && bone_id != BI_NONE && !pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);



    bone_id = pWeaponVisual->LL_BoneID(m_sWpn_rail_bone);
    if (RailAttachable())
    {
        if (IsRailAttached())
        {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
        else
        {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }
    if (m_eRailStatus == CSE_ALifeItemWeapon::eAddonDisabled && bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
    else if (m_eRailStatus == CSE_ALifeItemWeapon::eAddonPermanent && bone_id != BI_NONE && !pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);


    bone_id = pWeaponVisual->LL_BoneID(m_sWpn_llaser_bone);
    if (LaserAttachable())
    {
        if (IsLaserAttached())
        {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
        else
        {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }
    if (m_eLaserStatus == CSE_ALifeItemWeapon::eAddonDisabled && bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
    else if (m_eLaserStatus == CSE_ALifeItemWeapon::eAddonPermanent && bone_id != BI_NONE && !pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);


    bone_id = pWeaponVisual->LL_BoneID(m_sWpn_torch_bone);
    if (TorchAttachable())
    {
        if (IsTorchAttached())
        {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
        else
        {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }
    if (m_eTorchStatus == CSE_ALifeItemWeapon::eAddonDisabled && bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
    else if (m_eTorchStatus == CSE_ALifeItemWeapon::eAddonPermanent && bone_id != BI_NONE && !pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);



    bone_id = pWeaponVisual->LL_BoneID(m_sWpn_tacthandler_bone);
    if (TactHandlerAttachable())
    {
        if (IsTactHandlerAttached())
        {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
        else
        {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }
    if (m_eTactHandlerStatus == CSE_ALifeItemWeapon::eAddonDisabled && bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
    else if (m_eTactHandlerStatus == CSE_ALifeItemWeapon::eAddonPermanent && bone_id != BI_NONE && !pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);

    ///////////////////////////////////////////////////////////////////

    bone_id = pWeaponVisual->LL_BoneID(m_sWpn_launcher_bone);

    if (GrenadeLauncherAttachable())
    {
        if (IsGrenadeLauncherAttached())
        {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
        else
        {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }

    if (m_eGrenadeLauncherStatus == CSE_ALifeItemWeapon::eAddonDisabled && bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
    else if (m_eGrenadeLauncherStatus == CSE_ALifeItemWeapon::eAddonPermanent && bone_id != BI_NONE && !pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);

    ///////////////////////////////////////////////////////////////////

    if (m_sWpn_laser_bone.size() && has_laser)
    {
        bone_id = pWeaponVisual->LL_BoneID(m_sWpn_laser_bone);

        if (bone_id != BI_NONE)
        {
            const bool laser_on = IsLaserOn();
            if (pWeaponVisual->LL_GetBoneVisible(bone_id) && !laser_on)
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
            else if (!pWeaponVisual->LL_GetBoneVisible(bone_id) && laser_on)
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
    }

    ///////////////////////////////////////////////////////////////////

    if (m_sWpn_flashlight_bone.size() && has_flashlight)
    {
        bone_id = pWeaponVisual->LL_BoneID(m_sWpn_flashlight_bone);

        if (bone_id != BI_NONE)
        {
            const bool flashlight_on = IsFlashlightOn();
            if (pWeaponVisual->LL_GetBoneVisible(bone_id) && !flashlight_on)
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
            else if (!pWeaponVisual->LL_GetBoneVisible(bone_id) && flashlight_on)
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
    }

    ///////////////////////////////////////////////////////////////////

    for (const auto& bone_name : hidden_bones)
    {
        bone_id = pWeaponVisual->LL_BoneID(bone_name);
        if (bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
            pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
    }

    ///////////////////////////////////////////////////////////////////

    callback(GameObject::eOnUpdateAddonsVisibiility)();

    pWeaponVisual->CalculateBones_Invalidate();
    pWeaponVisual->CalculateBones();
}

bool CWeapon::Activate(bool now)
{
    UpdateAddonsVisibility();
    return inherited::Activate(now);
}

void CWeapon::InitAddons() {}

float CWeapon::CurrentZoomFactor()
{
    if (SecondVPEnabled())
        return Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system) ? 1.0f : m_fIronSightZoomFactor; // no change to main fov zoom when use second vp
    else if (IsScopeAttached() && !is_second_scope)
        return m_fScopeZoomFactor;
    else
        return m_fIronSightZoomFactor;
}

void CWeapon::OnZoomIn()
{
    m_bZoomMode = true;

    // если в режиме ПГ - не будем давать включать динамический зум
    if (m_bScopeDynamicZoom && !IsGrenadeMode() && !SecondVPEnabled())
        m_fZoomFactor = m_fRTZoomFactor;
    else if (is_second_scope)
        m_fZoomFactor = m_fRTZoomFactor;
    else
        m_fZoomFactor = CurrentZoomFactor();

    if (IsScopeAttached() && !IsGrenadeMode())
    {
        if (!m_bScopeZoomInertionAllow)
            AllowHudInertion(FALSE);
    }
    else if (!m_bZoomInertionAllow)
        AllowHudInertion(FALSE);

    if (GetHUDmode())
        GamePersistent().SetPickableEffectorDOF(true);

    CActor* pActor = smart_cast<CActor*>(H_Parent());
    if (pActor)
        pActor->callback(GameObject::eOnActorWeaponZoomIn)(lua_game_object());
}

void CWeapon::OnZoomOut()
{
    m_fZoomFactor = Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system) ? 1.f : g_fov;

    if (m_bZoomMode)
    {
        SprintType = false;
        m_bZoomMode = false;

        CActor* pActor = smart_cast<CActor*>(H_Parent());
        if (pActor)
        {
            w_states.set(0.f, 0.f, 0.f, 1.f);
            pActor->callback(GameObject::eOnActorWeaponZoomOut)(lua_game_object());
        }
    }

    AllowHudInertion(TRUE);

    if (GetHUDmode())
        GamePersistent().SetPickableEffectorDOF(false);

    ResetSubStateTime();

}

bool CWeapon::UseScopeTexture()
{
    return !SecondVPEnabled() && m_UIScope; // только если есть текстура прицела - для простого создания коллиматоров
}

CUIStaticItem* CWeapon::ZoomTexture()
{
    if (UseScopeTexture() && !is_second_scope)
        return m_UIScope;
    else
        return NULL;
}

void CWeapon::SwitchState(u32 S)
{
    if (OnClient())
        return;

    SetNextState(S); // Very-very important line of code!!! :)
    if (CHudItem::object().Local() && !CHudItem::object().getDestroy() /* && (S!=NEXT_STATE)*/
        && m_pCurrentInventory && OnServer())
    {
        // !!! Just single entry for given state !!!
        NET_Packet P;
        CHudItem::object().u_EventGen(P, GE_WPN_STATE_CHANGE, CHudItem::object().ID());
        P.w_u8(u8(S));
        P.w_u8(u8(m_sub_state));
        P.w_u8(u8(m_ammoType & 0xff));
        P.w_u8(u8(iAmmoElapsed & 0xff));
        P.w_u8(u8(m_set_next_ammoType_on_reload & 0xff));
        CHudItem::object().u_EventSend(P, net_flags(TRUE, TRUE, FALSE, TRUE));
    }
}

void CWeapon::OnMagazineEmpty() { VERIFY((u32)iAmmoElapsed == m_magazine.size()); }

void CWeapon::reinit()
{
    CShootingObject::reinit();
    CHudItemObject::reinit();
}

void CWeapon::reload(LPCSTR section)
{
    CShootingObject::reload(section);
    CHudItemObject::reload(section);

    m_can_be_strapped = true;
    m_strapped_mode = false;

    if (pSettings->line_exist(section, "strap_bone0"))
        m_strap_bone0 = pSettings->r_string(section, "strap_bone0");
    else
        m_can_be_strapped = false;

    if (pSettings->line_exist(section, "strap_bone1"))
        m_strap_bone1 = pSettings->r_string(section, "strap_bone1");
    else
        m_can_be_strapped = false;

    if (m_eScopeStatus == ALife::eAddonAttachable)
    {
        m_addon_holder_range_modifier = READ_IF_EXISTS(pSettings, r_float, m_sScopeName, "holder_range_modifier", m_holder_range_modifier);
        m_addon_holder_fov_modifier = READ_IF_EXISTS(pSettings, r_float, m_sScopeName, "holder_fov_modifier", m_holder_fov_modifier);
    }
    else
    {
        m_addon_holder_range_modifier = m_holder_range_modifier;
        m_addon_holder_fov_modifier = m_holder_fov_modifier;
    }

    {
        Fvector pos, ypr;
        pos = pSettings->r_fvector3(section, "position");
        ypr = pSettings->r_fvector3(section, "orientation");
        ypr.mul(PI / 180.f);

        m_Offset.setHPB(ypr.x, ypr.y, ypr.z);
        m_Offset.translate_over(pos);
    }

    m_StrapOffset = m_Offset;
    if (pSettings->line_exist(section, "strap_position") && pSettings->line_exist(section, "strap_orientation"))
    {
        Fvector pos, ypr;
        pos = pSettings->r_fvector3(section, "strap_position");
        ypr = pSettings->r_fvector3(section, "strap_orientation");
        ypr.mul(PI / 180.f);

        m_StrapOffset.setHPB(ypr.x, ypr.y, ypr.z);
        m_StrapOffset.translate_over(pos);
    }
    else
        m_can_be_strapped = false;

    m_ef_main_weapon_type = READ_IF_EXISTS(pSettings, r_u32, section, "ef_main_weapon_type", u32(-1));
    m_ef_weapon_type = READ_IF_EXISTS(pSettings, r_u32, section, "ef_weapon_type", u32(-1));
}

void CWeapon::create_physic_shell()
{
    // xrKrodin: Временный? "фикс" для оружия из ганслингера, валяющегося на земле. По непонятным причинам (много костей или хз от чего ещё) в некоторых случаях при рассчетах
    // физики происходят краши в ode которые исправить невозможно.
    if (IS_OGSR_GA)
    {
        m_pPhysicsShell = P_build_SimpleShell(this, 0.3f, false);
        m_pPhysicsShell->SetMaterial(smart_cast<IKinematics*>(Visual())->LL_GetData(smart_cast<IKinematics*>(Visual())->LL_GetBoneRoot()).game_mtl_idx);
    }
    else
        CPhysicsShellHolder::create_physic_shell();
}

void CWeapon::activate_physic_shell() { CPhysicsShellHolder::activate_physic_shell(); }

void CWeapon::setup_physic_shell() { CPhysicsShellHolder::setup_physic_shell(); }

bool CWeapon::can_kill() const
{
    if (GetAmmoCurrent(true) || m_ammoTypes.empty())
        return (true);

    return (false);
}

CInventoryItem* CWeapon::can_kill(CInventory* inventory) const
{
    if (GetAmmoElapsed() || m_ammoTypes.empty())
        return (const_cast<CWeapon*>(this));

    TIItemContainer::iterator I = inventory->m_all.begin();
    TIItemContainer::iterator E = inventory->m_all.end();
    for (; I != E; ++I)
    {
        CInventoryItem* inventory_item = smart_cast<CInventoryItem*>(*I);
        if (!inventory_item)
            continue;

        xr_vector<shared_str>::const_iterator i = std::find(m_ammoTypes.begin(), m_ammoTypes.end(), inventory_item->object().cNameSect());
        if (i != m_ammoTypes.end())
            return (inventory_item);
    }

    return (0);
}

const CInventoryItem* CWeapon::can_kill(const xr_vector<const CGameObject*>& items) const
{
    if (m_ammoTypes.empty())
        return (this);

    xr_vector<const CGameObject*>::const_iterator I = items.begin();
    xr_vector<const CGameObject*>::const_iterator E = items.end();
    for (; I != E; ++I)
    {
        const CInventoryItem* inventory_item = smart_cast<const CInventoryItem*>(*I);
        if (!inventory_item)
            continue;

        xr_vector<shared_str>::const_iterator i = std::find(m_ammoTypes.begin(), m_ammoTypes.end(), inventory_item->object().cNameSect());
        if (i != m_ammoTypes.end())
            return (inventory_item);
    }

    return (0);
}

bool CWeapon::ready_to_kill() const { return (!IsMisfire() && ((GetState() == eIdle) || (GetState() == eFire) || (GetState() == eFire2)) && GetAmmoElapsed()); }

// Получить индекс текущих координат худа
u8 CWeapon::GetCurrentHudOffsetIdx() const
{
    const bool b_aiming = ((IsZoomed() && m_fZoomRotationFactor <= 1.f) || (!IsZoomed() && m_fZoomRotationFactor > 0.f));
    if (is_second_scope)
    {
        return hud_item_measures::m_hands_offset_type_aim2;
    }
    if (b_aiming)
    {
        const bool has_gl = GrenadeLauncherAttachable() && IsGrenadeLauncherAttached();
        const bool has_scope = ScopeAttachable() && IsScopeAttached();

        if (IsGrenadeMode())
        {
            if (m_bUseScopeGrenadeZoom && has_scope)
                return hud_item_measures::m_hands_offset_type_gl_scope;
            else
                return hud_item_measures::m_hands_offset_type_gl;
        }
        else if (has_gl)
        {
            if (m_bUseScopeZoom && has_scope)
                return hud_item_measures::m_hands_offset_type_gl_normal_scope;
            else
                return hud_item_measures::m_hands_offset_type_aim_gl_normal;
        }
        else
        {
            if (m_bUseScopeZoom && has_scope)
                return hud_item_measures::m_hands_offset_type_aim_scope;
            else
                return hud_item_measures::m_hands_offset_type_aim;
        }
    }

    return hud_item_measures::m_hands_offset_type_normal;
}

void CWeapon::SetAmmoElapsed(int ammo_count)
{
    iAmmoElapsed = ammo_count;

    u32 uAmmo = u32(iAmmoElapsed);

    if (uAmmo != m_magazine.size())
    {
        if (uAmmo > m_magazine.size())
        {
            CCartridge l_cartridge;
            l_cartridge.Load(*m_ammoTypes[m_ammoType], u8(m_ammoType));
            while (uAmmo > m_magazine.size())
                m_magazine.push_back(l_cartridge);
        }
        else
        {
            while (uAmmo < m_magazine.size())
                m_magazine.pop_back();
        };
    };
}

u32 CWeapon::ef_main_weapon_type() const
{
    VERIFY(m_ef_main_weapon_type != u32(-1));
    return (m_ef_main_weapon_type);
}

u32 CWeapon::ef_weapon_type() const
{
    VERIFY(m_ef_weapon_type != u32(-1));
    return (m_ef_weapon_type);
}

bool CWeapon::IsNecessaryItem(const shared_str& item_sect) { return (std::find(m_ammoTypes.begin(), m_ammoTypes.end(), item_sect) != m_ammoTypes.end()); }

void CWeapon::modify_holder_params(float& range, float& fov) const
{
    if (!IsScopeAttached())
    {
        inherited::modify_holder_params(range, fov);
        return;
    }
    range *= m_addon_holder_range_modifier;
    fov *= m_addon_holder_fov_modifier;
}

void CWeapon::OnDrawUI()
{
    if (IsZoomed() && ZoomHideCrosshair())
    {
        if (ZoomTexture() && !IsRotatingToZoom())
        {
            ZoomTexture()->SetPos(0, 0);
            ZoomTexture()->SetRect(0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);
            ZoomTexture()->Render();

            //			m_UILens.Draw();
        }
    }
}

bool CWeapon::IsHudModeNow() { return (HudItemData() != nullptr); }

bool CWeapon::unlimited_ammo()
{
    if (m_pCurrentInventory)
        return inventory_owner().unlimited_ammo() && m_DefaultCartridge.m_flags.test(CCartridge::cfCanBeUnlimited);
    else
        return false;
};

LPCSTR CWeapon::GetCurrentAmmo_ShortName()
{
    if (m_magazine.empty())
        return ("");
    CCartridge& l_cartridge = m_magazine.back();
    return *(l_cartridge.m_InvShortName);
}

float CWeapon::GetMagazineWeight(const decltype(CWeapon::m_magazine)& mag) const
{
    float res = 0;
    const char* last_type = nullptr;
    float last_ammo_weight = 0;
    for (auto& c : mag)
    {
        // Usually ammos in mag have same type, use this fact to improve performance
        if (last_type != c.m_ammoSect.c_str())
        {
            last_type = c.m_ammoSect.c_str();
            last_ammo_weight = c.Weight();
        }
        res += last_ammo_weight;
    }
    return res;
}

float CWeapon::Weight() const
{
    float res = CInventoryItemObject::Weight();
    if (GrenadeLauncherAttachable() && IsGrenadeLauncherAttached())
        res += pSettings->r_float(GetGrenadeLauncherName(), "inv_weight");
    if (ScopeAttachable() && IsScopeAttached())
        res += pSettings->r_float(GetScopeName(), "inv_weight");
    if (SilencerAttachable() && IsSilencerAttached())
        res += pSettings->r_float(GetSilencerName(), "inv_weight");
    res += GetMagazineWeight(m_magazine);

    return res;
}

u32 CWeapon::Cost() const
{
    u32 res = m_cost;

    if (Core.Features.test(xrCore::Feature::wpn_cost_include_addons))
    {
        if (GrenadeLauncherAttachable() && IsGrenadeLauncherAttached())
            res += pSettings->r_u32(GetGrenadeLauncherName(), "cost");
        if (ScopeAttachable() && IsScopeAttached())
            res += pSettings->r_u32(GetScopeName(), "cost");
        if (SilencerAttachable() && IsSilencerAttached())
            res += pSettings->r_u32(GetSilencerName(), "cost");
    }
    return res;
}

void CWeapon::Hide(bool now)
{
    if (now)
    {
        OnStateSwitch(eHidden, GetState());
        SetState(eHidden);
        StopHUDSounds();
    }
    else
        SwitchState(eHiding);

    OnZoomOut();
}

void CWeapon::Show(bool now)
{
    if (now)
    {
        StopCurrentAnimWithoutCallback();
        OnStateSwitch(eIdle, GetState());
        SetState(eIdle);
        StopHUDSounds();
    }
    else
        SwitchState(eShowing);
}

bool CWeapon::show_crosshair() { return psActorFlags.test(AF_CROSSHAIR_DBG) || !(IsZoomed() && ZoomHideCrosshair()); }

bool CWeapon::show_indicators() { return !(IsZoomed() && (ZoomTexture() || !m_bScopeShowIndicators)); }

float CWeapon::GetConditionToShow() const
{
    return (GetCondition()); // powf(GetCondition(),4.0f));
}

BOOL CWeapon::ParentMayHaveAimBullet()
{
    CObject* O = H_Parent();
    if (!O)
        return FALSE;
    CEntityAlive* EA = smart_cast<CEntityAlive*>(O);
    return EA->cast_actor() != 0;
}

BOOL CWeapon::ParentIsActor()
{
    CObject* O = H_Parent();
    if (!O)
        return FALSE;
    CEntityAlive* EA = smart_cast<CEntityAlive*>(O);
    if (!EA)
        return FALSE;
    return EA->cast_actor() != 0;
}

const float& CWeapon::hit_probability() const
{
    VERIFY((g_SingleGameDifficulty >= egdNovice) && (g_SingleGameDifficulty <= egdMaster));
    return (m_hit_probability[egdNovice]);
}

// Обновление необходимости включения второго вьюпорта +SecondVP+
// Вызывается только для активного оружия игрока
void CWeapon::UpdateSecondVP()
{
    // + CActor::UpdateCL();
    CActor* pActor = smart_cast<CActor*>(H_Parent());
    if (!pActor)
        return;

    CInventoryOwner* inv_owner = pActor->cast_inventory_owner();

    bool b_is_active_item = inv_owner && (inv_owner->m_inventory->ActiveItem() == this);
    R_ASSERT(b_is_active_item); // Эта функция должна вызываться только для оружия в руках нашего игрока

    bool bCond_1 = m_fZoomRotationFactor > 0.05f; // Мы должны целиться
    bool bCond_3 = pActor->cam_Active() == pActor->cam_FirstEye(); // Мы должны быть от 1-го лица

    Device.m_SecondViewport.SetSVPActive(bCond_1 && bCond_3 && SecondVPEnabled());
}

bool CWeapon::SecondVPEnabled() const
{
    bool bCond_2 = m_fSecondVPZoomFactor > 0.0f; // В конфиге должен быть прописан фактор зума (scope_lense_fov_factor) больше чем 0
    bool bCond_4 = !IsGrenadeMode(); // Мы не должны быть в режиме подствольника
    bool bcond_6 = psActorFlags.test(AF_3D_SCOPES);
    bool bcond_8 = !is_second_scope;

    return bCond_2 && bCond_4 && bcond_6 && bcond_8;
}

// Чувствительность мышкии с оружием в руках во время прицеливания
float CWeapon::GetControlInertionFactor() const
{
    float fInertionFactor = inherited::GetControlInertionFactor();

    if (IsZoomed() && SecondVPEnabled() && !IsRotatingToZoom())
    {
        if (m_bScopeDynamicZoom)
        {
            const float delta_factor_total = 1 - m_fSecondVPZoomFactor;
            float min_zoom_factor = 1 + delta_factor_total * m_fMinZoomK;
            float k = (m_fRTZoomFactor - min_zoom_factor) / (m_fSecondVPZoomFactor - min_zoom_factor);
            return (m_fScopeInertionFactor - fInertionFactor) * k + fInertionFactor;
        }
        else
            return m_fScopeInertionFactor;
    }

    return fInertionFactor;
}

float CWeapon::GetSecondVPFov() const
{
    float fov_factor = m_fSecondVPZoomFactor;
    if (m_bScopeDynamicZoom)
    {
        fov_factor = m_fRTZoomFactor;
    }
    return atanf(tanf(g_fov * (0.5f * PI / 180)) / fov_factor) / (0.5f * PI / 180);
}

float CWeapon::GetHudFov()
{
    const float last_nw_hf = inherited::GetHudFov();

    if (m_fZoomRotationFactor > 0.0f)
    {
        if (SecondVPEnabled() && m_fSecondVPHudFov > 0.0f)
        {
            // В линзе зума
            const float fDiff = last_nw_hf - m_fSecondVPHudFov;
            return m_fSecondVPHudFov + (fDiff * (1 - m_fZoomRotationFactor));
        }
        if ((m_eScopeStatus == CSE_ALifeItemWeapon::eAddonDisabled || IsScopeAttached()) && !IsGrenadeMode() && m_fZoomHudFov > 0.0f)
        {
            // В процессе зума
            const float fDiff = last_nw_hf - m_fZoomHudFov;
            return m_fZoomHudFov + (fDiff * (1 - m_fZoomRotationFactor));
        }
    }

    return last_nw_hf;
}

void CWeapon::OnBulletHit()
{
    if (!fis_zero(conditionDecreasePerShotOnHit))
        ChangeCondition(-conditionDecreasePerShotOnHit);
}

void CWeapon::SaveAttachableParams()
{
    const char* sect_name = cNameSect().c_str();
    string_path buff;
    FS.update_path(buff, "$logs$", make_string("_world\\%s.ltx", sect_name).c_str());

    CInifile pHudCfg(buff, FALSE, FALSE, TRUE);

    sprintf_s(buff, "%f,%f,%f", m_Offset.c.x, m_Offset.c.y, m_Offset.c.z);
    pHudCfg.w_string(sect_name, "position", buff);

    Fvector ypr;
    m_Offset.getHPB(ypr.x, ypr.y, ypr.z);
    ypr.mul(180.f / PI);
    sprintf_s(buff, "%f,%f,%f", ypr.x, ypr.y, ypr.z);
    pHudCfg.w_string(sect_name, "orientation", buff);

    if (pSettings->line_exist(sect_name, "strap_position") && pSettings->line_exist(sect_name, "strap_orientation"))
    {
        sprintf_s(buff, "%f,%f,%f", m_StrapOffset.c.x, m_StrapOffset.c.y, m_StrapOffset.c.z);
        pHudCfg.w_string(sect_name, "strap_position", buff);
        m_StrapOffset.getHPB(ypr.x, ypr.y, ypr.z);
        ypr.mul(180.f / PI);
        sprintf_s(buff, "%f,%f,%f", ypr.x, ypr.y, ypr.z);
        pHudCfg.w_string(sect_name, "strap_orientation", buff);
    }

    Msg("--[%s] data saved to [%s]", __FUNCTION__, pHudCfg.fname());
}

void CWeapon::ParseCurrentItem(CGameFont* F) { F->OutNext("WEAPON IN STRAPPED MODE: [%d]", m_strapped_mode); }
