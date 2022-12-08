#include "stdafx.h"
#include "hudmanager.h"
#include "WeaponMagazined.h"
#include "entity.h"
#include "actor.h"
#include "torch.h"
#include "ParticlesObject.h"
#include "Attaches.h"
#include "inventory.h"
#include "xrserver_objects_alife_items.h"
#include "ActorEffector.h"
#include "EffectorZoomInertion.h"
#include "xr_level_controller.h"
#include "level.h"
#include "object_broker.h"
#include "string_table.h"
#include "WeaponBinoculars.h"
#include "WeaponBinocularsVision.h"
#include "ai_object_location.h"

#include "game_object_space.h"
#include "script_callback_ex.h"
#include "script_game_object.h"
#include <regex>
#include "../xr_3da/x_ray.h"

#include "level_bullet_manager.h"
#include "../xr_3da/gamemtllib.h"
#define KNIFE_MATERIAL_NAME "objects\\knife"

#include "UIGameSP.h"
#include "HudSound.h"
#include "ui/UIXmlInit.h"

CUIXml* g_wpnScopeXml = NULL;

CWeaponMagazined::CWeaponMagazined(LPCSTR name, ESoundTypes eSoundType) : CWeapon(name)
{
    m_eSoundShow = ESoundTypes(SOUND_TYPE_ITEM_TAKING | eSoundType);
    m_eSoundHide = ESoundTypes(SOUND_TYPE_ITEM_HIDING | eSoundType);
    m_eSoundShot = ESoundTypes(SOUND_TYPE_WEAPON_SHOOTING | eSoundType);
    m_eSoundEmptyClick = ESoundTypes(SOUND_TYPE_WEAPON_EMPTY_CLICKING | eSoundType);
    m_eSoundReload = ESoundTypes(SOUND_TYPE_WEAPON_RECHARGING | eSoundType);

    m_sSndShotCurrent = nullptr;
    m_pSndShotCurrent = NULL;
    m_sSilencerFlameParticles = m_sSilencerSmokeParticles = NULL;

    m_bFireSingleShot = false;
    m_iShotNum = 0;
    m_iQueueSize = WEAPON_ININITE_QUEUE;
    m_bLockType = false;

    m_binoc_vision = nullptr;
    m_bVision = false;
}

CWeaponMagazined::~CWeaponMagazined()
{
    // sounds
    HUD_SOUND::DestroySound(sndShow);
    HUD_SOUND::DestroySound(sndHide);
    HUD_SOUND::DestroySound(sndShot);
    HUD_SOUND::DestroySound(sndSilencerShot);
    HUD_SOUND::DestroySound(sndEmptyClick);
    HUD_SOUND::DestroySound(sndReload);
    HUD_SOUND::DestroySound(sndReloadPartly);
    HUD_SOUND::DestroySound(sndReloadJammed);
    HUD_SOUND::DestroySound(sndReloadJammedLast);
    HUD_SOUND::DestroySound(sndFireModes);
    HUD_SOUND::DestroySound(sndZoomChange);
    HUD_SOUND::DestroySound(sndTactItemOn);
    HUD_SOUND::DestroySound(sndItemOn);
    HUD_SOUND::DestroySound(sndAimStart);
    HUD_SOUND::DestroySound(sndAimEnd);
    HUD_SOUND::DestroySound(sndAnmKick);
    if (m_binoc_vision)
        xr_delete(m_binoc_vision);
}

void CWeaponMagazined::StopHUDSounds()
{
    HUD_SOUND::StopSound(sndShow);
    HUD_SOUND::StopSound(sndHide);

    HUD_SOUND::StopSound(sndEmptyClick);
    HUD_SOUND::StopSound(sndReload);
    HUD_SOUND::StopSound(sndReloadPartly);
    HUD_SOUND::StopSound(sndReloadJammed);
    HUD_SOUND::StopSound(sndReloadJammedLast);
    HUD_SOUND::StopSound(sndFireModes);
    HUD_SOUND::StopSound(sndZoomChange);
    HUD_SOUND::StopSound(sndTactItemOn);
    HUD_SOUND::StopSound(sndItemOn);
    HUD_SOUND::StopSound(sndAimStart);
    HUD_SOUND::StopSound(sndAimEnd);

    HUD_SOUND::StopSound(sndShot);
    HUD_SOUND::StopSound(sndSilencerShot);
    HUD_SOUND::StopSound(sndAnmKick);

    inherited::StopHUDSounds();
}

void CWeaponMagazined::net_Destroy()
{
    inherited::net_Destroy();
    if (m_binoc_vision)
        xr_delete(m_binoc_vision);
}

BOOL CWeaponMagazined::net_Spawn(CSE_Abstract* DC)
{
    BOOL bRes = inherited::net_Spawn(DC);
    const auto wpn = smart_cast<CSE_ALifeItemWeaponMagazined*>(DC);
    m_iCurFireMode = wpn->m_u8CurFireMode;
    if (HasFireModes() && m_iCurFireMode >= m_aFireModes.size())
    {
        Msg("! [%s]: %s: wrong m_iCurFireMode[%u/%u]", __FUNCTION__, cName().c_str(), m_iCurFireMode, m_aFireModes.size() - 1);
        m_iCurFireMode = m_aFireModes.size() - 1;
        auto se_obj = alife_object();
        if (se_obj)
        {
            auto W = smart_cast<CSE_ALifeItemWeaponMagazined*>(se_obj);
            W->m_u8CurFireMode = m_iCurFireMode;
        }
    }
    SetQueueSize(GetCurrentFireMode());
    return bRes;
}

void CWeaponMagazined::Load(LPCSTR section)
{
    inherited::Load(section);

    // Sounds
    HUD_SOUND::LoadSound(section, "snd_draw", sndShow, m_eSoundShow);
    HUD_SOUND::LoadSound(section, "snd_holster", sndHide, m_eSoundHide);
    //HUD_SOUND::LoadSound(section, "snd_shoot", sndShot, m_eSoundShot);
    HUD_SOUND::LoadSound(section, "snd_empty", sndEmptyClick, m_eSoundEmptyClick);

    	// Alundaio: LAYERED_SND_SHOOT
    m_layered_sounds.LoadSound(section, "snd_shoot", "sndShot", false, m_eSoundShot);

    if (pSettings->line_exist(section, "snd_shoot_actor"))
        m_layered_sounds.LoadSound(section, "snd_shoot_actor", "sndShotActor", false, m_eSoundShot);
    //-Alundaio

    // misfire shot
    /* if (pSettings->line_exist(section, "snd_shot_misfire"))
        m_layered_sounds.LoadSound(section, "snd_shot_misfire", "sndShotMisfire", false, m_eSoundShot);
    if (pSettings->line_exist(section, "snd_shot_misfire_actor"))
        m_layered_sounds.LoadSound(section, "snd_shot_misfire_actor", "sndShotMisfireActor", false, m_eSoundShot);*/
	

    if (pSettings->line_exist(section, "snd_reload_empty"))
        HUD_SOUND::LoadSound(section, "snd_reload_empty", sndReload, m_eSoundReload);
    else
        HUD_SOUND::LoadSound(section, "snd_reload", sndReload, m_eSoundReload);

    if (pSettings->line_exist(section, "snd_reload_jammed"))
        HUD_SOUND::LoadSound(section, "snd_reload_jammed", sndReloadJammed, m_eSoundReload);

    if (pSettings->line_exist(section, "snd_reload_jammed_last"))
        HUD_SOUND::LoadSound(section, "snd_reload_jammed_last", sndReloadJammedLast, m_eSoundReload);

    if (pSettings->line_exist(section, "snd_reload_empty")) // OpenXRay-style неполная перезарядка
        HUD_SOUND::LoadSound(section, "snd_reload", sndReloadPartly, m_eSoundReload);
    else if (pSettings->line_exist(section, "snd_reload_partly")) // OGSR-style неполная перезарядка
        HUD_SOUND::LoadSound(section, "snd_reload_partly", sndReloadPartly, m_eSoundReload);

    if (pSettings->line_exist(section, "snd_fire_modes"))
        HUD_SOUND::LoadSound(section, "snd_fire_modes", sndFireModes, m_eSoundEmptyClick);
    if (pSettings->line_exist(section, "snd_zoom_change"))
        HUD_SOUND::LoadSound(section, "snd_zoom_change", sndZoomChange, m_eSoundEmptyClick);
    if (pSettings->line_exist(section, "snd_tact_item_on"))
        HUD_SOUND::LoadSound(section, "snd_tact_item_on", sndTactItemOn, m_eSoundEmptyClick);
    if (pSettings->line_exist(section, "snd_item_on"))
        HUD_SOUND::LoadSound(section, "snd_item_on", sndItemOn, m_eSoundEmptyClick);

    if (pSettings->line_exist(section, "snd_aim_start"))
        HUD_SOUND::LoadSound(section, "snd_aim_start", sndAimStart, m_eSoundShow);
    if (pSettings->line_exist(section, "snd_aim_end"))
        HUD_SOUND::LoadSound(section, "snd_aim_end", sndAimEnd, m_eSoundHide);


    m_pSndShotCurrent = &sndShot;
    m_sSndShotCurrent = "sndShot";

    //звуки и партиклы глушителя, еслит такой есть
    if (m_eSilencerStatus == ALife::eAddonAttachable)
    {
        if (pSettings->line_exist(section, "silencer_flame_particles"))
            m_sSilencerFlameParticles = pSettings->r_string(section, "silencer_flame_particles");
        if (pSettings->line_exist(section, "silencer_smoke_particles"))
            m_sSilencerSmokeParticles = pSettings->r_string(section, "silencer_smoke_particles");
        // Alundaio: LAYERED_SND_SHOOT Silencer
        m_layered_sounds.LoadSound(section, "snd_silncer_shot", "sndSilencerShot", false, m_eSoundShot);
        if (pSettings->line_exist(section, "snd_silncer_shot_actor"))
            m_layered_sounds.LoadSound(section, "snd_silncer_shot_actor", "sndSilencerShotActor", false, m_eSoundShot);
        //-Alundaio
        //HUD_SOUND::LoadSound(section, "snd_silncer_shot", sndSilencerShot, m_eSoundShot);
    }
    //  [7/20/2005]
    if (pSettings->line_exist(section, "dispersion_start"))
        m_iShootEffectorStart = pSettings->r_u8(section, "dispersion_start");
    else
        m_iShootEffectorStart = 0;
    //  [7/20/2005]
    //  [7/21/2005]
    if (pSettings->line_exist(section, "fire_modes"))
    {
        m_bHasDifferentFireModes = true;
        shared_str FireModesList = pSettings->r_string(section, "fire_modes");
        int ModesCount = _GetItemCount(FireModesList.c_str());
        m_aFireModes.clear();
        for (int i = 0; i < ModesCount; i++)
        {
            string16 sItem;
            _GetItem(FireModesList.c_str(), i, sItem);
            int FireMode = atoi(sItem);
            m_aFireModes.push_back(FireMode);
        }
        m_iCurFireMode = ModesCount - 1;
        m_iPrefferedFireMode = READ_IF_EXISTS(pSettings, r_s16, section, "preffered_fire_mode", -1);
    }
    else
        m_bHasDifferentFireModes = false;

    m_bVision = !!READ_IF_EXISTS(pSettings, r_bool, section, "vision_present", false);
    m_fire_zoomout_time = READ_IF_EXISTS(pSettings, r_u32, section, "fire_zoomout_time", u32(-1));

    m_str_count_tmpl = READ_IF_EXISTS(pSettings, r_string, "features", "wpn_magazined_str_count_tmpl", "{AE}/{AC}");

    CartridgeInTheChamberEnabled = READ_IF_EXISTS(pSettings, r_bool, section, "CartridgeInTheChamberEnabled", false);

    // Штык-Нож
    if (pSettings->line_exist(section, "snd_bayonet_kick"))
        HUD_SOUND::LoadSound(section, "snd_bayonet_kick", sndAnmKick, m_eSoundEmptyClick);

    if (m_eBayonetStatus == ALife::eAddonAttachable)
    {
        fWallmarkSize = pSettings->r_float(section, "bayonet_wm_size");
        fireDistanceBayonet = pSettings->r_float(section, "bayonet_distance");
        cur_fHit = pSettings->r_float(section, "bayonet_hit_power");
        cur_fHitImpulse = pSettings->r_float(section, "bayonet_hit_impulse");
        cur_bayonet_hittype = ALife::g_tfString2HitType(pSettings->r_string(section, "bayonet_hit_type"));
        BayonetHitTime = pSettings->r_float(section, "bayonet_kick_lock_time");
    }

    knife_material_idx = GMLib.GetMaterialIdx(KNIFE_MATERIAL_NAME);
    IsBayonetKick = false;

}

