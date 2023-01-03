/*******************************/
/********** 3D-Гильзы **********/ //--#SM+#--
/*******************************/

#include "StdAfx.h"
#include "CustomShell.h"
#include "HUDManager.h"
//#include "xrPhysics/ExtendedGeom.h"
//#include "xrEngine/vis_object_data.h"
//#include "Include/xrRender/ParticleCustom.h"
#include "ExtendedGeom.h"
#include "..\xr_3da\GameMtlLib.h"


#include "ai_sounds.h"
//======= Описание класса и основные наследуемые методы =======//

CCustomShell::CCustomShell()
{
    dwHUDMode_upd_frame = u32(-1);
    m_bHUD_mode = false;

    m_dwDestroyTime = 0;
    m_dwDestroyOnCollideSafetime = u32(-1);
    m_dwDropOnFrameAfter = 0;
    m_dwFOVTranslateTime = 0;
    m_dwFOVStableTime = 0;

    m_launch_point_idx = 0;

    m_bItWasHudShell = false;
    m_bIgnoreGeometry_3p = false;
    m_bIgnoreGeometry_hud = false;

    m_bSndWasPlayed = false;
    m_sShellHitSndList = nullptr;
}

CCustomShell::~CCustomShell()
{
    m_pShellHitSnd.destroy();
}

void CCustomShell::Load(LPCSTR section)
{
    inherited::Load(section);

    Msg("! SHELL OBJ HAS BEEN LOADED !");

    reload(section);
}

void CCustomShell::reload(LPCSTR section)
{
    inherited::reload(section);

    m_bIgnoreGeometry_3p = READ_IF_EXISTS(pSettings, r_bool, this->cNameSect(), "shell_ignore_geometry_3p", false);
    m_bIgnoreGeometry_hud = READ_IF_EXISTS(pSettings, r_bool, this->cNameSect(), "shell_ignore_geometry_hud", false);

    m_sShellHitSndList = READ_IF_EXISTS(pSettings, r_string, this->cNameSect(), "shell_hit_snd_list", nullptr);
    m_fSndVolume = READ_IF_EXISTS(pSettings, r_float, this->cNameSect(), "shell_hit_snd_volume", 1.0f);
    m_vSndRange = READ_IF_EXISTS(pSettings, r_fvector2, this->cNameSect(), "shell_hit_snd_range",
        Fvector2().set(SHELL3D_SND_MIN_RANGE, SHELL3D_SND_MAX_RANGE));
    m_fSndRndFreq = READ_IF_EXISTS(pSettings, r_fvector2, this->cNameSect(), "shell_hit_snd_rnd_freq", Fvector2().set(1.0f, 1.0f));
}

BOOL CCustomShell::net_Spawn(CSE_Abstract* DC)
{
    BOOL bRes = inherited::net_Spawn(DC);

    // Добавляем объект в список рендера HUD-а
    HUD().RenderHUD_Add(this);

    // Считываем индекс Launch Point-а, из которого мы вылетели, из имени объекта
    shared_str sLP_idx = DC->name_replace();
    R_ASSERT(sLP_idx != nullptr && sLP_idx != "");

    //m_launch_point_idx = atoi(sLP_idx.c_str());
    m_launch_point_idx = 1;
    R_ASSERT(m_launch_point_idx > 0);

    return bRes;
}

void CCustomShell::net_Destroy()
{
    // Удаляем объект из списка рендера HUD-а
    HUD().RenderHUD_Remove(this);


    CPHUpdateObject::Deactivate();
    inherited::net_Destroy();
}

// Вызывается из родительского NetSpawn, после вызова reload() там-же
void CCustomShell::reinit()
{
    inherited::reinit();
}

