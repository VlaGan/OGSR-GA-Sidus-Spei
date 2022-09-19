#include "stdafx.h"
#include "WeaponFORT.h"

CWeaponFORT::CWeaponFORT() : CWeaponPistol("FORT") {}

CWeaponFORT::~CWeaponFORT() {}

using namespace luabind;

#pragma optimize("s", on)
void CWeaponFORT::script_register(lua_State* L) { module(L)[class_<CWeaponFORT, CGameObject>("CWeaponFORT").def(constructor<>())]; }
