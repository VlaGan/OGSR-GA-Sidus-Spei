
#include "stdafx.h"

#include "Laser.h"
//#include "PhysicsShell.h"

CLaser::CLaser() {}

CLaser::~CLaser() {}

BOOL CLaser::net_Spawn(CSE_Abstract* DC) { return (inherited::net_Spawn(DC)); }

void CLaser::Load(LPCSTR section) { inherited::Load(section); }

void CLaser::net_Destroy() { inherited::net_Destroy(); }

void CLaser::UpdateCL() { inherited::UpdateCL(); }

void CLaser::OnH_A_Chield() { inherited::OnH_A_Chield(); }

void CLaser::OnH_B_Independent(bool just_before_destroy) { inherited::OnH_B_Independent(just_before_destroy); }

void CLaser::renderable_Render() { inherited::renderable_Render(); }