// Вызывается после присоединения гильзы к родителю
void CCustomShell::OnH_A_Chield()
{
    inherited::OnH_A_Chield();

    auto pLauncher = GetLauncher();

    m_dwRegisterTime = Device.dwTimeGlobal;

    //ShellDrop();

     if (pLauncher != nullptr)
    { //--> Владелец присутствует и это CShellLauncher
         Msg("Shell has been attached to parent!");
        
        const CShellLauncher::launch_points& pointsAll = ShellGetAllLaunchPoints();
        const CShellLauncher::_lpoint& pointCurr = ShellGetCurrentLaunchPoint();

        m_dwFOVTranslateTime = pointsAll.dwFOVTranslateTime;
        m_dwFOVStableTime = pointsAll.dwFOVStableTime;

        // Если гильза не анимированная - сразу её "выкидываем"
        if (!pointCurr.bAnimatedLaunch)

            //--> Делаем задержку в один кадр для пересчёта координат худа \ оружия
            m_dwDropOnFrameAfter = Device.dwFrame;

    }
    else //--> Владелец отсутствует или не CShellLauncher
        ShellDrop(); //--> Пробуем сразу выкинуть её
}

// Вызывается после отсоединения гильзы от родителя, CShellLauncher уже недоступен
void CCustomShell::OnH_A_Independent()
{
    Msg("Shell has been detached from parent!");
    inherited::OnH_A_Independent();

    if (!g_pGameLevel->bReady)
        return;

    setVisible(true);
    
    prepare_physic_shell();
}

// Вызывается на каждом кадре
void CCustomShell::UpdateCL()
{
    inherited::UpdateCL();

    UpdateShellHUDMode();
    UpdateShellAnimated();

    if (m_dwDropOnFrameAfter != 0 && m_dwDropOnFrameAfter != Device.dwFrame)
    {
        ShellDrop();
        m_dwDropOnFrameAfter = 0;
    }
    
    if (m_dwDestroyTime != 0 && m_dwDestroyTime < Device.dwTimeGlobal)
    {
        Msg("Shell obj has been destroyed!");
        if (Local())
            DestroyObject();
    }


    if (m_bHUD_mode)
    {
        ::Render->set_Transform(&XFORM());
        ::Render->add_Visual(Visual());
        this->setVisible(true);
    }

}

//================== Рендеринг ==================//

// Рендер мировой модели
void CCustomShell::renderable_Render()
{
    //Msg("!SHELL :renderable_Render() is activated!");
    if (m_bHUD_mode)
    {
        // Отрисовываем модель с динамическим HUD FOV
        extern ENGINE_API float psHUD_FOV;

        //--> Производим расчёт плавного изменения FOV объекта
        float fFactor = (m_dwFOVTranslateTime < 1 || (Device.dwTimeGlobal <= (m_dwRegisterTime + m_dwFOVStableTime)) ?
                             1.f :
                             (1.f - ((float)(Device.dwTimeGlobal - (m_dwRegisterTime + m_dwFOVStableTime)) / (float)m_dwFOVTranslateTime)));
        clamp(fFactor, 0.0f, 1.0f);

        // clang-format on

        // 1 - только заспавнился (HUD FOV), <= 0 - закончился таймер перехода (FOV)
        // float fHUD_FOV_delta = Device.fFOV - (psHUD_FOV * Device.fFOV);

        // float fHUD_FOV = Device.fFOV - (fHUD_FOV_delta * fFactor);

        // Msg("[%d]: fFactor = %f, fHUD_FOV = %f, Device.dwTimeGlobal = %d, m_dwRegisterTime = %d", ID(), fFactor,
        // fHUD_FOV, Device.dwTimeGlobal, m_dwRegisterTime);

        //--> Рисуем модель
        // Visual()->getObjectData()->m_hud_custom_fov = fHUD_FOV;
        // Visual()->getVisData().m_hud_custom_fov = fHUD_FOV;

        ::Render->set_Transform(&XFORM());
        ::Render->add_Visual(Visual());

        //--> После полного перехода в мировой FOV отключаем HUD-режим
        if (fFactor <= 0.0f)
        {
            m_bHUD_mode = false;
            // Visual()->getObjectData()->m_hud_custom_fov = -1.f;
            // Visual()->getVisData().m_hud_custom_fov = -1.f;
        }
    }

    //--> Просто отрисовываем модель
    inherited::renderable_Render();
}


