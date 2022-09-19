///////////////////////////////////////////////////////////////
// Bayonet.cpp
// Bayonet - штык-нож
///////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "Bayonet.h"
//#include "PhysicsShell.h"

CBayonet::CBayonet() {}

CBayonet::~CBayonet() {}

BOOL CBayonet::net_Spawn(CSE_Abstract* DC) { return (inherited::net_Spawn(DC)); }

void CBayonet::Load(LPCSTR section) { inherited::Load(section); }

void CBayonet::net_Destroy() { inherited::net_Destroy(); }

void CBayonet::UpdateCL() { inherited::UpdateCL(); }

void CBayonet::OnH_A_Chield() { inherited::OnH_A_Chield(); }

void CBayonet::OnH_B_Independent(bool just_before_destroy) { inherited::OnH_B_Independent(just_before_destroy); }

void CBayonet::renderable_Render() { inherited::renderable_Render(); }