void CWeaponMagazined::FireStart()
{
    if (IsValid() && (!IsMisfire() || IsGrenadeMode()))
    {
        if (!IsWorking() || AllowFireWhileWorking())
        {
            if (GetState() == eReload)
                return;
            if (GetState() == eShowing)
                return;
            if (GetState() == eHiding)
                return;
            if (GetState() == eMisfire)
                return;

            inherited::FireStart();

            if (iAmmoElapsed == 0)
                OnMagazineEmpty();
            else
                SwitchState(eFire);
        }
    }
    else if (IsMisfire() && !IsGrenadeMode())
    {
        if (smart_cast<CActor*>(H_Parent()))
        {
            HUD().GetUI()->AddInfoMessage("gun_jammed");
            Misfire();
        }
    }
    else if (eReload != GetState() && eMisfire != GetState())
        OnMagazineEmpty();
}

void CWeaponMagazined::FireEnd()
{
    inherited::FireEnd();

    if (Core.Features.test(xrCore::Feature::autoreload_wpn))
    {
        auto actor = smart_cast<CActor*>(H_Parent());
        if (!iAmmoElapsed && actor && GetState() != eReload)
            Reload();
    }
}

void CWeaponMagazined::Reload()
{
    inherited::Reload();

    TryReload();
}

bool CWeaponMagazined::TryReload()
{
    if (m_pCurrentInventory)
    {
        bool forActor = ParentIsActor();

        m_pAmmo = smart_cast<CWeaponAmmo*>(m_pCurrentInventory->GetAmmo(*m_ammoTypes[m_ammoType], forActor));

        if ((m_pAmmo || m_set_next_ammoType_on_reload != u32(-1)) || unlimited_ammo() || (IsMisfire() && iAmmoElapsed))
        {
            SetPending(TRUE);
            SwitchState(eReload);
            return true;
        }
        else
            for (u32 i = 0; i < m_ammoTypes.size(); ++i)
            {
                m_pAmmo = smart_cast<CWeaponAmmo*>(m_pCurrentInventory->GetAmmo(*m_ammoTypes[i], forActor));
                if (m_pAmmo)
                {
                    m_set_next_ammoType_on_reload = i; // https://github.com/revolucas/CoC-Xray/pull/5/commits/3c45cad1edb388664efbe3bb20a29f92e2d827ca
                    SetPending(TRUE);
                    SwitchState(eReload);
                    return true;
                }
            }
    }

    SwitchState(eIdle);

    return false;
}

void CWeaponMagazined::OnMagazineEmpty()
{
    //попытка стрелять когда нет патронов
    if (GetState() == eIdle)
    {
        OnEmptyClick();
        return;
    }

    if (GetNextState() != eMagEmpty && GetNextState() != eReload)
    {
        SwitchState(eMagEmpty);
    }

    inherited::OnMagazineEmpty();
}

void CWeaponMagazined::UnloadMagazine(bool spawn_ammo)
{
    xr_map<LPCSTR, u16> l_ammo;

    while (!m_magazine.empty())
    {
        CCartridge& l_cartridge = m_magazine.back();
        xr_map<LPCSTR, u16>::iterator l_it;
        for (l_it = l_ammo.begin(); l_ammo.end() != l_it; ++l_it)
        {
            if (!xr_strcmp(*l_cartridge.m_ammoSect, l_it->first))
            {
                ++(l_it->second);
                break;
            }
        }

        if (l_it == l_ammo.end())
            l_ammo[*l_cartridge.m_ammoSect] = 1;
        m_magazine.pop_back();
        --iAmmoElapsed;
    }

    VERIFY((u32)iAmmoElapsed == m_magazine.size());

    if (!spawn_ammo)
        return;

    bool forActor = ParentIsActor();

    xr_map<LPCSTR, u16>::iterator l_it;
    for (l_it = l_ammo.begin(); l_ammo.end() != l_it; ++l_it)
    {
        if (Core.Features.test(xrCore::Feature::hard_ammo_reload) ? (!forActor && m_pCurrentInventory) : !!m_pCurrentInventory)
        {
            CWeaponAmmo* l_pA = smart_cast<CWeaponAmmo*>(m_pCurrentInventory->GetAmmo(l_it->first, forActor));
            if (l_pA)
            {
                u16 l_free = l_pA->m_boxSize - l_pA->m_boxCurr;
                l_pA->m_boxCurr = l_pA->m_boxCurr + (l_free < l_it->second ? l_free : l_it->second);
                l_it->second = l_it->second - (l_free < l_it->second ? l_free : l_it->second);
            }
        }
        if (l_it->second && !unlimited_ammo())
            SpawnAmmo(l_it->second, l_it->first);
    }
}

