#include "StdAfx.h"
#include "CustomDetector.h"
#include "ui/ArtefactDetectorUI.h"
#include "HUDManager.h"
#include "Inventory.h"
#include "Level.h"
#include "map_manager.h"
#include "ActorEffector.h"
#include "Actor.h"
#include "player_hud.h"
#include "Weapon.h"

#include "..\xr_3da\LightAnimLibrary.h"

ITEM_INFO::~ITEM_INFO()
{
    if (pParticle)
        CParticlesObject::Destroy(pParticle);
}

bool CCustomDetector::CheckCompatibilityInt(CHudItem* itm, u16* slot_to_activate)
{
    if (itm == nullptr)
        return true;

    CInventoryItem& iitm = itm->item();
    u32 slot = iitm.BaseSlot();
    bool bres = (slot == FIRST_WEAPON_SLOT || slot == KNIFE_SLOT || slot == BOLT_SLOT);
    auto pActor = smart_cast<CActor*>(H_Parent());
    auto& Inv = pActor->inventory();

    if (!bres && slot_to_activate)
    {
        *slot_to_activate = NO_ACTIVE_SLOT;
        if (Inv.ItemFromSlot(BOLT_SLOT))
            *slot_to_activate = BOLT_SLOT;

        if (Inv.ItemFromSlot(KNIFE_SLOT))
            *slot_to_activate = KNIFE_SLOT;

        if (Inv.ItemFromSlot(SECOND_WEAPON_SLOT) && Inv.ItemFromSlot(SECOND_WEAPON_SLOT)->BaseSlot() != SECOND_WEAPON_SLOT)
            *slot_to_activate = SECOND_WEAPON_SLOT;

        if (Inv.ItemFromSlot(FIRST_WEAPON_SLOT) && Inv.ItemFromSlot(FIRST_WEAPON_SLOT)->BaseSlot() != SECOND_WEAPON_SLOT)
            *slot_to_activate = FIRST_WEAPON_SLOT;

        if (*slot_to_activate != NO_ACTIVE_SLOT)
            bres = true;
    }

    if (itm->GetState() != CHUDState::eShowing)
        bres = bres && !itm->IsPending();

    if (bres)
    {
        CWeapon* W = smart_cast<CWeapon*>(itm);
        if (W)
            bres = bres && (W->GetState() != CHUDState::eBore) && (W->GetState() != CWeapon::eReload) && (W->GetState() != CWeapon::eSwitch) /* && !W->IsZoomed()*/;
    }
    return bres;
}

bool CCustomDetector::CheckCompatibility(CHudItem* itm)
{
    if (!inherited::CheckCompatibility(itm))
        return false;

   // if (ZoomedIn)
    //    return false;

    if (!CheckCompatibilityInt(itm, nullptr))
    {
        HideDetector(true);
        return false;
    }
    return true;
}

void CCustomDetector::HideDetector(bool bFastMode)
{
    if (GetState() == eIdle)
        ToggleDetector(bFastMode);
}

void CCustomDetector::ShowDetector(bool bFastMode)
{
    if (GetState() == eHidden)
        ToggleDetector(bFastMode);
}

void CCustomDetector::ToggleDetector(bool bFastMode)
{
   // if (ZoomedIn)
    //    return;

    m_bNeedActivation = false;
    m_bFastAnimMode = bFastMode;

    if (GetState() == eHidden)
    {
        CActor* pActor = smart_cast<CActor*>(H_Parent());
        PIItem iitem = pActor->inventory().ActiveItem();
        CHudItem* itm = (iitem) ? iitem->cast_hud_item() : nullptr;
        u16 slot_to_activate = NO_ACTIVE_SLOT;

        if (CheckCompatibilityInt(itm, &slot_to_activate))
        {
            if (slot_to_activate != NO_ACTIVE_SLOT)
            {
                pActor->inventory().Activate(slot_to_activate);
                m_bNeedActivation = true;
            }
            else
            {
                SwitchState(eShowing);
                TurnDetectorInternal(true);
            }
        }
    }
    else if (GetState() == eIdle)
        SwitchState(eHiding);
}