// Рендер худовой модели
void CCustomShell::OnRenderHUD(CGameObject* pCurViewEntity)
{
    if (!m_bHUD_mode)// || Device.m_SecondViewport.IsSVPFrame()) // +SecondVP+
        return;

    Msg("!SHELL OnRenderHUD is activated!");

    // Отрисовываем модель с динамическим HUD FOV
    extern ENGINE_API float psHUD_FOV;

    //--> Производим расчёт плавного изменения FOV объекта
    float fFactor = (m_dwFOVTranslateTime < 1 || (Device.dwTimeGlobal <= (m_dwRegisterTime + m_dwFOVStableTime)) ?
          1.f :
          (1.f -  ((float)(Device.dwTimeGlobal - (m_dwRegisterTime + m_dwFOVStableTime)) / (float)m_dwFOVTranslateTime)));
    clamp(fFactor, 0.0f, 1.0f);

    // clang-format on

    // 1 - только заспавнился (HUD FOV), <= 0 - закончился таймер перехода (FOV)
   //float fHUD_FOV_delta = Device.fFOV - (psHUD_FOV * Device.fFOV);

    //float fHUD_FOV = Device.fFOV - (fHUD_FOV_delta * fFactor);

    // Msg("[%d]: fFactor = %f, fHUD_FOV = %f, Device.dwTimeGlobal = %d, m_dwRegisterTime = %d", ID(), fFactor,
     //fHUD_FOV, Device.dwTimeGlobal, m_dwRegisterTime);

    //--> Рисуем модель
    //Visual()->getObjectData()->m_hud_custom_fov = fHUD_FOV;
    //Visual()->getVisData().m_hud_custom_fov = fHUD_FOV;

    ::Render->set_Transform(&XFORM());
    ::Render->add_Visual(Visual());


    //--> После полного перехода в мировой FOV отключаем HUD-режим
    if (fFactor <= 0.0f)
    {
        m_bHUD_mode = false;
        //Visual()->getObjectData()->m_hud_custom_fov = -1.f;
        //Visual()->getVisData().m_hud_custom_fov = -1.f;
    }
}

//================== Физика ==================//

// Подготовка физической оболочки сразу после выпадания гильзы из оружия
void CCustomShell::prepare_physic_shell()
{
    CPhysicsShellHolder& obj = *this;
    VERIFY(obj.Visual());

    IKinematics* K = obj.Visual()->dcast_PKinematics();
    VERIFY(K);

    if (!obj.PPhysicsShell())
    {
        Msg("! ERROR: PhysicsShell is NULL, object [%s][%d]", obj.cName().c_str(), obj.ID());
        return;
    }

    if (!obj.PPhysicsShell()->isFullActive())
    {
        K->CalculateBones_Invalidate();
        K->CalculateBones(TRUE);
    }
    obj.PPhysicsShell()->GetGlobalTransformDynamic(&obj.XFORM());

    K->CalculateBones_Invalidate();
    K->CalculateBones(TRUE);

    obj.spatial_move();
}

// Инициализация физ. оболочки (NetSpawn)
void CCustomShell::setup_physic_shell()
{
    R_ASSERT(!m_pPhysicsShell);

    create_physic_shell();
    m_pPhysicsShell->Activate(XFORM(), 0, XFORM());

    IKinematics* pKinematics = smart_cast<IKinematics*>(Visual());
    R_ASSERT(pKinematics);

    pKinematics->CalculateBones_Invalidate();
    pKinematics->CalculateBones(TRUE);

    inherited::setup_physic_shell();
}

