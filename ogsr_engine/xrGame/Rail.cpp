///////////////////////////////////////////////////////////////
// Silencer.cpp
// Silencer - апгрейд оружия глушитель
///////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "Rail.h"
//#include "PhysicsShell.h"

CRail::CRail() {}

CRail::~CRail() {}

BOOL CRail::net_Spawn(CSE_Abstract* DC) { return (inherited::net_Spawn(DC)); }

void CRail::Load(LPCSTR section) { inherited::Load(section); }

void CRail::net_Destroy() { inherited::net_Destroy(); }

void CRail::UpdateCL() { inherited::UpdateCL(); }

void CRail::OnH_A_Chield() { inherited::OnH_A_Chield(); }

void CRail::OnH_B_Independent(bool just_before_destroy) { inherited::OnH_B_Independent(just_before_destroy); }

void CRail::renderable_Render() { inherited::renderable_Render(); }