void CWeaponMagazined::ReloadMagazine()
{
    m_dwAmmoCurrentCalcFrame = 0;

    //устранить осечку при перезарядке
    if (IsMisfire() && !IsGrenadeMode())
    {
        SwitchMisfire(false);
        if (GetAmmoElapsed() > 0)
            SetAmmoElapsed(GetAmmoElapsed() - 1);
        return;
    }

    //переменная блокирует использование
    //только разных типов патронов
    if (!m_bLockType)
    {
        m_pAmmo = NULL;
    }

    if (!m_pCurrentInventory)
        return;

    if (m_set_next_ammoType_on_reload != u32(-1))
    {
        m_ammoType = m_set_next_ammoType_on_reload;
        m_set_next_ammoType_on_reload = u32(-1);
    }

    if (!unlimited_ammo())
    {
        bool forActor = ParentIsActor();

        //попытаться найти в инвентаре патроны текущего типа
        if (Core.Features.test(xrCore::Feature::hard_ammo_reload) && forActor)
            m_pAmmo = smart_cast<CWeaponAmmo*>(m_pCurrentInventory->GetAmmoMaxCurr(*m_ammoTypes[m_ammoType], forActor));
        else
            m_pAmmo = smart_cast<CWeaponAmmo*>(m_pCurrentInventory->GetAmmo(*m_ammoTypes[m_ammoType], forActor));

        if (!m_pAmmo && !m_bLockType)
        {
            for (u32 i = 0; i < m_ammoTypes.size(); ++i)
            {
                //проверить патроны всех подходящих типов
                if (Core.Features.test(xrCore::Feature::hard_ammo_reload) && forActor)
                    m_pAmmo = smart_cast<CWeaponAmmo*>(m_pCurrentInventory->GetAmmoMaxCurr(*m_ammoTypes[i], forActor));
                else
                    m_pAmmo = smart_cast<CWeaponAmmo*>(m_pCurrentInventory->GetAmmo(*m_ammoTypes[i], forActor));

                if (m_pAmmo)
                {
                    m_ammoType = i;
                    break;
                }
            }
        }
    }

    //нет патронов для перезарядки
    if (!m_pAmmo && !unlimited_ammo())
        return;

    //разрядить магазин, если загружаем патронами другого типа
    if (Core.Features.test(xrCore::Feature::hard_ammo_reload))
    {
        if (!m_bLockType && !m_magazine.empty())
            if ((ParentIsActor() && !unlimited_ammo()) || (!m_pAmmo || xr_strcmp(m_pAmmo->cNameSect(), *m_magazine.back().m_ammoSect)))
                UnloadMagazine();
    }
    else
    {
        if (!m_bLockType && !m_magazine.empty() && (!m_pAmmo || xr_strcmp(m_pAmmo->cNameSect(), *m_magazine.back().m_ammoSect)))
            UnloadMagazine();
    }

    VERIFY((u32)iAmmoElapsed == m_magazine.size());

    if (m_DefaultCartridge.m_LocalAmmoType != m_ammoType)
        m_DefaultCartridge.Load(*m_ammoTypes[m_ammoType], u8(m_ammoType));
    CCartridge l_cartridge = m_DefaultCartridge;
    while (iAmmoElapsed < (iMagazineSize + CartridgeInTheChamber))
    {
        if (!unlimited_ammo())
        {
            if (!m_pAmmo->Get(l_cartridge))
                break; //-V595
        }
        ++iAmmoElapsed;
        l_cartridge.m_LocalAmmoType = u8(m_ammoType);
        m_magazine.push_back(l_cartridge);
    }

    VERIFY((u32)iAmmoElapsed == m_magazine.size());

    //выкинуть коробку патронов, если она пустая
    if (m_pAmmo && !m_pAmmo->m_boxCurr && OnServer())
        m_pAmmo->SetDropManual(TRUE);

    if (Core.Features.test(xrCore::Feature::hard_ammo_reload) && ParentIsActor() && m_pAmmo)
    {
        int box_size = m_pAmmo->m_boxSize;
        if (!m_bLockType && iMagazineSize > iAmmoElapsed && iMagazineSize > box_size)
        {
            m_bLockType = true;
            int need_ammo = iMagazineSize - box_size;
            while (need_ammo > 0)
            {
                ReloadMagazine();
                if (need_ammo < box_size)
                    break;
                need_ammo -= box_size;
            }
            m_bLockType = false;
        }
    }
    else if (iMagazineSize > iAmmoElapsed)
    {
        m_bLockType = true;
        ReloadMagazine();
        m_bLockType = false;
    }

    VERIFY((u32)iAmmoElapsed == m_magazine.size());
}

void CWeaponMagazined::Misfire()
{
    inherited::Misfire();

    if (IsZoomed() && !IsRotatingToZoom())
    {
        OnEmptyClick();
    }
    else
    {
        SetPending(TRUE);
        SwitchState(eMisfire);
    }
}

void CWeaponMagazined::DeviceSwitch()
{
    inherited::DeviceSwitch();

    SetPending(TRUE);
    SwitchState(eDeviceSwitch);
}

void CWeaponMagazined::OnStateSwitch(u32 S, u32 oldState)
{
    inherited::OnStateSwitch(S, oldState);
    switch (S)
    {
    case eIdle: switch2_Idle(); break;
    case eFire: switch2_Fire(); break;
    case eFire2: switch2_Fire2(); break;
    case eMisfire: {
        PlayAnimCheckMisfire();
        PlaySound(sndEmptyClick, get_LastFP());
        SetPending(TRUE);
    }
    break;
    case eMagEmpty:
        switch2_Empty();

        if (GetNextState() != eReload)
        {
            SwitchState(eIdle);
        }
        break;
    case eReload: switch2_Reload(); break;
    case eShowing: switch2_Showing(); break;
    case eHiding: switch2_Hiding(); break;
    case eHidden: switch2_Hidden(); break;
    case eDeviceSwitch:
        PlayAnimDeviceSwitch();
        SetPending(TRUE);
        break;
    }
}

void CWeaponMagazined::DeviceUpdate()
{
    if (auto pA = smart_cast<CActor*>(H_Parent()))
    {
        if (LaserSwitch)
        {
            SwitchLaser(!IsLaserOn());
            LaserSwitch = false;
        }
        else if (TorchSwitch)
        {
            SwitchFlashlight(!IsFlashlightOn());
            TorchSwitch = false;
        }
        else if (HeadLampSwitch)
        {
            auto pActorTorch = smart_cast<CTorch*>(pA->inventory().ItemFromSlot(TORCH_SLOT));
            pActorTorch->Switch();
            HeadLampSwitch = false;
        }
        else if (NightVisionSwitch)
        {
            if (auto pActorTorch = smart_cast<CTorch*>(pA->inventory().ItemFromSlot(TORCH_SLOT)))
                pActorTorch->SwitchNightVision();
            NightVisionSwitch = false;
        }
    }
}

void CWeaponMagazined::UpdateCL()
{
    inherited::UpdateCL();
    float dt = Device.fTimeDelta;

    //когда происходит апдейт состояния оружия
    //ничего другого не делать
    if (GetNextState() == GetState())
    {
        switch (GetState())
        {
        case eShowing:
        case eHiding:
        case eReload:
        case eMisfire:
        case eDeviceSwitch:
        case eSprintStart:
        case eSprintEnd:
        case eIdle:
            fTime -= dt;
            if (fTime < 0)
                fTime = 0;
            break;
        case eFire:
            if (iAmmoElapsed > 0)
                state_Fire(dt);

            if (fTime <= 0)
            {
                if (iAmmoElapsed == 0)
                    OnMagazineEmpty();
                StopShooting();
            }
            else
            {
                fTime -= dt;
            }

            if (m_fire_zoomout_time != u32(-1) && IsZoomed() && m_dwStateTime > m_fire_zoomout_time)
                OnZoomOut();

            break;
        case eMagEmpty:
        case eHidden: break;
        }
    }

    if (H_Parent() && IsZoomed() && !IsRotatingToZoom() && m_binoc_vision)
        m_binoc_vision->Update();

    UpdateSounds();
    TimeLockAnimation();
    UpdateBayonetKick();
}

void CWeaponMagazined::UpdateSounds()
{
    if (Device.dwFrame == dwUpdateSounds_Frame)
        return;

    dwUpdateSounds_Frame = Device.dwFrame;

    // ref_sound positions
    if (sndShow.playing())
        sndShow.set_position(get_LastFP());
    if (sndHide.playing())
        sndHide.set_position(get_LastFP());
    if (sndShot.playing())
        sndShot.set_position(get_LastFP());
    if (sndSilencerShot.playing())
        sndSilencerShot.set_position(get_LastFP());
    if (sndReload.playing())
        sndReload.set_position(get_LastFP());
    if (sndReloadPartly.playing())
        sndReloadPartly.set_position(get_LastFP());
    if (sndReloadJammed.playing())
        sndReloadJammed.set_position(get_LastFP());
    if (sndReloadJammedLast.playing())
        sndReloadJammedLast.set_position(get_LastFP());
    if (sndEmptyClick.playing())
        sndEmptyClick.set_position(get_LastFP());
    if (sndFireModes.playing())
        sndFireModes.set_position(get_LastFP());
    if (sndZoomChange.playing())
        sndZoomChange.set_position(get_LastFP());
    if (sndTactItemOn.playing())
        sndTactItemOn.set_position(get_LastFP());
    if (sndItemOn.playing())
        sndItemOn.set_position(get_LastFP());
    if (sndAimStart.playing())
        sndAimStart.set_position(get_LastFP());
    if (sndAimEnd.playing())
        sndAimEnd.set_position(get_LastFP());
}


void CWeaponMagazined::UpdateBayonetKick()
{
    if (IsBayonetKick)
    {
        if (BayonetHitTime > HitTime)
        {
            float dt = Device.fTimeDelta;
            HitTime += dt;
        }
        else
        {
            MakeBayonetHit();
            HitTime = 0;
            IsBayonetKick = false;
        }
    }

}


void CWeaponMagazined::state_Fire(float dt)
{
    VERIFY(fTimeToFire > 0.f);

    Fvector p1, d;
    p1.set(get_LastFP());
    d.set(get_LastFD());

    auto Parent = H_Parent();
    if (!Parent)
        return;

    auto ParentEnt = smart_cast<CEntity*>(Parent);
    if (!ParentEnt)
        return; //Такое иногда бывает. Не понятно почему, но бывает. Например был случай когда пыталось стрелять оружие лежащее в ящике.

#ifdef DEBUG
    CInventoryOwner* io = smart_cast<CInventoryOwner*>(H_Parent());
    if (!io->inventory().ActiveItem())
    {
        Log("current_state", GetState());
        Log("next_state", GetNextState());
        Log("state_time", m_dwStateTime);
        Log("item_sect", cNameSect().c_str());
        Log("H_Parent", H_Parent()->cNameSect().c_str());
    }
#endif

    ParentEnt->g_fireParams(this, p1, d);

    if (m_iShotNum == 0)
    {
        m_vStartPos = p1;
        m_vStartDir = d;
    }

    VERIFY(!m_magazine.empty());
    //	Msg("%d && %d && (%d || %d) && (%d || %d)", !m_magazine.empty(), fTime<=0, IsWorking(), m_bFireSingleShot, m_iQueueSize < 0, m_iShotNum < m_iQueueSize);
    while (!m_magazine.empty() && fTime <= 0 && (IsWorking() || m_bFireSingleShot) && (m_iQueueSize < 0 || m_iShotNum < m_iQueueSize))
    {
        if (CheckForMisfire())
        {
            OnEmptyClick();
            StopShooting();
            return;
        }

        m_bFireSingleShot = false;

        VERIFY(fTimeToFire > 0.f);
        // Alundaio: Use fModeShotTime instead of fOneShotTime if current fire mode is 2-shot burst
        // Alundaio: Cycle down RPM after two shots; used for Abakan/AN-94
        if (GetCurrentFireMode() == 2 || (bCycleDown == true && m_iShotNum < 1))
        {
            fTime += fTimeToFire2;
        }
        else
            fTime += fTimeToFire;
        // Alundaio: END

        ++m_iShotNum;

        OnShot();

        if (m_iShotNum > m_iShootEffectorStart)
            FireTrace(p1, d);
        else
            FireTrace(m_vStartPos, m_vStartDir);
    }

    if (m_iShotNum == m_iQueueSize)
        m_bStopedAfterQueueFired = true;

    UpdateSounds();
}

