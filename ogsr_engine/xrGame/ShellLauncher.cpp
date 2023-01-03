/********************************/
/***** Запускатель 3D-Гильз *****/ //--#SM+#--
/********************************/

#include "StdAfx.h"
//#include "ShellLauncher.h"
#include "CustomShell.h"
#include "HudItem.h"
#include "CustomShell.cpp"

CShellLauncher::CShellLauncher(CGameObject* parent)
{
    m_parent_shell_launcher = parent;

    m_params_sect = nullptr; // Не инициализировать здесь <!>
    m_params_hud_sect = nullptr;

    m_launch_points_count = 2;
}

CShellLauncher::~CShellLauncher() { delete_data(m_launch_points); }

CShellLauncher::_lpoint::_lpoint()
{
    sBoneName = nullptr;
    bUseBoneDir = false;

    vOfsPos.set(0, 0, 0);
    vOfsDir.set(0, 0, 0);
    vOfsDirRnd.set(0, 0, 0);
    vVelocity.set(0, 0, 0);
    vVelocityRnd.set(0, 0, 0);

    fLaunchAVel = 0.0f;
    vLaunchVel.set(0, 0, 0);
    vLaunchMatrix.identity();

    iBoneID = BI_NONE;
    bEnabled = false;
}

CShellLauncher::_lpoint::_lpoint(const shared_str& sect_data, u32 _idx) : _lpoint()
{
    string64 sLine;

    xr_sprintf(sLine, "shells_3d_use_bone_dir_%d", _idx);
    bUseBoneDir = READ_IF_EXISTS(pSettings, r_bool, sect_data, sLine, false);

    xr_sprintf(sLine, "shells_3d_bone_name_%d", _idx);
    sBoneName = READ_IF_EXISTS(pSettings, r_string, sect_data, sLine, "wpn_body");

    xr_sprintf(sLine, "shells_3d_animated_launch_%d", _idx);
    bAnimatedLaunch = READ_IF_EXISTS(pSettings, r_bool, sect_data, sLine, false);

    xr_sprintf(sLine, "shells_3d_anim_drop_time_%d", _idx);
    dwAnimReleaseTime = READ_IF_EXISTS(pSettings, r_u32, sect_data, sLine, 0);

    xr_sprintf(sLine, "shells_3d_anim_drop_time_pump_%d", _idx);
    dwAnimReleaseTimePump = READ_IF_EXISTS(pSettings, r_u32, sect_data, sLine, dwAnimReleaseTime);

    xr_sprintf(sLine, "shells_3d_pvel_factor_%d", _idx);
    fParentVelFactor = READ_IF_EXISTS(pSettings, r_float, sect_data, sLine, SHELL3D_PARENT_DEF_SPEED_FACTOR);

    xr_sprintf(sLine, "shells_3d_pos_%d", _idx);
    vOfsPos = READ_IF_EXISTS(pSettings, r_fvector3, sect_data, sLine, vOfsPos);

    xr_sprintf(sLine, "shells_3d_dir_%d", _idx);
    vOfsDir = READ_IF_EXISTS(pSettings, r_fvector3, sect_data, sLine, vOfsDir);

    xr_sprintf(sLine, "shells_3d_dir_disp_%d", _idx);
    vOfsDirRnd = READ_IF_EXISTS(pSettings, r_fvector3, sect_data, sLine, vOfsDirRnd);

    xr_sprintf(sLine, "shells_3d_vel_%d", _idx);
    vVelocity = READ_IF_EXISTS(pSettings, r_fvector3, sect_data, sLine, vVelocity);

    xr_sprintf(sLine, "shells_3d_vel_disp_%d", _idx);
    vVelocityRnd = READ_IF_EXISTS(pSettings, r_fvector3, sect_data, sLine, vVelocityRnd);

    xr_sprintf(sLine, "shells_3d_avel_%d", _idx);
    fLaunchAVel = READ_IF_EXISTS(pSettings, r_float, sect_data, sLine, fLaunchAVel);

    xr_sprintf(sLine, "shells_3d_lifetime_%d", _idx);
    dwLifeTime = READ_IF_EXISTS(pSettings, r_u32, sect_data, sLine, SHELL3D_DEF_LIFETIME);

    xr_sprintf(sLine, "shells_3d_destroy_on_collide_%d", _idx);
    bDestroyOnCollide = READ_IF_EXISTS(pSettings, r_bool, sect_data, sLine, false);
    xr_sprintf(sLine, "shells_3d_collide_safetime_%d", _idx);
    dwMinCollideLifeTime = READ_IF_EXISTS(pSettings, r_u32, sect_data, sLine, SHELL3D_DEF_COL_LIFETIME);

    bEnabled = true;
}

