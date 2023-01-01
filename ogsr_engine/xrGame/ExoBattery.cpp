///////////////////////////////////////////////////////////////
// ExoBattery.cpp
// ExoBattery - батарейка для экзосклета
///////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "ExoBattery.h"

CExoBattery::CExoBattery() {}

CExoBattery::~CExoBattery() {}

BOOL CExoBattery::net_Spawn(CSE_Abstract* DC) { return (inherited::net_Spawn(DC)); }

void CExoBattery::Load(LPCSTR section) { inherited::Load(section); }

void CExoBattery::net_Destroy() { inherited::net_Destroy(); }

void CExoBattery::UpdateCL() { inherited::UpdateCL(); }

void CExoBattery::OnH_A_Chield() { inherited::OnH_A_Chield(); }

void CExoBattery::OnH_B_Independent(bool just_before_destroy) { inherited::OnH_B_Independent(just_before_destroy); }

void CExoBattery::renderable_Render() { inherited::renderable_Render(); }