void CWeaponMagazined::SetDefaults() { CWeapon::SetDefaults(); }

void CWeaponMagazined::PlaySoundShot()
{
    if (ParentIsActor())
    {
        string128 sndName;
        strconcat(sizeof(sndName), sndName, m_sSndShotCurrent.c_str(), "Actor");
        if (m_layered_sounds.FindSoundItem(sndName, false))
        {
            m_layered_sounds.PlaySound(sndName, get_LastFP(), H_Root(), !!GetHUDmode(), false, (u8)-1);
            return;
        }
    }

    m_layered_sounds.PlaySound(m_sSndShotCurrent.c_str(), get_LastFP(), H_Root(), !!GetHUDmode(), false, (u8)-1);
}

void CWeaponMagazined::OnShot()
{
    // Если актор бежит - останавливаем его
    if (ParentIsActor())
        Actor()->set_state_wishful(Actor()->get_state_wishful() & (~mcSprint));

    // Sound
    //PlaySound(*m_pSndShotCurrent, get_LastFP(), true);
    //m_layered_sounds.PlaySound(m_sSndShotCurrent.c_str(), get_LastFP(), H_Root(), !!GetHUDmode(), false, (u8)-1);
    PlaySoundShot();
    // Camera
    AddShotEffector();

    // Animation
    PlayAnimShoot();

    // Shell Drop
    Fvector vel;
    PHGetLinearVell(vel);
    OnShellDrop(get_LastSP(), vel);

    // Огонь из ствола
    StartFlameParticles();

    //дым из ствола
    ForceUpdateFireParticles();
    StartSmokeParticles(get_LastFP(), vel);
}

void CWeaponMagazined::OnEmptyClick()
{
    PlayAnimFakeShoot();
    PlaySound(sndEmptyClick, get_LastFP());
}

void CWeaponMagazined::OnAnimationEnd(u32 state)
{
    switch (state)
    {
    case eReload:
        ReloadMagazine();
        HUD_SOUND::StopSound(sndReload);
        HUD_SOUND::StopSound(sndReloadPartly);
        HUD_SOUND::StopSound(sndReloadJammed);
        HUD_SOUND::StopSound(sndReloadJammedLast);
        SwitchState(eIdle);
        break; // End of reload animation
    case eHiding: SwitchState(eHidden); break; // End of Hide
    case eShowing: SwitchState(eIdle); break; // End of Show
    case eIdle: switch2_Idle(); break; // Keep showing idle
    case eMisfire: SwitchState(eIdle); break; // End of misfire animation
    case eDeviceSwitch: SwitchState(eIdle); break; // End of device switch animation
    default: inherited::OnAnimationEnd(state);
    }
}

void CWeaponMagazined::switch2_Idle()
{
    SetPending(FALSE);
    PlayAnimIdle();
}

#ifdef DEBUG
#include "ai\stalker\ai_stalker.h"
#include "object_handler_planner.h"
#endif
#include <player_hud.h>
void CWeaponMagazined::switch2_Fire()
{
    CInventoryOwner* io = smart_cast<CInventoryOwner*>(H_Parent());
#ifdef DEBUG
    CInventoryItem* ii = smart_cast<CInventoryItem*>(this);
    VERIFY2(io, make_string("no inventory owner, item %s", *cName()));

    if (ii != io->inventory().ActiveItem())
        Msg("! not an active item, item %s, owner %s, active item %s", *cName(), *H_Parent()->cName(),
            io->inventory().ActiveItem() ? *io->inventory().ActiveItem()->object().cName() : "no_active_item");

    if (!(io && (ii == io->inventory().ActiveItem())))
    {
        CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(H_Parent());
        if (stalker)
        {
            stalker->planner().show();
            stalker->planner().show_current_world_state();
            stalker->planner().show_target_world_state();
        }
    }
#else
    if (!io)
        return;
#endif // DEBUG

    //
    //	VERIFY2(
    //		io && (ii == io->inventory().ActiveItem()),
    //		make_string(
    //			"item[%s], parent[%s]",
    //			*cName(),
    //			H_Parent() ? *H_Parent()->cName() : "no_parent"
    //		)
    //	);

    m_bStopedAfterQueueFired = false;
    m_bFireSingleShot = true;
    m_iShotNum = 0;

    if ((OnClient() || Level().IsDemoPlay()) && !IsWorking())
        FireStart();

    /*	if(SingleShotMode())
        {
            m_bFireSingleShot = true;
            bWorking = false;
        }*/
}
void CWeaponMagazined::switch2_Empty()
{
    if (!Core.Features.test(xrCore::Feature::autoreload_wpn) && smart_cast<CActor*>(H_Parent()))
    {
        OnEmptyClick();
        return;
    }

    OnZoomOut();

    if (!TryReload())
    {
        OnEmptyClick();
    }
    else
    {
        inherited::FireEnd();
    }
}
void CWeaponMagazined::PlayReloadSound()
{
    if ((IsMisfire() && iAmmoElapsed == 1) && !sndReloadJammedLast.sounds.empty())
        PlaySound(sndReloadJammedLast, get_LastFP());
    else if (IsMisfire() && !sndReloadJammed.sounds.empty())
        PlaySound(sndReloadJammed, get_LastFP());
    else if (IsPartlyReloading() && !sndReloadPartly.sounds.empty())
        PlaySound(sndReloadPartly, get_LastFP());
    else
        PlaySound(sndReload, get_LastFP());
}

void CWeaponMagazined::switch2_Reload()
{
    CWeapon::FireEnd();

    if (iAmmoElapsed > 0 && CartridgeInTheChamberEnabled)
        CartridgeInTheChamber = 1;
    else
        CartridgeInTheChamber = 0;

    PlayReloadSound();
    PlayAnimReload();
    SetPending(TRUE);
}

void CWeaponMagazined::switch2_Hiding()
{
    CWeapon::FireEnd();

    StopHUDSounds();
    PlaySound(sndHide, get_LastFP());

    PlayAnimHide();
    SetPending(TRUE);
}

void CWeaponMagazined::switch2_Hidden()
{
    CWeapon::FireEnd();

    HUD_SOUND::StopSound(sndReload);
    HUD_SOUND::StopSound(sndReloadPartly);
    HUD_SOUND::StopSound(sndReloadJammed);
    HUD_SOUND::StopSound(sndReloadJammedLast);
    StopCurrentAnimWithoutCallback();

    signal_HideComplete();
    RemoveShotEffector();
}
void CWeaponMagazined::switch2_Showing()
{
    PlaySound(sndShow, get_LastFP());

    SetPending(TRUE);
    PlayAnimShow();
}

bool CWeaponMagazined::Action(s32 cmd, u32 flags)
{
    if (inherited::Action(cmd, flags))
        return true;

    //если оружие чем-то занято, то ничего не делать
    if (IsPending()) //&& cmd != kWPN_FIREMODE_PREV && cmd != kWPN_FIREMODE_NEXT)
        return false;

    switch (cmd)
    {
    case kWPN_RELOAD: {
        if (!Core.Features.test(xrCore::Feature::lock_reload_in_sprint) || (!ParentIsActor() || !(g_actor->get_state() & mcSprint)))
            if (flags & CMD_START && !m_fAltScopeActive && !IsZoomed())
                if (iAmmoElapsed < iMagazineSize || (IsMisfire() && !IsGrenadeMode()))
                    Reload();
    }
        return true;
    case kWPN_FIREMODE_PREV: {
        if (flags & CMD_START)
        {
            if (!m_fAltScopeActive && !IsZoomed())
                OnPrevFireMode();
            return true;
        }
    }
    /* case kSAFEMODE : {
        if (flags & CMD_START)
        {
            if (!m_fAltScopeActive && !IsZoomed())
            {
                CActor* pAct = smart_cast<CActor*>(H_Parent());
                if (pAct->GetSafemode())
                    pAct->SetSafemode(false);
                else
                    pAct->SetSafemode(true);
            }
                return true;
        }
    }
    break;*/
    case kWPN_FIREMODE_NEXT: {
        if (flags & CMD_START)
        {
            if (!m_fAltScopeActive && !IsZoomed())
                OnNextFireMode();
            return true;
        }
    }
    break;
    case kLASER_ON: {
        if ((flags & CMD_START) && has_laser && (GetState() == eIdle) )//&& IsLaserAttached())
        {
            LaserSwitch = true;
            DeviceSwitch();
            return true;
        }
    }
    break;
    case kFLASHLIGHT: {
        if ((flags & CMD_START) && has_flashlight && GetState() == eIdle)
        {
            TorchSwitch = true;
            DeviceSwitch();
            return true;
        }
    }
    break;
    case kTORCH: {
        auto pActorTorch = smart_cast<CActor*>(H_Parent())->inventory().ItemFromSlot(TORCH_SLOT);
        if ((flags & CMD_START) && pActorTorch && GetState() == eIdle)
        {
            HeadLampSwitch = true;
            DeviceSwitch();
            return true;
        }
    }
    break;
    case kNIGHT_VISION: {
        auto pActorNv = smart_cast<CActor*>(H_Parent())->inventory().ItemFromSlot(IS_OGSR_GA ? NIGHT_VISION_SLOT : TORCH_SLOT);
        if ((flags & CMD_START) && pActorNv && GetState() == eIdle)
        {
            NightVisionSwitch = true;
            DeviceSwitch();
            return true;
        }
    }
    break;
    case kBAYONET: {
        if ((flags & CMD_START) && IsBayonetAttached() && (GetState() == eIdle) && !IsBayonetKick)
        {
            CActor* pAct = smart_cast<CActor*>(H_Parent());
            if (pAct->GetSafemode()){
                pAct->SetSafemode(false);
                return false;
            }
            BayonetKick();
            return true;
        }
    }
    break;
    }

    return false;
}