CShellLauncher::launch_points::launch_points(const shared_str& sWorldSect, const shared_str& sHudSect, u32 _idx)
{
    R_ASSERT(pSettings != nullptr);

    // Грузим переопределённую секцию гильз (если есть)
    string64 sLine;
    xr_sprintf(sLine, "shells_3d_overridden_sect_%d", _idx);
    sShellOverSect = READ_IF_EXISTS(pSettings, r_string, sWorldSect, sLine, "none");
    if (sShellOverSect == "none")
        sShellOverSect = nullptr;

    // Параметры худового FOV
    xr_sprintf(sLine, "shells_3d_fov_transl_time_%d", _idx);
    dwFOVTranslateTime = READ_IF_EXISTS(pSettings, r_u32, sWorldSect, sLine, 0);

    xr_sprintf(sLine, "shells_3d_fov_stable_time_%d", _idx);
    dwFOVStableTime = READ_IF_EXISTS(pSettings, r_u32, sWorldSect, sLine, 0);

    // Грузим точки запуска для мировой и худовой модели
    point_world = _lpoint(sWorldSect, _idx);
    if (sHudSect != nullptr)
        point_hud = _lpoint(sHudSect, _idx);
}

// ПереЗагрузить параметры точек запуска гильз
void CShellLauncher::ReLoadLaunchPoints(const shared_str& sWorldSect, const shared_str& sHudSect)
{
    // Считываем число точек для гильз

    // TRY TO LAUNCH
    m_launch_points_count = READ_IF_EXISTS(pSettings, r_u32, sWorldSect, "shells_3d_lp_count", 0);

    clamp(m_launch_points_count, u32(0), u32(-1));

    // Очищаем прошлые данные
    delete_data(m_launch_points);

    // Грузим новые для мировой и худовой модели (если есть)
    for (int _idx = 1; _idx <= m_launch_points_count; _idx++)
        m_launch_points.push_back(launch_points(sWorldSect, sHudSect, _idx));

    // Запоминаем новые секции
    m_params_sect = sWorldSect; //--> Секция предмета-гильзы
    m_params_hud_sect = sHudSect; //--> Секция HUD-а гильзы
}

// Запустить одну партию гильз
void CShellLauncher::LaunchShell3D(u32 launch_point_idx, LPCSTR sShellSect)
{
    // Индекс точки запуска должен быть задан верно
    R_ASSERT(launch_point_idx > 0 && launch_point_idx <= m_launch_points_count);

    // У нас должен быть родитель и загруженные данные
    R_ASSERT(m_parent_shell_launcher != nullptr);
    R_ASSERT(m_params_sect != nullptr);

    // Получаем точки запуска с данным индексом

    //const launch_points& lp = m_launch_points[launch_point_idx - 1];

    // Достаём из них секцию гильзы
    // !!!
   // R_ASSERT2(lp.sShellOverSect != nullptr || sShellSect != nullptr, "Shell section is not specified in a weapon\\ammo config");

   // LPCSTR shell_sect = (lp.sShellOverSect != nullptr ? lp.sShellOverSect.c_str() : sShellSect);
   // R_ASSERT(shell_sect != nullptr);

    // Спавним гильзу
    //get_LastSP() 
    auto pWeapon = smart_cast<CWeapon*>(m_parent_shell_launcher);
    auto pSO = Level().spawn_item(sShellSect, pWeapon ? pWeapon->get_LastSP() : m_parent_shell_launcher->Position(), u32(-1), m_parent_shell_launcher->ID(), true);
        

    //CSE_Temporary* l_tpTemporary = smart_cast<CSE_Temporary*>(pSO);
    //R_ASSERT(l_tpTemporary);

    // PS: Значит custom_data (m_ini_string) мы определяем в CSE_Abstract, зато её сохранение реализовываем лишь в
    // CSE_AlifeObject ._.
    // => мы не можем передать данные через spawn_ini() => играем грязно
    // client_data использовать в теории можно, но разработчиками это не предусмотренно
    // добавлять новое поле в CSE_Temporary ради одних гильз - расточительно

    // pSO->spawn_ini().w_u32("shell_data", "lp_idx", launch_point_idx);

    //string64 sLP_idx;
    //xr_sprintf(sLP_idx, "%d", launch_point_idx);
    //pSO->set_name_replace(sLP_idx);

    NET_Packet P;
    pSO->Spawn_Write(P, TRUE);
    Level().Send(P, net_flags(TRUE));
    F_entity_Destroy(pSO);
}


// Зарегестрировать заспавненную гильзу
void CShellLauncher::RegisterShell(u16 shell_id, CGameObject* parent_shell_launcher)
{
    // Регистратор гильзы должен быть нашим владельцем
    R_ASSERT(parent_shell_launcher == m_parent_shell_launcher);

    // Если это не гильза, то не регистрируем её
    CCustomShell* pShell = smart_cast<CCustomShell*>(Level().Objects.net_Find(shell_id));
    if (!pShell)
        return;

    // Выставляем у гильзы владельца - нас
    pShell->H_SetParent(m_parent_shell_launcher);
}

