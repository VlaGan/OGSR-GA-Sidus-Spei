
#include "stdafx.h"

#include "TactHandler.h"
//#include "PhysicsShell.h"

CTactHandler::CTactHandler() {}

CTactHandler::~CTactHandler() {}

BOOL CTactHandler::net_Spawn(CSE_Abstract* DC) { return (inherited::net_Spawn(DC)); }

void CTactHandler::Load(LPCSTR section) { inherited::Load(section); }

void CTactHandler::net_Destroy() { inherited::net_Destroy(); }

void CTactHandler::UpdateCL() { inherited::UpdateCL(); }

void CTactHandler::OnH_A_Chield() { inherited::OnH_A_Chield(); }

void CTactHandler::OnH_B_Independent(bool just_before_destroy) { inherited::OnH_B_Independent(just_before_destroy); }

void CTactHandler::renderable_Render() { inherited::renderable_Render(); }