bool CWeaponMagazined::CanAttach(PIItem pIItem)
{
    CScope* pScope = smart_cast<CScope*>(pIItem);
    CSilencer* pSilencer = smart_cast<CSilencer*>(pIItem);
    CGrenadeLauncher* pGrenadeLauncher = smart_cast<CGrenadeLauncher*>(pIItem);
    CBayonet* pBayonet = smart_cast<CBayonet*>(pIItem);
    CRail* pRail = smart_cast<CRail*>(pIItem);
    CLaser* pLaser = smart_cast<CLaser*>(pIItem);
    CTactTorch* pTTorch = smart_cast<CTactTorch*>(pIItem);
    CTactHandler* pTHandler = smart_cast<CTactHandler*>(pIItem);

    if (pScope && m_eScopeStatus == ALife::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope) == 0 &&
        std::find(m_allScopeNames.begin(), m_allScopeNames.end(), pIItem->object().cNameSect()) != m_allScopeNames.end())
        return true;
    else if (pSilencer && m_eSilencerStatus == ALife::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer) == 0 &&
             (m_sSilencerName == pIItem->object().cNameSect()))
        return true;
    else if (pGrenadeLauncher && m_eGrenadeLauncherStatus == ALife::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) == 0 &&
             (m_sGrenadeLauncherName == pIItem->object().cNameSect()) && (!IsBayonetAttached() || !BayonetAttachable()))
        return true;
    else if (pBayonet && m_eBayonetStatus == ALife::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonBayonet) == 0 &&
             (m_sBayonetName == pIItem->object().cNameSect()) && !IsGrenadeLauncherAttached())
        return true;
    else if (pRail && m_eRailStatus == ALife::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonRail) == 0 && (m_sRailName == pIItem->object().cNameSect()))
        return true;
    else if (pLaser && m_eLaserStatus == ALife::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonLaser) == 0 &&
             (m_sLLaserName == pIItem->object().cNameSect()) && GetRail())
        return true;
    else if (pTTorch && m_eTorchStatus == ALife::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonTorch) == 0 &&
             (m_sTorchName == pIItem->object().cNameSect()) && GetRail())
        return true;
    else if (pTHandler && m_eTactHandlerStatus == ALife::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonTactHandler) == 0 &&
             (m_sTactHandlerName == pIItem->object().cNameSect()))
        return true;
    else
        return inherited::CanAttach(pIItem);
}

bool CWeaponMagazined::CanDetach(const char* item_section_name)
{
    if (m_eScopeStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope) && (m_sScopeName == item_section_name))
        return true;
    else if (m_eSilencerStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer) &&
             (m_sSilencerName == item_section_name))
        return true;
    else if (m_eGrenadeLauncherStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) &&
             (m_sGrenadeLauncherName == item_section_name))
        return true;
    else if (m_eBayonetStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonBayonet) &&
             (m_sBayonetName == item_section_name))
        return true;
    else if (m_eRailStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonRail) && (m_sRailName == item_section_name) 
        && !(IsLaserAttached() && IsTorchAttached()))
        return true;
    else if (m_eLaserStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonLaser) && (m_sLLaserName == item_section_name))
        return true;
    else if (m_eTorchStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonTorch) && (m_sTorchName == item_section_name))
        return true;
    else if (m_eTactHandlerStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonTactHandler) &&
             (m_sTactHandlerName == item_section_name))
        return true;
    else
        return inherited::CanDetach(item_section_name);
}

bool CWeaponMagazined::Attach(PIItem pIItem, bool b_send_event)
{
    bool result = false;

    CScope* pScope = smart_cast<CScope*>(pIItem);
    CSilencer* pSilencer = smart_cast<CSilencer*>(pIItem);
    CGrenadeLauncher* pGrenadeLauncher = smart_cast<CGrenadeLauncher*>(pIItem);
    CBayonet* pBayonet = smart_cast<CBayonet*>(pIItem);
    CRail* pRail = smart_cast<CRail*>(pIItem);
    CLaser* pLaser = smart_cast<CLaser*>(pIItem);
    CTactTorch* pTTorch = smart_cast<CTactTorch*>(pIItem);
    CTactHandler* pTHandler = smart_cast<CTactHandler*>(pIItem);

    if (pScope && m_eScopeStatus == CSE_ALifeItemWeapon::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope) == 0 &&
        std::find(m_allScopeNames.begin(), m_allScopeNames.end(), pIItem->object().cNameSect()) != m_allScopeNames.end())
    {
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonScope;
        result = true;
    }
    else if (pSilencer && m_eSilencerStatus == CSE_ALifeItemWeapon::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer) == 0 &&
             (m_sSilencerName == pIItem->object().cNameSect()))
    {
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonSilencer;
        result = true;
    }
    else if (pGrenadeLauncher && m_eGrenadeLauncherStatus == CSE_ALifeItemWeapon::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) == 0 &&
             (m_sGrenadeLauncherName == pIItem->object().cNameSect()))
    {
         m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher;
         result = true;
    }
    else if (pBayonet && m_eBayonetStatus == CSE_ALifeItemWeapon::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonBayonet) == 0 &&
             (m_sBayonetName == pIItem->object().cNameSect()))
    {
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonBayonet;
        result = true;
    }
    else if (pRail && m_eRailStatus == CSE_ALifeItemWeapon::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonRail) == 0 &&
             (m_sRailName == pIItem->object().cNameSect()))
    {
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonRail;
        result = true;
    }
    else if (pLaser && m_eLaserStatus == CSE_ALifeItemWeapon::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonLaser) == 0 &&
             (m_sLLaserName == pIItem->object().cNameSect()))
    {
         m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonLaser;
         result = true;

    }
    else if (pTTorch && m_eTorchStatus == CSE_ALifeItemWeapon::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonTorch) == 0 &&
             (m_sTorchName == pIItem->object().cNameSect()))
    {
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonTorch;
        result = true;
    }
    else if (pTHandler && m_eTactHandlerStatus == CSE_ALifeItemWeapon::eAddonAttachable && (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonTactHandler) == 0 &&
             (m_sTactHandlerName == pIItem->object().cNameSect()))
    {
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonTactHandler;
        result = true;
    }

    if (result)
    {
        if (b_send_event && OnServer())
        {
            //уничтожить подсоединенную вещь из инвентаря
            //.			pIItem->Drop					();
            pIItem->object().DestroyObject();
        };

        if (!ScopeRespawn(pIItem))
        {
            UpdateAddonsVisibility();
            InitAddons();
        }

        return true;
    }
    else
        return inherited::Attach(pIItem, b_send_event);
}

bool CWeaponMagazined::Detach(const char* item_section_name, bool b_spawn_item)
{
    if (m_eScopeStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope) && (m_sScopeName == item_section_name))
    {
        m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonScope;

        if (!ScopeRespawn(nullptr))
        {
            UpdateAddonsVisibility();
            InitAddons();
        }

        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    }
    else if (m_eSilencerStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer) &&
             (m_sSilencerName == item_section_name))
    {
        m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonSilencer;

        UpdateAddonsVisibility();
        InitAddons();
        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    }
    else if (m_eGrenadeLauncherStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) &&
             (m_sGrenadeLauncherName == item_section_name))
    {
        m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher;

        UpdateAddonsVisibility();
        InitAddons();
        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    }

    else if (m_eBayonetStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonBayonet) &&
             (m_sBayonetName == item_section_name))
    {
        m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonBayonet;

        UpdateAddonsVisibility();
        InitAddons();
        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    }

    else if (m_eRailStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonRail) && (m_sRailName == item_section_name))
    {
        m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonRail;

        UpdateAddonsVisibility();
        InitAddons();
        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    }

    else if (m_eLaserStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonLaser) && (m_sLLaserName == item_section_name))
    {
        m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonLaser;

        UpdateAddonsVisibility();
        InitAddons();
        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    }

    else if (m_eTorchStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonTorch) && (m_sTorchName == item_section_name))
    {
        m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonTorch;

        UpdateAddonsVisibility();
        InitAddons();
        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    }
    else if (m_eTactHandlerStatus == CSE_ALifeItemWeapon::eAddonAttachable && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonTactHandler) && (m_sTactHandlerName == item_section_name))
    {
        m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonTactHandler;

        UpdateAddonsVisibility();
        InitAddons();
        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    }



    else
        return inherited::Detach(item_section_name, b_spawn_item);
}


