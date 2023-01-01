#include "stdafx.h"

#include "customexooutfit.h"
#include "ExoBattery.h"
#include "inventory_space.h"
#include "Inventory.h"
#include "Actor.h"
#include "game_cl_base.h"
#include "Level.h"



void CCustomExeskeleton::net_Export(CSE_Abstract* E)
{
    inherited::net_Export(E);

    // CSE_ALifeInventoryItem* itm = smart_cast<CSE_ALifeInventoryItem*>(E);
    smart_cast<CSE_ALifeInventoryItem*>(E)->m_fCondition = m_fCondition;

    auto exooutf = smart_cast<CSE_ALifeItemExoskeleton*>(E);
    exooutf->m_exo_addon_flags.flags = m_fExoflagsAddOnState;
}
BOOL CCustomExeskeleton::net_Spawn(CSE_Abstract* DC)
{
    auto E = smart_cast<CSE_ALifeItemExoskeleton*>(DC);
    m_fExoflagsAddOnState = E->m_exo_addon_flags.get();

    return inherited::net_Spawn(DC);
}

void CCustomExeskeleton::Load(LPCSTR section)
{
    inherited::Load(section);

    m_sBatterySectionName = READ_IF_EXISTS(pSettings, r_string, section, "battery_name", nullptr);
    m_fBatteryDischargeSpeed = READ_IF_EXISTS(pSettings, r_float, section, "battery_discharge_speed", 0.00005f);
    m_iBatteryX = READ_IF_EXISTS(pSettings, r_float, section, "battery_x", 0);
    m_iBatteryY = READ_IF_EXISTS(pSettings, r_float, section, "battery_y", 0);

}

void CCustomExeskeleton::save(NET_Packet& output_packet)
{
    inherited::save(output_packet);

    //save_data(m_sBatterySectionNameCurrent, output_packet);
    save_data(m_fExoflagsAddOnState, output_packet);
}

void CCustomExeskeleton::load(IReader& input_packet)
{
    inherited::load(input_packet);

    //load_data(m_sBatterySectionNameCurrent, input_packet);
    load_data(m_fExoflagsAddOnState, input_packet);
}

bool CCustomExeskeleton::CanAttach(PIItem pIItem)
{
    auto pBattery = smart_cast<CExoBattery*>(pIItem);

    if (pBattery && (m_fExoflagsAddOnState & CSE_ALifeItemExoskeleton::eExoBatteryAttached) == 0 && (m_sBatterySectionName == pIItem->object().cNameSect()))
        return true;

    else
        return inherited::CanAttach(pIItem);
}

bool CCustomExeskeleton::CanDetach(const char* item_section_name)
{
    if (0 != (m_fExoflagsAddOnState & CSE_ALifeItemExoskeleton::eExoBatteryAttached) && (m_sBatterySectionName == item_section_name))
        return true;

    else
        return inherited::CanDetach(item_section_name);
}

bool CCustomExeskeleton::Attach(PIItem pIItem, bool b_send_event)
{
    bool result = false;

    auto pBattery = smart_cast<CExoBattery*>(pIItem);

    if (pBattery && (m_fExoflagsAddOnState & CSE_ALifeItemExoskeleton::eExoBatteryAttached) == 0 && (m_sBatterySectionName == pIItem->object().cNameSect()))
    {
        m_fExoflagsAddOnState |= CSE_ALifeItemExoskeleton::eExoBatteryAttached;

        //-- VlaGan: Устанавливаем значение батареи экзача относительно её состоянию
        m_fBatteryCurrentLevel = pBattery->GetCondition();
        m_sBatterySectionNameCurrent = pIItem->object().cNameSect();

        result = true;
    }

    if (result)
    {
        if (b_send_event && OnServer())
        {
            //уничтожить подсоединенную вещь из инвентаря
            //.			pIItem->Drop();
            pIItem->object().DestroyObject();
        };

        return true;
    }
    else
        return inherited::Attach(pIItem, b_send_event);
}

//-- VlaGan: TODO: Придумать, как бы кондицию менять батарейке при детаче
bool CCustomExeskeleton::Detach(const char* item_section_name, bool b_spawn_item)
{
    if (0 != (m_fExoflagsAddOnState & CSE_ALifeItemExoskeleton::eExoBatteryAttached) && (m_sBatterySectionName == item_section_name))
    {
        m_fExoflagsAddOnState &= ~CSE_ALifeItemExoskeleton::eExoBatteryAttached;

        m_sBatterySectionNameCurrent = nullptr;

        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    }

    else
        return inherited::Detach(item_section_name, b_spawn_item);
}


void CCustomExeskeleton::UpdateCL() { 
    inherited::UpdateCL(); 

    if (IsBatteryAttached() && m_fBatteryCurrentLevel)
        m_fBatteryCurrentLevel -= m_fBatteryDischargeSpeed;
}