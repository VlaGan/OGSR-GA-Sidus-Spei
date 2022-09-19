
#include "stdafx.h"

#include "TactTorch.h"
//#include "PhysicsShell.h"

CTactTorch::CTactTorch() {}

CTactTorch::~CTactTorch() {}

BOOL CTactTorch::net_Spawn(CSE_Abstract* DC) { return (inherited::net_Spawn(DC)); }

void CTactTorch::Load(LPCSTR section) { inherited::Load(section); }

void CTactTorch::net_Destroy() { inherited::net_Destroy(); }

void CTactTorch::UpdateCL() { inherited::UpdateCL(); }

void CTactTorch::OnH_A_Chield() { inherited::OnH_A_Chield(); }

void CTactTorch::OnH_B_Independent(bool just_before_destroy) { inherited::OnH_B_Independent(just_before_destroy); }

void CTactTorch::renderable_Render() { inherited::renderable_Render(); }