extern int scope_2dtexactive;
/*void CWeaponMagazined::InitZoomParams(LPCSTR section, bool useTexture)
{
    m_fMinZoomK = def_min_zoom_k;
    m_fZoomStepCount = def_zoom_step_count;

    LPCSTR dynamicZoomParams = READ_IF_EXISTS(pSettings, r_string, section, "scope_dynamic_zoom", NULL);
    if (dynamicZoomParams)
    {
        int num_zoom_param = _GetItemCount(dynamicZoomParams);

        ASSERT_FMT(num_zoom_param >= 1, "!![%s] : Invalid scope_dynamic_zoom parameter in section [%s]", __FUNCTION__, section);

        string128 tmp;
        m_bScopeDynamicZoom = CInifile::IsBOOL(_GetItem(dynamicZoomParams, 0, tmp));

        if (num_zoom_param > 1)
            m_fZoomStepCount = atof(_GetItem(dynamicZoomParams, 1, tmp));

        if (num_zoom_param > 2)
            m_fMinZoomK = atof(_GetItem(dynamicZoomParams, 2, tmp));
    }
    else
        m_bScopeDynamicZoom = false;

    m_fScopeInertionFactor = READ_IF_EXISTS(pSettings, r_float, section, "scope_inertion_factor", m_fControlInertionFactor);
    clamp(m_fScopeInertionFactor, m_fControlInertionFactor, m_fScopeInertionFactor);

    m_fScopeZoomFactor = pSettings->r_float(section, "scope_zoom_factor");
    m_fSecondVPZoomFactor = READ_IF_EXISTS(pSettings, r_float, section, "scope_lense_fov_factor", 0.0f);

    m_fZoomHudFov = READ_IF_EXISTS(pSettings, r_float, section, "scope_zoom_hud_fov", 0.0f);
    m_fSecondVPHudFov = READ_IF_EXISTS(pSettings, r_float, section, "scope_lense_hud_fov", 0.0f);

    if (m_UIScope)
    {
        xr_delete(m_UIScope);
        scope_2dtexactive = 0;
    }
    if (useTexture)
    {
        shared_str scope_tex_name = READ_IF_EXISTS(pSettings, r_string, section, "scope_texture", "");

        if (scope_tex_name.size() > 0)
        {
            m_UIScope = xr_new<CUIStaticItem>();
            m_UIScope->Init(scope_tex_name.c_str(), Core.Features.test(xrCore::Feature::scope_textures_autoresize) ? "hud\\scope" : "hud\\default", 0, 0, alNone);
        }
    }
}*/
void CWeaponMagazined::InitZoomParams(LPCSTR section, bool useTexture)
{
    m_fMinZoomK = def_min_zoom_k;
    m_fZoomStepCount = def_zoom_step_count;

    LPCSTR dynamicZoomParams = READ_IF_EXISTS(pSettings, r_string, section, "scope_dynamic_zoom", NULL);
    if (dynamicZoomParams)
    {
        int num_zoom_param = _GetItemCount(dynamicZoomParams);

        ASSERT_FMT(num_zoom_param >= 1, "!![%s] : Invalid scope_dynamic_zoom parameter in section [%s]", __FUNCTION__, section);

        string128 tmp;
        m_bScopeDynamicZoom = CInifile::IsBOOL(_GetItem(dynamicZoomParams, 0, tmp));

        if (num_zoom_param > 1)
            m_fZoomStepCount = atof(_GetItem(dynamicZoomParams, 1, tmp));

        if (num_zoom_param > 2)
            m_fMinZoomK = atof(_GetItem(dynamicZoomParams, 2, tmp));
    }
    else
        m_bScopeDynamicZoom = false;

    m_fScopeInertionFactor = READ_IF_EXISTS(pSettings, r_float, section, "scope_inertion_factor", m_fControlInertionFactor);
    clamp(m_fScopeInertionFactor, m_fControlInertionFactor, m_fScopeInertionFactor);

    m_fScopeZoomFactor = pSettings->r_float(section, "scope_zoom_factor");
    m_fSecondVPZoomFactor = READ_IF_EXISTS(pSettings, r_float, section, "scope_lense_fov_factor", 0.0f);

    m_fZoomHudFov = READ_IF_EXISTS(pSettings, r_float, section, "scope_zoom_hud_fov", 0.0f);
    m_fSecondVPHudFov = READ_IF_EXISTS(pSettings, r_float, section, "scope_lense_hud_fov", 0.0f);

    if (m_UIScope)
    {
        xr_delete(m_UIScope);
        scope_2dtexactive = 0;
    }
    if (useTexture)
    {
        shared_str scope_tex_name = READ_IF_EXISTS(pSettings, r_string, section, "scope_texture", "");

        if (scope_tex_name.size() > 0 && !scope_tex_name.equal("none"))
        {
            m_UIScope = xr_new<CUIWindow>();

            bool was_set = false;

            if (Core.Features.test(xrCore::Feature::cop_style_scope_texture))
            {
                if (!g_wpnScopeXml)
                {
                    g_wpnScopeXml = xr_new<CUIXml>();
                    g_wpnScopeXml->Init(CONFIG_PATH, UI_PATH, "scopes.xml");
                }

                if (g_wpnScopeXml->NavigateToNode(scope_tex_name.c_str()))
                {
                    CUIXmlInit::InitWindow(*g_wpnScopeXml, scope_tex_name.c_str(), 0, m_UIScope);

                    was_set = true;
                }
                else if (g_wpnScopeXml->NavigateToNode("wpn_crosshair_fallback") && READ_IF_EXISTS(pSettings, r_bool, section, "wpn_crosshair_fallback", true))
                {
                    CUIXmlInit::InitWindow(*g_wpnScopeXml, "wpn_crosshair_fallback", 0, m_UIScope);

                    was_set = true;

                    CUIWindow* scope_wnd = m_UIScope->FindChild("scope_texture");
                    if (scope_wnd && smart_cast<CUIStatic*>(scope_wnd))
                    {
                        smart_cast<CUIStatic*>(scope_wnd)->InitTexture(scope_tex_name.c_str());
                    }
                }
            }

            if (!was_set)
            {
                m_UIScope->SetWndRect(0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);

                // legacy mode
                CUIStatic* inner = xr_new<CUIStatic>();
                inner->SetAutoDelete(true);
                inner->SetWndRect(0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);
                inner->SetStretchTexture(true);
                inner->GetStaticItem()->Init(scope_tex_name.c_str(), Core.Features.test(xrCore::Feature::scope_textures_autoresize) ? "hud\\scope" : "hud\\default", 0, 0, alNone);
                m_UIScope->AttachChild(inner);
            }
        }
    }
}



void CWeaponMagazined::InitAddons()
{
    //////////////////////////////////////////////////////////////////////////
    // Прицел
    m_fIronSightZoomFactor = READ_IF_EXISTS(pSettings, r_float, cNameSect(), "ironsight_zoom_factor", 50.0f);

    if (IsScopeAttached())
    {
        if (m_eScopeStatus == ALife::eAddonAttachable)
        {
            m_sScopeName = pSettings->r_string(cNameSect(), "scope_name");
            m_iScopeX = pSettings->r_s32(cNameSect(), "scope_x");
            m_iScopeY = pSettings->r_s32(cNameSect(), "scope_y");

            InitZoomParams(*m_sScopeName, !m_bIgnoreScopeTexture);

            m_fZoomHudFov = READ_IF_EXISTS(pSettings, r_float, cNameSect().c_str(), "scope_zoom_hud_fov", m_fZoomHudFov);
            m_fSecondVPHudFov = READ_IF_EXISTS(pSettings, r_float, cNameSect().c_str(), "scope_lense_hud_fov", m_fSecondVPHudFov);
        }
        else if (m_eScopeStatus == ALife::eAddonPermanent)
        {
            InitZoomParams(cNameSect().c_str(), !m_bIgnoreScopeTexture);

            // CWeaponBinoculars always use dynamic zoom
            m_bScopeDynamicZoom = m_bScopeDynamicZoom || !!smart_cast<CWeaponBinoculars*>(this);
        }
    }
    else
    {
        m_bScopeDynamicZoom = false;

        if (IsZoomEnabled())
        {
            InitZoomParams(cNameSect().c_str(), !!READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "force_scope_texture", false));

            // for weapon without any scope - scope_zoom_factor will overrider ironsight_zoom_factor
            m_fIronSightZoomFactor = m_fScopeZoomFactor;
        }
        else
        {
            m_fSecondVPZoomFactor = 0.0f;
            m_fZoomHudFov = 0.0f;
            m_fSecondVPHudFov = 0.0f;
            m_fScopeInertionFactor = m_fControlInertionFactor;
        }
    }

    if (m_bScopeDynamicZoom)
    {
        if (SecondVPEnabled())
        {
            float delta, min_zoom_factor;
            GetZoomData(m_fSecondVPZoomFactor, delta, min_zoom_factor);

            m_fRTZoomFactor = min_zoom_factor;
        }
        else
        {
            if (Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system))
            {
                float delta, min_zoom_factor;
                GetZoomData(m_fScopeZoomFactor, delta, min_zoom_factor);

                m_fRTZoomFactor = min_zoom_factor; // set minimal zoom by default for ogse mode
            }
            else
            {
                m_fRTZoomFactor = m_fScopeZoomFactor;
            }
        }
    }

    if (IsSilencerAttached() && SilencerAttachable())
    {
        m_sFlameParticlesCurrent = m_sSilencerFlameParticles;
        m_sSmokeParticlesCurrent = m_sSilencerSmokeParticles;
        m_pSndShotCurrent = &sndSilencerShot;

        m_sSndShotCurrent = "sndSilencerShot";
        //сила выстрела
        LoadFireParams(*cNameSect(), "");

        //подсветка от выстрела
        LoadLights(*cNameSect(), "silencer_");

        ApplySilencerKoeffs();
    }
    else
    {
        m_sFlameParticlesCurrent = m_sFlameParticles;
        m_sSmokeParticlesCurrent = m_sSmokeParticles;
        m_pSndShotCurrent = &sndShot;

        m_sSndShotCurrent = "sndShot"; 
        //сила выстрела
        LoadFireParams(*cNameSect(), "");

        //подсветка от выстрела
        LoadLights(*cNameSect(), "");
    }

    inherited::InitAddons();
    callback(GameObject::eOnAddonInit)(1);

    m_fZoomFactor = CurrentZoomFactor();
}

