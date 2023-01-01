#pragma once

#include "customoutfit.h"
#include "xrserver_objects_alife_items.h"

class CCustomExeskeleton : public CCustomOutfit
{
private:
    typedef CCustomOutfit inherited;

    shared_str m_sBatterySectionName, m_sBatterySectionNameCurrent;
    float m_fBatteryDischargeSpeed{}, m_fBatteryCurrentLevel{};


    //-- VlaGan: Такс, непосредственно аттачи
    u16 m_fExoflagsAddOnState;


    int m_iBatteryX{}, m_iBatteryY{};


public:
    virtual void Load(LPCSTR section);
    virtual void save(NET_Packet& output_packet);
    virtual void load(IReader& input_packet);

    
    BOOL net_Spawn(CSE_Abstract* DC);
    void net_Export(CSE_Abstract* E);

    //-- VlaGan: Такс, непосредственно аттачи
    bool CanAttach(PIItem pIItem);
    bool CanDetach(const char* item_section_name);
    bool Attach(PIItem pIItem, bool b_send_event);
    bool Detach(const char* item_section_name, bool b_spawn_item);

    //-- VlaGan: это для UI иконки
    const shared_str& GetBatteryName() const { return m_sBatterySectionName; }
    int GetBatteryX() { return m_iBatteryX; }
    int GetBatteryY() { return m_iBatteryY; }
    bool IsBatteryAttached() {return 0 != (m_fExoflagsAddOnState & CSE_ALifeItemExoskeleton::eExoBatteryAttached); }
   

    u16 GetAddonsState() const { return m_fExoflagsAddOnState; };
    void SetAddonsState(u16 st) { m_fExoflagsAddOnState = st; }


    void UpdateCL();
};