// Настройка и активация физ. оболочки гильзы (OnH_B_Independent)
void CCustomShell::activate_physic_shell()
{
    
    Fvector l_vel, a_vel;
    Fmatrix launch_matrix;
    launch_matrix.identity();

    CShellLauncher* pLauncher = GetLauncher();

    // Если в момент активации у нас есть владелец CShellLauncher - считываем параметры "полёта" гильзы
    if (pLauncher != nullptr)
    {
        Msg("activate_physic_shell() func has been called!");
        const CShellLauncher::_lpoint& point = ShellGetCurrentLaunchPoint();

        //--> Начальная матрица трнсформации гильзы (позиция + поворот)
        launch_matrix = point.GetLaunchMatrix();

        //--> Начальная линейная скорость (и вектор полёта)
        l_vel = point.GetLaunchVel();

        //--> Начальная угловая скорость (кручение гильзы)
        float fi, teta, r;
        fi = ::Random.randF(0.f, 2.f * M_PI);
        teta = ::Random.randF(0.f, M_PI);
        r = ::Random.randF(2.f * M_PI, 3.f * M_PI);
        float rxy = r * _sin(teta);
        a_vel.set(rxy * _cos(fi), rxy * _sin(fi), r * _cos(teta));
        a_vel.mul(point.GetLaunchAVel());

        //--> Скорость от перемещения владельца гильзы
        u16 iLockGuard = 250;
        CGameObject* pParentRoot = pLauncher->GetParentObject();
        while (iLockGuard > 0 && pParentRoot->H_Parent() != nullptr)
        { //--> Находим исходного владельца гильзы, путём перебора всех возможных
            pParentRoot = smart_cast<CGameObject*>(pParentRoot->H_Parent());
            iLockGuard--; //--> На случаи если что то пойдёт не так - мы не должны зависнуть
        }
        VERIFY(iLockGuard > 0);

        float fParentSpeedMod = point.fParentVelFactor;

       // auto entity_alive = smart_cast<CEntityAlive*>(pParentRoot);

        /* if (entity_alive && entity_alive->character_physics_support())
        {
            Fvector parent_vel = {0.0f, 1.0f, 0.0f};

            // TODO:

            //entity_alive->character_physics_support()->movement()->GetCharacterVelocity(parent_vel);

            l_vel.add(parent_vel.mul(fParentSpeedMod));
        }
        else
        {*/
            if (pParentRoot != this && pParentRoot->cast_physics_shell_holder() != nullptr)
            {
                Fvector vParentLVel;
                pParentRoot->cast_physics_shell_holder()->PHGetLinearVell(vParentLVel);
                l_vel.add(vParentLVel.mul(fParentSpeedMod));
            }
        //}
    }

    R_ASSERT(!m_pPhysicsShell);
    create_physic_shell();

    R_ASSERT(m_pPhysicsShell);
    if (m_pPhysicsShell->isActive())
        return;

    m_pPhysicsShell->Activate(launch_matrix, l_vel, a_vel);
    m_pPhysicsShell->Update();

    XFORM().set(m_pPhysicsShell->mXFORM);
    Position().set(m_pPhysicsShell->mXFORM.c);

    m_pPhysicsShell->set_PhysicsRefObject(this);
    m_pPhysicsShell->set_ObjectContactCallback(ObjectContactCallback);
    m_pPhysicsShell->set_ContactCallback(nullptr);

    m_pPhysicsShell->SetAirResistance(0.f, 0.f);
    m_pPhysicsShell->set_DynamicScales(1.f, 1.f);

    m_pPhysicsShell->SetAllGeomTraced();
    m_pPhysicsShell->DisableCharacterCollision();

    // m_pPhysicsShell->SetIgnoreDynamic();
    // m_pPhysicsShell->SetIgnoreStatic();

    inherited::activate_physic_shell();
}