void CWeaponMagazined::ApplySilencerKoeffs()
{
    float BHPk = 1.0f, BSk = 1.0f;
    float FDB_k = 1.0f, CD_k = 1.0f;

    if (pSettings->line_exist(m_sSilencerName, "bullet_hit_power_k"))
    {
        BHPk = pSettings->r_float(m_sSilencerName, "bullet_hit_power_k");
        clamp(BHPk, 0.0f, 1.0f);
    };
    if (pSettings->line_exist(m_sSilencerName, "bullet_speed_k"))
    {
        BSk = pSettings->r_float(m_sSilencerName, "bullet_speed_k");
        clamp(BSk, 0.0f, 1.0f);
    };
    if (pSettings->line_exist(m_sSilencerName, "fire_dispersion_base_k"))
    {
        FDB_k = pSettings->r_float(m_sSilencerName, "fire_dispersion_base_k");
        //		clamp(FDB_k, 0.0f, 1.0f);
    };
    if (pSettings->line_exist(m_sSilencerName, "cam_dispersion_k"))
    {
        CD_k = pSettings->r_float(m_sSilencerName, "cam_dispersion_k");
        clamp(CD_k, 0.0f, 1.0f);
    };

    // fHitPower			= fHitPower*BHPk;
    fvHitPower.mul(BHPk);
    fHitImpulse *= BSk;
    m_fStartBulletSpeed *= BSk;
    fireDispersionBase *= FDB_k;
    camDispersion *= CD_k;
    camDispersionInc *= CD_k;
}

//виртуальные функции для проигрывания анимации HUD
void CWeaponMagazined::PlayAnimShow()
{
    PlayHUDMotion({IsMisfire() ? "anm_show_jammed" : (iAmmoElapsed == 0 ? "anm_show_empty" : "nullptr"), "anim_draw", "anm_show"}, false, GetState());
}

void CWeaponMagazined::PlayAnimHide()
{
    PlayHUDMotion({IsMisfire() ? "anm_hide_jammed" : (iAmmoElapsed == 0 ? "anm_hide_empty" : "nullptr"), "anim_holster", "anm_hide"}, true, GetState());
}

void CWeaponMagazined::PlayAnimReload()
{
    if (IsMisfire())
        PlayHUDMotion({iAmmoElapsed == 1 ? "anm_reload_jammed_last" : "anm_reload_jammed", "anm_reload_empty", "anim_reload", "anm_reload"}, true, GetState());
    else if (IsPartlyReloading())
        PlayHUDMotion({"anim_reload_partly", "anm_reload_partly", "anim_reload", "anm_reload"}, true, GetState());
    else
        PlayHUDMotion({"anm_reload_empty", "anim_reload", "anm_reload"}, true, GetState());
}

const char* CWeaponMagazined::GetAnimAimName()
{
    if (auto pActor = smart_cast<const CActor*>(H_Parent()))
    {
        if (AnmIdleMovingAllowed())
        {
            if (const u32 state = pActor->get_state(); state & mcAnyMove)
            {
                if (IsScopeAttached())
                    return xr_strconcat(guns_aim_anm, "anm_idle_aim_scope_moving", IsMisfire() ? "_jammed" : (iAmmoElapsed == 0 ? "_empty" : ""));
                else
                    return xr_strconcat(guns_aim_anm, "anm_idle_aim_moving", (state & mcFwd) ? "_forward" : ((state & mcBack) ? "_back" : ""),
                                        (state & mcLStrafe) ? "_left" : ((state & mcRStrafe) ? "_right" : ""), IsMisfire() ? "_jammed" : (iAmmoElapsed == 0 ? "_empty" : ""));
            }
        }
    }
    return nullptr;
}

void CWeaponMagazined::PlayAnimAim()
{
    if (IsRotatingToZoom())
    {
        string128 guns_aim_start_anm;
        xr_strconcat(guns_aim_start_anm, "anm_idle_aim_start", IsMisfire() ? "_jammed" : (iAmmoElapsed == 0 ? "_empty" : ""));
        if (AnimationExist(guns_aim_start_anm))
        {
            PlayHUDMotion(guns_aim_start_anm, true, GetState());
            PlaySound(sndAimStart, get_LastFP());
            return;
        }
    }

    if (const char* guns_aim_anm = GetAnimAimName())
    {
        if (AnimationExist(guns_aim_anm))
        {
            PlayHUDMotion(guns_aim_anm, true, GetState());
            return;
        }
    }

    PlayHUDMotion({IsMisfire() ? "anm_idle_aim_jammed" : (iAmmoElapsed == 0 ? "anm_idle_aim_empty" : "nullptr"), "anim_idle_aim", "anm_idle_aim"}, true, GetState());
}

void CWeaponMagazined::PlayAnimIdle()
{
    if (GetState() != eIdle)
        return;

    if (IsZoomed())
        PlayAnimAim();
    else
    {
        if (IsRotatingFromZoom())
        {
            string128 guns_aim_end_anm;
            xr_strconcat(guns_aim_end_anm, "anm_idle_aim_end", IsMisfire() ? "_jammed" : (iAmmoElapsed == 0 ? "_empty" : ""));
            if (AnimationExist(guns_aim_end_anm))
            {
                PlayHUDMotion(guns_aim_end_anm, true, GetState());
                PlaySound(sndAimEnd, get_LastFP());
                return;
            }
        }

        inherited::PlayAnimIdle();
    }
}

void CWeaponMagazined::PlayAnimShoot()
{
    string128 guns_shoot_anm;
    xr_strconcat(guns_shoot_anm, "anm_shoot", (IsZoomed() && !IsRotatingToZoom()) ? (IsScopeAttached() ? "_aim_scope" : "_aim") : "", iAmmoElapsed == 1 ? "_last" : "",
                 IsSilencerAttached() ? "_sil" : "");

    PlayHUDMotion({guns_shoot_anm, "anim_shoot", "anm_shots"}, true, GetState());
}

void CWeaponMagazined::PlayAnimFakeShoot()
{
    auto wpn = smart_cast<CWeapon*>(this);
    string128 guns_fakeshoot_anm;
    xr_strconcat(guns_fakeshoot_anm, "anm_fakeshoot",
                 (IsZoomed() && !IsRotatingToZoom()) ? (IsMisfire() ? "_aim_jammed" : "_aim") : ((IsGrenadeMode() && IsMisfire()) ? "_jammed" : ""),
                 ((iAmmoElapsed == 0 && !IsGrenadeMode()) || (wpn && wpn->GetAmmoElapsed2() == 0 && IsGrenadeMode())) ? "_empty" : "",
                 IsGrenadeLauncherAttached() ? (!IsGrenadeMode() ? "_w_gl" : "_g") : "");
    if (AnimationExist(guns_fakeshoot_anm))
        PlayHUDMotion(guns_fakeshoot_anm, true, GetState());
}

void CWeaponMagazined::PlayAnimCheckMisfire()
{
    string128 guns_fakeshoot_anm;
    xr_strconcat(guns_fakeshoot_anm, "anm_fakeshoot", IsMisfire() ? "_jammed" : "", IsGrenadeLauncherAttached() ? (!IsGrenadeMode() ? "_w_gl" : "_g") : "");
    if (AnimationExist(guns_fakeshoot_anm))
        PlayHUDMotion(guns_fakeshoot_anm, true, GetState());
    else
        SwitchState(eIdle);
}

void CWeaponMagazined::PlayAnimDeviceSwitch()
{
    PlaySound((HeadLampSwitch || NightVisionSwitch) ? sndItemOn : sndTactItemOn, get_LastFP());

    auto wpn = smart_cast<CWeapon*>(this);
    string128 guns_device_anm;
    xr_strconcat(guns_device_anm, LaserSwitch ? "anm_laser_on" : (TorchSwitch ? "anm_torch_on" : ((HeadLampSwitch || NightVisionSwitch) ? "anm_headlamp_on" : "")),
                 IsMisfire()                                                                                              ? "_jammed" :
                     ((iAmmoElapsed == 0 && !IsGrenadeMode()) || (wpn && wpn->GetAmmoElapsed2() == 0 && IsGrenadeMode())) ? "_empty" :
                                                                                                                            "",
                 (IsGrenadeLauncherAttached()) ? (!IsGrenadeMode() ? "_w_gl" : "_g") : "");
    if (AnimationExist(guns_device_anm))
        PlayHUDMotion(guns_device_anm, true, GetState());
    else
    {
        DeviceUpdate();
        SwitchState(eIdle);
    }
}

void CWeaponMagazined::OnZoomIn()
{
    inherited::OnZoomIn();

    if (GetState() == eIdle)
        PlayAnimIdle();
 
    auto pActor = smart_cast<CActor*>(H_Parent());
    pActor->SetSafemode(false);
    if (pActor)
    {
        CEffectorCam* ec = pActor->Cameras().GetCamEffector(eCEActorMoving);
        if (ec)
            pActor->Cameras().RemoveCamEffector(eCEActorMoving);

        CEffectorZoomInertion* S = smart_cast<CEffectorZoomInertion*>(pActor->Cameras().GetCamEffector(eCEZoom));
        if (!S)
        {
            S = (CEffectorZoomInertion*)pActor->Cameras().AddCamEffector(xr_new<CEffectorZoomInertion>());
            S->Init(this);
        }
        S->SetRndSeed(pActor->GetZoomRndSeed());
        R_ASSERT(S);

        if (m_bVision && !m_binoc_vision)
            m_binoc_vision = xr_new<CBinocularsVision>(this);
    }
}
void CWeaponMagazined::OnZoomOut()
{
    if (!m_bZoomMode)
        return;

    inherited::OnZoomOut();

    if (GetState() == eIdle)
        PlayAnimIdle();

    CActor* pActor = smart_cast<CActor*>(H_Parent());
    if (pActor)
    {
        pActor->Cameras().RemoveCamEffector(eCEZoom);
        if (m_bVision)
        {
            VERIFY(m_binoc_vision);
            xr_delete(m_binoc_vision);
        }
    }
}