void CCustomDetector::OnStateSwitch(u32 S, u32 oldState)
{
    inherited::OnStateSwitch(S, oldState);

    switch (S)
    {
    case eShowing: {
        g_player_hud->attach_item(this);
        HUD_SOUND::PlaySound(sndShow, Fvector{}, this, !!GetHUDmode(), false, false);
        PlayHUDMotion({m_bFastAnimMode ? "anm_show_fast" : "anm_show"}, false, GetState());
        SetPending(TRUE);
    }
    break;
    case eHiding: {
        if (oldState != eHiding)
        {
            HUD_SOUND::PlaySound(sndHide, Fvector{}, this, !!GetHUDmode(), false, false);
            PlayHUDMotion({m_bFastAnimMode ? "anm_hide_fast" : "anm_hide"}, true, GetState());
            SetPending(TRUE);
        }
    }
    break;
    case eIdle: {
        PlayAnimIdle();
        SetPending(FALSE);
    }
    break;
    case eIdleZoomIn: {
        if (AnimationExist("anm_idle_aim_start"))
        {
            PlayHUDMotion("anm_idle_aim_start", false, eIdleZoomIn);
            SetPending(TRUE);
        }
    }
    break;
    case eIdleZoomOut: {
        if (AnimationExist("anm_idle_aim_end"))
        {
            PlayHUDMotion("anm_idle_aim_end", false, eIdleZoomOut);
            SetPending(TRUE);
        }
    }
    break;
    case eIdleZoom: {
        PlayIdleAimAnm();
    }
    break;
    }
}

void CCustomDetector::OnAnimationEnd(u32 state)
{
    switch (state)
    {
    case eShowing: {
        SwitchState(eIdle);
        if (m_fDecayRate > 0.f)
            this->SetCondition(-m_fDecayRate);
    }
    break;
    case eHiding: {
        SwitchState(eHidden);
        TurnDetectorInternal(false);
        g_player_hud->detach_item(this);
    }
    break;
    case eIdle: SwitchState(eIdle); break;
    case eIdleZoomIn: SwitchState(eIdleZoom); break;
    case eIdleZoom:SwitchState(ZoomedIn ? eIdleZoom : eIdleZoomOut); break;
    case eIdleZoomOut: SwitchState(eIdle); break;
    default: inherited::OnAnimationEnd(state);
    }
}

void CCustomDetector::UpdateXForm() { CInventoryItem::UpdateXForm(); }

void CCustomDetector::OnActiveItem() {}

void CCustomDetector::OnHiddenItem() {}

CCustomDetector::~CCustomDetector()
{
    HUD_SOUND::DestroySound(sndShow);
    HUD_SOUND::DestroySound(sndHide);

    m_artefacts.destroy();
    TurnDetectorInternal(false);
    xr_delete(m_ui);
}

BOOL CCustomDetector::net_Spawn(CSE_Abstract* DC)
{
    TurnDetectorInternal(false);
    return inherited::net_Spawn(DC);
}

void CCustomDetector::Load(LPCSTR section)
{
    m_animation_slot = 7;
    inherited::Load(section);

    m_fAfDetectRadius = READ_IF_EXISTS(pSettings, r_float, section, "af_radius", 30.0f);
    m_fAfVisRadius = READ_IF_EXISTS(pSettings, r_float, section, "af_vis_radius", 2.0f);
    m_fDecayRate = READ_IF_EXISTS(pSettings, r_float, section, "decay_rate", 0.f); // Alundaio
    m_artefacts.load(section, "af");

    HUD_SOUND::LoadSound(section, "snd_draw", sndShow, SOUND_TYPE_ITEM_TAKING);
    HUD_SOUND::LoadSound(section, "snd_holster", sndHide, SOUND_TYPE_ITEM_HIDING);

    // Шоб не просило то, чего не нужно в класе фонарика
    IsFlashlight = READ_IF_EXISTS(pSettings, r_string, section, "flashlight_section", false);
}

void CCustomDetector::shedule_Update(u32 dt)
{
    inherited::shedule_Update(dt);

    if (!IsWorking())
        return;

    Position().set(H_Parent()->Position());

    Fvector P;
    P.set(H_Parent()->Position());

    if (GetCondition() <= 0.01f)
        return;

    if (!IsFlashlight)
        m_artefacts.feel_touch_update(P, m_fAfDetectRadius);
}

bool CCustomDetector::IsWorking() const { return m_bWorking && H_Parent() && H_Parent() == Level().CurrentViewEntity(); }

void CCustomDetector::UpfateWork()
{
    UpdateAf();
    m_ui->update();
}

void CCustomDetector::UpdateVisibility()
{
    // check visibility
    attachable_hud_item* i0 = g_player_hud->attached_item(0);
    if (i0 && HudItemData())
    {
        bool bClimb = ((Actor()->MovingState() & mcClimb) != 0);
        if (bClimb)
        {
            HideDetector(true);
            m_bNeedActivation = true;
        }
        else
        {
            auto wpn = smart_cast<CWeapon*>(i0->m_parent_hud_item);
            if (wpn)
            {
                u32 state = wpn->GetState();
                if (/* wpn->IsZoomed() ||*/ state == CWeapon::eReload || state == CWeapon::eSwitch)
                {
                    HideDetector(true);
                    m_bNeedActivation = true;
                }
            }
        }
    }
    else if (m_bNeedActivation)
    {
        attachable_hud_item* i0 = g_player_hud->attached_item(0);
        bool bClimb = ((Actor()->MovingState() & mcClimb) != 0);
        if (!bClimb)
        {
            CHudItem* huditem = (i0) ? i0->m_parent_hud_item : nullptr;
            bool bChecked = !huditem || CheckCompatibilityInt(huditem, 0);

            if (bChecked)
                ShowDetector(true);
        }
    }
}

