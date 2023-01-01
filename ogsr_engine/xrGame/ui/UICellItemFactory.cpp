#include "stdafx.h"
#include "UICellItemFactory.h"
#include "UICellCustomItems.h"

CUICellItem* create_cell_item(CInventoryItem* itm)
{
    CWeaponAmmo* pAmmo = smart_cast<CWeaponAmmo*>(itm);
    if (pAmmo)
        return xr_new<CUIAmmoCellItem>(pAmmo);

    CWeapon* pWeapon = smart_cast<CWeapon*>(itm);
    if (pWeapon)
        return xr_new<CUIWeaponCellItem>(pWeapon);

    auto pExo = smart_cast<CCustomExeskeleton*> (itm);
    if (pExo)
        return xr_new<CUIExoOutfitCellItem>(pExo);


    return xr_new<CUIInventoryCellItem>(itm);
}