void CWeaponMagazined::OnZoomChanged() { PlaySound(sndZoomChange, get_LastFP()); }

//переключение режимов стрельбы одиночными и очередями
bool CWeaponMagazined::SwitchMode()
{
    if (eIdle != GetState() || IsPending())
        return false;

    if (SingleShotMode())
        m_iQueueSize = WEAPON_ININITE_QUEUE;
    else
        m_iQueueSize = 1;

    PlaySound(sndEmptyClick, get_LastFP());

    return true;
}

void CWeaponMagazined::OnNextFireMode()
{
    if (m_aFireModes.size() < 2)
        return;
    m_iCurFireMode = (m_iCurFireMode + 1 + m_aFireModes.size()) % m_aFireModes.size();
    SetQueueSize(GetCurrentFireMode());

    string128 guns_aim_next_fmode_anm;
    xr_strconcat(guns_aim_next_fmode_anm, "anm_changefiremode_from_1_to_a", IsMisfire() ? "_jammed" : (iAmmoElapsed == 0 ? "_empty" : ""),  
        IsGrenadeLauncherAttached() ? (!IsGrenadeMode() ? "_w_gl" : "_g") : "");
    if (AnimationExist(guns_aim_next_fmode_anm))
        PlayHUDMotion(guns_aim_next_fmode_anm, true, GetState());

    PlaySound(sndFireModes, get_LastFP());
    SetPending(TRUE);
}

void CWeaponMagazined::OnPrevFireMode()
{
    if (m_aFireModes.size() < 2)
        return;
    m_iCurFireMode = (m_iCurFireMode - 1 + m_aFireModes.size()) % m_aFireModes.size();
    SetQueueSize(GetCurrentFireMode());

    string128 guns_aim_prev_fmode_anm;
    xr_strconcat(guns_aim_prev_fmode_anm, "anm_changefiremode_from_a_to_1", IsMisfire() ? "_jammed" : (iAmmoElapsed == 0 ? "_empty" : ""), 
        IsGrenadeLauncherAttached() ? (!IsGrenadeMode() ? "_w_gl" : "_g") : "");
    if (AnimationExist(guns_aim_prev_fmode_anm))
        PlayHUDMotion(guns_aim_prev_fmode_anm, true, GetState());

    PlaySound(sndFireModes, get_LastFP());
    SetPending(TRUE);
}


void CWeaponMagazined::BayonetKick() {


    string128 guns_anm_kick;
    xr_strconcat(guns_anm_kick, "anm_kick", IsMisfire() ? "_jammed" : (iAmmoElapsed == 0 ? "_empty" : ""),
                 IsGrenadeLauncherAttached() ? (!IsGrenadeMode() ? "_w_gl" : "_g") : "");

    if (AnimationExist(guns_anm_kick))
    {
        IsBayonetKick = true;
        PlayHUDMotion(guns_anm_kick, true, GetState());
        PlaySound(sndAnmKick, get_LastFP());
        SetPending(TRUE);
    }

}

void CWeaponMagazined::MakeBayonetHit() {
    CCartridge cartridge;
    cartridge.m_buckShot = 1;
    cartridge.m_impair = 1;
    cartridge.m_kDisp = 1;
    cartridge.m_kHit = 1;
    cartridge.m_kImpulse = 1;
    cartridge.m_kPierce = 1;
    cartridge.m_flags.set(CCartridge::cfTracer, FALSE);
    cartridge.m_flags.set(CCartridge::cfRicochet, FALSE);
    cartridge.fWallmarkSize = fWallmarkSize;
    cartridge.bullet_material_idx = knife_material_idx;
    const bool send_hit = SendHitAllowed(H_Parent());

    int last_amm = iAmmoElapsed;
    if (iAmmoElapsed == 0)
        iAmmoElapsed+=1;

    SBullet& bullet = Level().BulletManager().AddBullet(get_LastFP(), get_LastFD(), m_fStartBulletSpeed, cur_fHit, cur_fHitImpulse, H_Parent()->ID(), ID(), cur_bayonet_hittype,
                                                        fireDistanceBayonet,
                                                        cartridge, send_hit);
    if (ParentIsActor())
        bullet.setOnBulletHit(true);

    if (last_amm == 0)
        iAmmoElapsed -= 1;
}


void CWeaponMagazined::OnH_A_Chield()
{
    if (m_bHasDifferentFireModes)
    {
        CActor* actor = smart_cast<CActor*>(H_Parent());
        if (!actor)
            SetQueueSize(-1);
        else
            SetQueueSize(GetCurrentFireMode());
    };
    inherited::OnH_A_Chield();
};

void CWeaponMagazined::SetQueueSize(int size)
{
    m_iQueueSize = size;
    if (m_iQueueSize == -1)
        strcpy_s(m_sCurFireMode, " (A)");
    else
        sprintf_s(m_sCurFireMode, " (%d)", m_iQueueSize);
};

float CWeaponMagazined::GetWeaponDeterioration()
{
    if (!m_bHasDifferentFireModes || m_iPrefferedFireMode == -1 || u32(GetCurrentFireMode()) <= u32(m_iPrefferedFireMode))
    {
        if (IsSilencerAttached() && SilencerAttachable())
            return conditionDecreasePerShotSilencer;
        else
            return inherited::GetWeaponDeterioration();
    }
    if (IsSilencerAttached() && SilencerAttachable())
        return m_iShotNum * conditionDecreasePerShotSilencer;
    return m_iShotNum * conditionDecreasePerShot;
}

void CWeaponMagazined::save(NET_Packet& output_packet)
{
    inherited::save(output_packet);
    save_data(m_iQueueSize, output_packet);
    save_data(m_iShotNum, output_packet);
    save_data(m_iCurFireMode, output_packet);
}

void CWeaponMagazined::load(IReader& input_packet)
{
    inherited::load(input_packet);
    load_data(m_iQueueSize, input_packet);
    SetQueueSize(m_iQueueSize);
    load_data(m_iShotNum, input_packet);
    load_data(m_iCurFireMode, input_packet);
}

void CWeaponMagazined::net_Export(CSE_Abstract* E)
{
    inherited::net_Export(E);
    CSE_ALifeItemWeaponMagazined* wpn = smart_cast<CSE_ALifeItemWeaponMagazined*>(E);
    wpn->m_u8CurFireMode = u8(m_iCurFireMode & 0x00ff);
}

void CWeaponMagazined::GetBriefInfo(xr_string& str_name, xr_string& icon_sect_name, xr_string& str_count)
{
    const int AE = GetAmmoElapsed(), AC = GetAmmoCurrent();

    if (AE == 0 || m_magazine.empty())
        icon_sect_name = m_ammoTypes[m_ammoType].c_str();
    else
        icon_sect_name = m_ammoTypes[m_magazine.back().m_LocalAmmoType].c_str();

    string256 sItemName;
    strcpy_s(sItemName, CStringTable().translate(pSettings->r_string(icon_sect_name.c_str(), "inv_name_short")).c_str());

    if (HasFireModes())
        strcat_s(sItemName, GetCurrentFireModeStr());

    str_name = sItemName;

    static const std::regex ae_re{R"(\{AE\})"}, ac_re{R"(\{AC\})"};
    str_count = m_str_count_tmpl;
    str_count = std::regex_replace(str_count, ae_re, std::to_string(AE));
    str_count = std::regex_replace(str_count, ac_re, unlimited_ammo() ? "--" : std::to_string(AC - AE));
}

void CWeaponMagazined::OnDrawUI()
{
    if (H_Parent() && IsZoomed() && !IsRotatingToZoom() && m_binoc_vision)
        m_binoc_vision->Draw();
    inherited::OnDrawUI();
}
void CWeaponMagazined::net_Relcase(CObject* object)
{
    if (!m_binoc_vision)
        return;

    m_binoc_vision->remove_links(object);
}

bool CWeaponMagazined::ScopeRespawn(PIItem pIItem)
{
    xr_string scope_respawn = "scope_respawn";
    if (ScopeAttachable() && IsScopeAttached())
    {
        scope_respawn += "_";
        if (smart_cast<CScope*>(pIItem))
            scope_respawn += pIItem->object().cNameSect().c_str();
        else
            scope_respawn += m_sScopeName.c_str();
    }

    if (pSettings->line_exist(cNameSect(), scope_respawn.c_str()))
    {
        LPCSTR S = pSettings->r_string(cNameSect(), scope_respawn.c_str());
        if (xr_strcmp(cName().c_str(), S) != 0)
        {
            CSE_Abstract* _abstract = Level().spawn_item(S, Position(), ai_location().level_vertex_id(), H_Parent()->ID(), true);
            CSE_ALifeDynamicObject* sobj1 = alife_object();
            CSE_ALifeDynamicObject* sobj2 = smart_cast<CSE_ALifeDynamicObject*>(_abstract);

            NET_Packet P;
            P.w_begin(M_UPDATE);
            u32 position = P.w_tell();
            P.w_u16(0);
            sobj1->STATE_Write(P);
            u16 size = u16(P.w_tell() - position);
            P.w_seek(position, &size, sizeof(u16));
            u16 id;
            P.r_begin(id);
            P.r_u16(size);
            sobj2->STATE_Read(P, size);

            net_Export(_abstract);

            auto io = smart_cast<CInventoryOwner*>(H_Parent());
            auto ii = smart_cast<CInventoryItem*>(this);
            if (io)
            {
                if (io->inventory().InSlot(ii))
                    io->SetNextItemSlot(ii->GetSlot());
                else
                    io->SetNextItemSlot(0);
            }

            DestroyObject();
            sobj2->Spawn_Write(P, TRUE);
            Level().Send(P, net_flags(TRUE));
            F_entity_Destroy(_abstract);

            return true;
        }
    }
    return false;
}