// Обновить параметры полёта гильз для точек вылета
void CShellLauncher::RebuildLaunchParams(const Fmatrix& mTransform, IKinematics* pModel, bool bIsHud)
{
    // У нас должен быть родитель, загруженные данные и визуал
    R_ASSERT(m_parent_shell_launcher != nullptr);
    //R_ASSERT(m_params_sect != nullptr);
    R_ASSERT(pModel != nullptr);

    // Если точек запуска гильз нет, то и считать нечего
    if (m_launch_points_count == 0)
        return;

    // Если у нас сменилась худовая секция, то требуется перезагрузить данные

    auto pHudItem = smart_cast<CHudItem*>(m_parent_shell_launcher);

   // CHudItem* pHudItem = m_parent_shell_launcher->cast_hud_item();

    if (pHudItem != nullptr && pHudItem->GetHUDmode())
        if (pHudItem->HudSection() != m_params_hud_sect)
            ReLoadLaunchPoints(m_params_sect, pHudItem->HudSection());

    // Обновляем все точки запуска
    R_ASSERT(m_launch_points.size() == m_launch_points_count);

    for (int i = 0; i < m_launch_points.size(); i++)
    {
        _lpoint& point = (bIsHud ? m_launch_points[i].point_hud : m_launch_points[i].point_world);

        // Если точка не активна - не обновляем её
        if (point.bEnabled == false)
            continue;

        // Если ещё нету BoneID, то находим его
        if (point.iBoneID == BI_NONE)
            point.iBoneID = pModel->LL_BoneID(point.sBoneName);

        R_ASSERT2(point.iBoneID != BI_NONE,
            make_string("Model from [%s] has no bone [%s]",
                (bIsHud ? m_params_hud_sect.c_str() : m_params_sect.c_str()), point.sBoneName.c_str())
                .c_str());

        // Матрица трансформации кости
        Fmatrix& mBoneTransform = pModel->LL_GetTransform(point.iBoneID);

        // Вектор поворота
        Fvector vRotate = Fvector(point.vOfsDir);
        if (!point.bAnimatedLaunch)
        { //--> Случайный поворот не делаем для анимированных гильз - они будут трястись:
            // поворот пересчитывается на апдейте прямо во время анимации
            vRotate.x += Random.randFs(point.vOfsDirRnd.x); // +\-
            vRotate.y += Random.randFs(point.vOfsDirRnd.y);
            vRotate.z += Random.randFs(point.vOfsDirRnd.z);
        }
        vRotate.mul(PI / 180.f); // Переводим углы в радианы

        // Строим для гильзы матрицу поворота и позиции
        //--> Поворот
        point.vLaunchMatrix.setXYZ(VPUSH(vRotate));

        //--> Позиция
        point.vLaunchMatrix.c.set(point.vOfsPos); //--> Смещение из конфига
        point.vLaunchMatrix.c.add(mBoneTransform.c); //--> Смещение от кости

        //--> Добавляем к ней поворот от кости
        if (point.bUseBoneDir)
        {
            point.vLaunchMatrix.c.sub(mBoneTransform.c); //--> Убираем смещение от кости
            point.vLaunchMatrix.mulA_43(mBoneTransform); //--> Рассчитываем новое смещение и поворот от кости
        }

        //--> Добавляем к ней поворот самого предмета
        point.vLaunchMatrix.mulA_43(mTransform);

        // Случайный вектор скорости полёта
        point.vLaunchVel.set(point.vVelocity);
        point.vLaunchVel.x += Random.randFs(point.vVelocityRnd.x); // +\-
        point.vLaunchVel.y += Random.randFs(point.vVelocityRnd.y);
        point.vLaunchVel.z += Random.randFs(point.vVelocityRnd.z);

        //--> Модифицируем его с помощью поворота камеры
        if (bIsHud && m_parent_shell_launcher->H_Parent() != nullptr)
        {
            //--> Скорость от кручения камеры (от 1-го лица)
            //CActor* pParentActor = m_parent_shell_launcher->H_Parent()->cast_actor();
            auto pParentActor = smart_cast<CActor*>(m_parent_shell_launcher->H_Parent());
            if (pParentActor != nullptr)
            {
                float fYMag = pParentActor->fFPCamYawMagnitude; //--> Y
                float fMagF = SHELL3D_CAM_MAGNITUDE_F;

                //--> Корректируем полёт гильзы от горизонтального движения камеры
                if (point.vVelocity.x > 0.0f && fYMag < 0.0f)
                { //--> Гильзы должны лететь вправо, и камера крутится вправо
                    point.vLaunchVel.x += (-fYMag * fMagF);
                }
                else if (point.vVelocity.x < 0.0f && fYMag > 0.0f)
                { //--> Гильзы должны лететь влево, и камера крутится влево
                    point.vLaunchVel.x += (-fYMag * fMagF);
                }
            }
        }

        //--> Модифицируем его с помощью предмета
        mTransform.transform_dir(point.vLaunchVel);
    }
}