void CCustomDetector::UpdateCL()
{
    inherited::UpdateCL();

    if (H_Parent() != Level().CurrentEntity())
        return;

    UpdateVisibility();
    if (!IsWorking())
        return;

    if (smart_cast<CActor*>(H_Parent()))
    {
        if (auto pActiveItem = g_actor->inventory().ActiveItem())
        {
            if (auto pWeapon = smart_cast<CWeapon*>(pActiveItem))
            {
                if (pWeapon->IsZoomed() && pWeapon->IsRotatingToZoom() && !ZoomedIn && !IsPending())
                {
                    SwitchState(eIdleZoomIn);
                    ZoomedIn = true;
                }
                if (!pWeapon->IsZoomed() && ZoomedIn)
                {
                    SwitchState(eIdleZoomOut);
                    ZoomedIn = false;
                }
            }
            else if (ZoomedIn)
                ZoomedIn = false;
        }
        else if (ZoomedIn)
            ZoomedIn = false;
    }


    if (!IsFlashlight)
        UpfateWork();
}

void CCustomDetector::OnH_A_Chield() { inherited::OnH_A_Chield(); }

void CCustomDetector::OnH_B_Independent(bool just_before_destroy)
{
    inherited::OnH_B_Independent(just_before_destroy);

    m_artefacts.clear();

    if (GetState() != eHidden)
    {
        // Detaching hud item and animation stop in OnH_A_Independent
        TurnDetectorInternal(false);
        SwitchState(eHidden);
    }
}

void CCustomDetector::OnMoveToRuck(EItemPlace prevPlace)
{
    inherited::OnMoveToRuck(prevPlace);
    if (prevPlace == eItemPlaceSlot)
    {
        SwitchState(eHidden);
        g_player_hud->detach_item(this);
    }
    TurnDetectorInternal(false);
    StopCurrentAnimWithoutCallback();
}

void CCustomDetector::OnMoveToSlot() { inherited::OnMoveToSlot(); }

void CCustomDetector::TurnDetectorInternal(bool b)
{
    m_bWorking = b;
    if (b && !m_ui)
        CreateUI();

    // UpdateNightVisionMode(b);
}

// void CCustomDetector::UpdateNightVisionMode(bool b_on) {}

Fvector CCustomDetector::GetPositionForCollision()
{
    Fvector det_pos{}, det_dir{};
    //Офсет подобрал через худ аждаст, это скорее всего временно, но такое решение подходит всем детекторам более-менее.
    GetBoneOffsetPosDir("wpn_body", det_pos, det_dir, Fvector{-0.247499f, -0.810510f, 0.178999f});
    return det_pos;
}

Fvector CCustomDetector::GetDirectionForCollision()
{
    //Пока и так нормально, в будущем мб придумаю решение получше.
    return Device.vCameraDirection;
}


void CCustomDetector::PlayIdleAimAnm() {
    // В анимках для ходьбы надыбал только walk(_right, _left, _back), поэтому пускай для начала
    // будет так, в будущем можно будет сделать остальные, просто ограничивая скорость walk анимки
    if (auto pActor = smart_cast<CActor*>(H_Parent()))
    { 
        if (pActor->is_actor_moving() && AnimationExist("anm_idle_aim_walk"))
            PlayHUDMotion("anm_idle_aim_walk", false, eIdleZoom);
        else
        {// впринципе, +-, но анимация зума у детекторов очень длинная(из-за этого может быть прикол с walk), мб обрезать её?
            if (AnimationExist("anm_idle_aim"))
                PlayHUDMotion("anm_idle_aim", false, eIdleZoom);
        }
        SetPending(FALSE);
    }
}


BOOL CAfList::feel_touch_contact(CObject* O)
{
    auto pAf = smart_cast<CArtefact*>(O);
    if (!pAf)
        return false;

    bool res = (m_TypesMap.find(O->cNameSect()) != m_TypesMap.end()) || (m_TypesMap.find("class_all") != m_TypesMap.end());
    if (res)
        if (pAf->GetAfRank() > m_af_rank)
            res = false;

    return res;
}