// Контакт гильзы с геометрией
void CCustomShell::ObjectContactCallback(
    bool& do_colide, bool bo1, dContact& c, SGameMtl* material_1, SGameMtl* material_2)
{

    if (!do_colide)
        return;

    dxGeomUserData* pUD1 = nullptr;
    dxGeomUserData* pUD2 = nullptr;
    pUD1 = retrieveGeomUserData(c.geom.g1);
    pUD2 = retrieveGeomUserData(c.geom.g2);

    // Получаем контактный материал и объект гильзы
    SGameMtl* pMaterial = 0;
    CCustomShell* pThisShell = pUD1 ? smart_cast<CCustomShell*>(pUD1->ph_ref_object) : nullptr;
    if (!pThisShell)
    {
        pThisShell = pUD2 ? smart_cast<CCustomShell*>(pUD2->ph_ref_object) : nullptr;
        pMaterial = material_1;
    }
    else
    {
        pMaterial = material_2;
    }
    VERIFY(pMaterial);

    if (!pThisShell)
        return;

    // Игнорируем неконтактные материалы
    if (pMaterial->Flags.is(SGameMtl::flPassable))
        return;

    // Проверяем необходимость коллизии с объектом
    do_colide = (pThisShell->m_bItWasHudShell ? !pThisShell->m_bIgnoreGeometry_hud : !pThisShell->m_bIgnoreGeometry_3p);

    CGameObject* pObjCollidedWith = pUD1 ? smart_cast<CGameObject*>(pUD1->ph_ref_object) : nullptr;
    if (!pObjCollidedWith || pObjCollidedWith == (CGameObject*)pThisShell)
    {
        pObjCollidedWith = pUD2 ? smart_cast<CGameObject*>(pUD2->ph_ref_object) : nullptr;
        if (pObjCollidedWith == (CGameObject*)pThisShell)
        {
            pObjCollidedWith = nullptr;
        }
    }

    // Проверяем динамический объект, с которым столкнулись
    if (pObjCollidedWith != nullptr)
    {
        // Log("pObjCollidedWith =", pObjCollidedWith->cNameSect_str());
        if (pObjCollidedWith->cast_stalker() != nullptr || pObjCollidedWith->cast_actor() != nullptr)
        {
            // Не обрабатываем коллизию со сталкерами \ игроком
            return;
        }
    }

     //Msg("Contact with [%s] at [%d]", pMaterial->m_Name.c_str(), Device.dwTimeGlobal);

    // Проигрываем звук удара гильзы об землю
    if (!pThisShell->m_bSndWasPlayed)
    {
        //Msg("Shell has contact with ground/other surface!");
        //--> Находим случайный звук
        int iSndCnt = _GetItemCount(pThisShell->m_sShellHitSndList.c_str());
        if (iSndCnt > 0)
        {
            string256 sSndPath;
            _GetItem(pThisShell->m_sShellHitSndList.c_str(), Random.randI(0, iSndCnt), sSndPath);

            //--> Запускаем его
            float fRndFreq = Random.randF(pThisShell->m_fSndRndFreq.x, pThisShell->m_fSndRndFreq.y);
            pThisShell->m_pShellHitSnd.create(sSndPath, st_Effect, ESoundTypes(SOUND_TYPE_WEAPON_SHOOTING));
            ::Sound->play_no_feedback(pThisShell->m_pShellHitSnd, pThisShell->H_Parent(), 0, 0.0f,
                &pThisShell->Position(), &pThisShell->m_fSndVolume, &fRndFreq, &pThisShell->m_vSndRange);
        }

        //--> Играем только один раз
        pThisShell->m_bSndWasPlayed = true;
    }

    // Удаляем гильзы при коллизии
    if (pThisShell->m_dwDestroyOnCollideSafetime != u32(-1) &&
        pThisShell->m_dwDestroyOnCollideSafetime < Device.dwTimeGlobal)
    {
        pThisShell->m_dwDestroyTime = 1; //--> Отложенное удаление
        pThisShell->m_pPhysicsShell->set_ObjectContactCallback(nullptr);
        return;
    }
}

//============== Специфичный код для гильз ==============//

// Обновление анимации вылета гильзы
void CCustomShell::UpdateShellAnimated()
{
    // Если гильза уже отвязана от владельца - то не анимируем её
    if (ShellIsDropped())
        return;

    CShellLauncher* pLauncher = GetLauncher();

    // Владелец не может проигрывать анимацию по какой-либо причине
    if (!pLauncher->CanPlay3DShellAnim())
    {
        ShellDrop();
        return;
    }

    // Обновляем позицию анимированной гильзы
    const CShellLauncher::_lpoint& pointCurr = ShellGetCurrentLaunchPoint();
    bool bAnimated = pointCurr.bAnimatedLaunch;

    if (bAnimated)
    {
        pLauncher->Update3DShellTransform();

        //--> Пересчитываем стартовую позицию вылет
        Fmatrix launch_matrix;
        launch_matrix.identity();
        launch_matrix = pointCurr.GetLaunchMatrix();

        XFORM().set(launch_matrix);

        setVisible(true);

        //--> По окончанию таймера запускаем гильзу в свободный полёт
        //CGameObject* pGameObj = pLauncher->GetParentObject();
       // CWeapon* pWpnItem = pGameObj->cast_weapon();
        u32 iAnimReleaseTime = pointCurr.dwAnimReleaseTime;
            //TODO:
            //(pWpnItem == nullptr || pWpnItem->CanUsePumpMode() == false ? pointCurr.dwAnimReleaseTime :
            //                                                              pointCurr.dwAnimReleaseTimePump);
        if (Device.dwTimeGlobal >= m_dwRegisterTime + iAnimReleaseTime)
            ShellDrop();
    }
}