BOOL CZoneList::feel_touch_contact(CObject* O)
{
    auto pZone = smart_cast<CCustomZone*>(O);
    if (!pZone)
        return false;

    bool res = (m_TypesMap.find(O->cNameSect()) != m_TypesMap.end()) || (m_TypesMap.find("class_all") != m_TypesMap.end());
    if (!pZone->IsEnabled())
        res = false;

    return res;
}

CZoneList::~CZoneList()
{
    clear();
    destroy();
}




//////////////////////////////////////////////////////////
// 
// VlaGan: Ручной фонарик
// 
//////////////////////////////////////////////////////////

//#include "Flashlight.h"

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
        flashlight_attach_bone = pSettings->r_string(section, "torch_light_bone");
        flashlight_attach_offset = Fvector{pSettings->r_float(section, "torch_attach_offset_x"), pSettings->r_float(section, "torch_attach_offset_y"),
                                           pSettings->r_float(section, "torch_attach_offset_z")};
        flashlight_omni_attach_offset = Fvector{pSettings->r_float(section, "torch_omni_attach_offset_x"), pSettings->r_float(section, "torch_omni_attach_offset_y"),
                                                pSettings->r_float(section, "torch_omni_attach_offset_z")};

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

        // TODO: Оффсеты фонарика в аиме
        flashlight_attach_offset_aim = Fvector{pSettings->r_float(section, "torch_aim_attach_offset_x"),
                                               pSettings->r_float(section, "torch_aim_attach_offset_y"),
                                               pSettings->r_float(section, "torch_aim_attach_offset_z")}.add(flashlight_attach_offset);
        
        flashlight_discharge_speed = pSettings->r_float(section, "torch_discharge_speed");
    }
}

void CFlashlight::UpdateCL()
{
    inherited::UpdateCL();
    if (HudItemData())
        UpdateFlashlight();
    else
        DeactivateFLash();
}

void CFlashlight::UpdateFlashlight()
{
    if (this->m_fCondition < 0.05f)
    {
        if (flashlight_light_bone.size())
            if (HudItemData()->get_bone_visible(flashlight_light_bone))
                HudItemData()->set_bone_visible(flashlight_light_bone, false, true);
        else if (flashlight_render->get_active())
            {
                DeactivateFLash();
                return;
            }
        else
            return;
    }
    
    
    // нормально ли так будет?... (ин тайм вкл/выкл)
    if (GetState() == eShowing && !IsShown)
        if (flashlight_show_hide_factor < flashlight_show_time)
            flashlight_show_hide_factor += Device.fTimeDelta;
        else
        {
            flashlight_show_hide_factor = 0;
            IsShown = true;
        }
    else if (GetState() == eHiding) //&& IsShown)
        if (flashlight_show_hide_factor < flashlight_hide_time)
            flashlight_show_hide_factor += Device.fTimeDelta;
        else
        {
            flashlight_show_hide_factor = 0;
            IsShown = false;
        }

    // Скрытие кости света
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

            if (GetHUDmode()){                                                             
                GetBoneOffsetPosDir(flashlight_attach_bone, flashlight_pos, flashlight_dir, ZoomedIn ? flashlight_attach_offset_aim : flashlight_attach_offset);
                CorrectDirFromWorldToHud(flashlight_dir);

                GetBoneOffsetPosDir(flashlight_attach_bone, flashlight_pos_omni, flashlight_dir_omni, flashlight_omni_attach_offset);
                CorrectDirFromWorldToHud(flashlight_dir_omni);
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

                // переделать, ибо еффекта не видно, всё равно ярко
                fclr.mul_rgb((flashlight_fBrightness * (0.5f + (m_fCondition ? m_fCondition / 2.f: 0.f))) / 255.f);

                flashlight_render->set_color(fclr);
                flashlight_omni->set_color(fclr);
                flashlight_glow->set_color(fclr);
            }

             m_fCondition -= flashlight_discharge_speed;
        }
    }
}

void CFlashlight::OnMoveToSlot() { 
    inherited::OnMoveToSlot();
    DeactivateFLash();

};
void CFlashlight::OnMoveToRuck(EItemPlace prevPlace) { 
    inherited::OnMoveToRuck(prevPlace); 
    DeactivateFLash();
}

void CFlashlight::DeactivateFLash()
{
    if (flashlight_render)
        if (flashlight_render->get_active())
        {
            flashlight_render->set_active(false);
            flashlight_omni->set_active(false);
            flashlight_glow->set_active(false);
        }
    if(IsShown)IsShown = false;

    //if (flashlight_light_bone.size() && HudItemData())
     //   HudItemData()->set_bone_visible(flashlight_light_bone, IsShown, TRUE);
}