// Обновление текущего режима гильзы (Мировая \ Худовая)
void CCustomShell::UpdateShellHUDMode()
{
    if (dwHUDMode_upd_frame == Device.dwFrame)
        return; //--> Мы уже обновляли флаг на текущем кадре

    CShellLauncher* pLauncher = GetLauncher();
    if (pLauncher == nullptr)
        return; //--> У нас нет владельца, запоминаем последнее значение

    //CGameObject* pGameObj = pLauncher->GetParentObject();
    auto pHudItem = smart_cast<CHudItem*>(pLauncher->GetParentObject());

    m_bHUD_mode = pHudItem ? pHudItem->GetHUDmode() : false;

    dwHUDMode_upd_frame = Device.dwFrame;
}

// Получить владельца гильзы, возможно лишь до запуска гильзы в свободный полёт
CShellLauncher* CCustomShell::GetLauncher()
{
    //CGameObject* pParent = (H_Parent() != nullptr ? H_Parent()->cast_game_object() : nullptr);
    auto pParent = (H_Parent() != nullptr ? smart_cast<CGameObject*>(H_Parent()) : nullptr);
    if (pParent)
    {
        auto pLauncher = smart_cast<CShellLauncher*>(pParent); //->cast_shell_launcher();
        R_ASSERT(pLauncher != nullptr, "Shell parent is not CShellLauncher", this->Name(), pParent->Name());

        return pLauncher;
    }

    return nullptr;
}

// Запустить гильзу в свободный полёт
void CCustomShell::ShellDrop()
{
    Msg("!void CCustomShell::ShellDrop() has been called!");
    if (ShellIsDropped())
        return;

    UpdateShellHUDMode();

    //const CShellLauncher::launch_points& pointsAll = ShellGetAllLaunchPoints();
    const CShellLauncher::_lpoint& pointCurr = ShellGetCurrentLaunchPoint();
  
    // Рассчитываем время уничтожения гильзы
    m_dwDestroyTime = (pointCurr.dwLifeTime == 0 ? 0 : Device.dwTimeGlobal + pointCurr.dwLifeTime);

    // Рассчитываем время разблокировки уничтожения при столкновении
    if (pointCurr.bDestroyOnCollide)
        m_dwDestroyOnCollideSafetime = Device.dwTimeGlobal + pointCurr.dwMinCollideLifeTime;

    // Прочее
    m_bItWasHudShell = m_bHUD_mode;

    H_SetParent(nullptr);
}

// Запущена-ли гильза в свободный полёт?
bool CCustomShell::ShellIsDropped() { return H_Parent() == nullptr; }

// Получить мировые и худовые точки запуска гильз
const CShellLauncher::launch_points& CCustomShell::ShellGetAllLaunchPoints()
{
    CShellLauncher* pLauncher = GetLauncher();
    //auto pWeapon = smart_cast<CWeapon*>(GetLauncher()->cast_parent())


    R_ASSERT2(pLauncher, "Can't get shell launch points, owner not found or shell already dropped");

    const u32 nLPCnt = pLauncher->m_launch_points.size();
    R_ASSERT2(m_launch_point_idx <= nLPCnt, "Wrong m_launch_point_idx");

    return pLauncher->m_launch_points[m_launch_point_idx - 1];
}

// Получить нужную в данный момент точку запуска гильзы (худовую \ мировую)
const CShellLauncher::_lpoint& CCustomShell::ShellGetCurrentLaunchPoint()
{
    UpdateShellHUDMode();

    const CShellLauncher::launch_points& points = ShellGetAllLaunchPoints();
    return (m_bHUD_mode ? points.point_hud : points.point_world);
}
