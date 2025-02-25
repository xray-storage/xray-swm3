/*******************************************/
/***** Различные функции для инвентаря *****/ //--#SM+#--
/*******************************************/

#include "stdafx.h"
#include "Weapon_Shared.h"

// Получить текущий вес оружия
float CWeapon::Weight() const
{
    float res = CInventoryItemObject::Weight();

    // Прибавляем вес аддонов
    if (IsGrenadeLauncherAttached())
    {
        res += pSettings->r_float(GetGrenadeLauncherName(), "inv_weight");
    }
    if (IsScopeAttached())
    {
        res += pSettings->r_float(GetScopeName(), "inv_weight");
    }
    if (IsSilencerAttached())
    {
        res += pSettings->r_float(GetSilencerName(), "inv_weight");
    }
    if (IsMagazineAttached())
    {
        res += pSettings->r_float(GetMagazineName(), "inv_weight");
    }
    if (IsSpecial_1_Attached())
    {
        res += pSettings->r_float(GetSpecial_1_Name(), "inv_weight");
    }
    if (IsSpecial_2_Attached())
    {
        res += pSettings->r_float(GetSpecial_2_Name(), "inv_weight");
    }
    if (IsSpecial_3_Attached())
    {
        res += pSettings->r_float(GetSpecial_3_Name(), "inv_weight");
    }
    if (IsSpecial_4_Attached())
    {
        res += pSettings->r_float(GetSpecial_4_Name(), "inv_weight");
    }

    // Прибавляем вес патронов
    if (iAmmoElapsed)
    {
        float w  = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "inv_weight");
        float bs = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "box_size");

        res += w * (iAmmoElapsed / bs);
    }

    return res;
}

// Получить текущую стоймость оружия
u32 CWeapon::Cost() const
{
    u32 res = CInventoryItem::Cost();

    // Прибавляем стоймость аддонов
    if (IsGrenadeLauncherAttached())
    {
        res += pSettings->r_u32(GetGrenadeLauncherName(), "cost");
    }
    if (IsScopeAttached())
    {
        res += pSettings->r_u32(GetScopeName(), "cost");
    }
    if (IsSilencerAttached())
    {
        res += pSettings->r_u32(GetSilencerName(), "cost");
    }
    if (IsMagazineAttached())
    {
        res += pSettings->r_u32(GetMagazineName(), "cost");
    }
    if (IsSpecial_1_Attached())
    {
        res += pSettings->r_u32(GetSpecial_1_Name(), "cost");
    }
    if (IsSpecial_2_Attached())
    {
        res += pSettings->r_u32(GetSpecial_2_Name(), "cost");
    }
    if (IsSpecial_3_Attached())
    {
        res += pSettings->r_u32(GetSpecial_3_Name(), "cost");
    }
    if (IsSpecial_4_Attached())
    {
        res += pSettings->r_u32(GetSpecial_4_Name(), "cost");
    }

    // Прибавляем стоймость патронов
    if (iAmmoElapsed)
    {
        float w  = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "cost");
        float bs = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "box_size");

        res += iFloor(w * (iAmmoElapsed / bs));
    }

    return res;
}

// Проверка является ли переданная секция связанной с данным оружием (имеет значение для него)
bool CWeapon::IsNecessaryItem(const shared_str& item_sect)
{
    return (std::find(m_ammoTypes.begin(), m_ammoTypes.end(), item_sect) != m_ammoTypes.end() ||
            std::find(m_ammoTypes2.begin(), m_ammoTypes2.end(), item_sect) != m_ammoTypes2.end());
}

// Получить необходимую информацию о предмете для UI
bool CWeapon::GetBriefInfo(II_BriefInfo& info)
{
    VERIFY(m_pInventory);

    if (m_bKnifeMode || m_bUIShowAmmo == false)
    {
        info.clear();
        info.name._set(m_nameShort);
        info.icon._set(cNameSect());
        return true;
    }

    string32 int_str;

    // Обновляем текущее число патронов в магазине
    int ae = GetAmmoElapsed();
    if (m_set_next_ammoType_on_reload != undefined_ammo_type)
        if (m_set_next_ammoType_on_reload != m_ammoType)
            ae = 0;

    xr_sprintf(int_str, "%d", ae);
    info.cur_ammo._set(int_str);

    // Обновляем текущий режим стрельбы
    if (HasFireModes())
    {
        if (m_iQueueSize == WEAPON_ININITE_QUEUE)
            info.fire_mode._set("A");
        else
        {
            xr_sprintf(int_str, "%d", m_iQueueSize);
            info.fire_mode._set(int_str);
        }
    }
    else
        info.fire_mode._set("");

    if (m_pInventory->ModifyFrame() <= m_BriefInfo_CalcFrame)
        return false;

    // Подсчитываем число патронов текущего типа и число патронов других типов в основном магазине
    if (m_bUseMagazines == false)
    {
        u32 at_size = m_bGrenadeMode ? m_ammoTypes2.size() : m_ammoTypes.size(); //--> Возможное кол-во типов патронов в основном магазине
        if (unlimited_ammo() || at_size == 0)
        {
            info.fmj_ammo._set("--");
            info.ap_ammo._set("--");
        }
        else
        {
            u8 ammo_type = m_bGrenadeMode ? m_ammoType2 : m_ammoType; //--> Получаем тип патронов в основном магазине
            if (m_set_next_ammoType_on_reload != undefined_ammo_type)
                ammo_type = m_set_next_ammoType_on_reload;

            int iTotalCurAmmo     = 0;
            int iTotalAviableAmmo = 0;

            for (u8 i = 0; i < at_size; i++)
            {
                int iTotalAmmo = m_bGrenadeMode ? GetAmmoCount2(i) : GetAmmoCount(i);
                iTotalAviableAmmo += iTotalAmmo;

                if (i == ammo_type)
                    iTotalCurAmmo = iTotalAmmo;
            }

            xr_sprintf(int_str, "%d", iTotalCurAmmo);
            info.fmj_ammo._set(int_str); //--> Число патронов текущего типа
            xr_sprintf(int_str, "%d", iTotalAviableAmmo - iTotalCurAmmo);
            info.ap_ammo._set(int_str); //--> Число патронов всех остальных типов
        }
    }
    else
    {
        // При магазинном питании подсчитываем число патронов во всех магазинах
        int iTotalAmmoInMagazines = 0;
        int iTotalMagazines       = 0;

        TIItemContainer::iterator itb = m_pInventory->m_ruck.begin();
        TIItemContainer::iterator ite = m_pInventory->m_ruck.end();
        for (; itb != ite; ++itb)
        {
            CWeapon* pWpn = smart_cast<CWeapon*>(*itb);
            if (pWpn && GetAddonSlot(pWpn) == eMagaz)
            {
                int iAmmo = pWpn->GetMainAmmoElapsed();
                iTotalAmmoInMagazines += iAmmo;
                if (iAmmo > 0)
                    iTotalMagazines++;
            }
        }

        xr_sprintf(int_str, "%d", iTotalAmmoInMagazines);
        info.fmj_ammo._set(int_str);
        xr_sprintf(int_str, "%d", iTotalMagazines);
        info.ap_ammo._set(int_str);
    }

    // Подсчитываем число доступных патронов в подствольнике (все типы)
    if (!IsGrenadeLauncherAttached())
    {
        info.grenade = "";
    }
    else
    {
        if (unlimited_ammo())
            xr_sprintf(int_str, "--");
        else
        {
            u32 at_size2 = m_bGrenadeMode ? m_ammoTypes.size() : m_ammoTypes2.size(); //--> Возможное кол-во типов патронов в подствольном магазине
            int iTotalAviableAmmo = 0;

            for (u8 i = 0; i < at_size2; i++)
                iTotalAviableAmmo += m_bGrenadeMode ? GetAmmoCount(i) : GetAmmoCount2(i);

            if (iTotalAviableAmmo > 0)
                xr_sprintf(int_str, "%d", iTotalAviableAmmo);
            else
                xr_sprintf(int_str, "X");
        }

        info.grenade = int_str;
    }

    // Устанавливаем иконку патронов текущего типа
    if (m_magazine.size() != 0 && m_set_next_ammoType_on_reload == undefined_ammo_type)
    {
        LPCSTR ammo_type = m_ammoTypes[m_magazine.back()->m_LocalAmmoType].c_str();
        info.name._set(CStringTable().translate(pSettings->r_string(ammo_type, "inv_name_short")));
        info.icon._set(ammo_type);
    }
    else
    {
        LPCSTR ammo_type = ((m_set_next_ammoType_on_reload == undefined_ammo_type) ? m_ammoTypes[m_ammoType].c_str() :
                                                                                     m_ammoTypes[m_set_next_ammoType_on_reload].c_str());
        info.name._set(CStringTable().translate(pSettings->r_string(ammo_type, "inv_name_short")));
        info.icon._set(ammo_type);
    }

    return true;
}

// Разрешена-ли мгновенная перезарядка из инвентаря
bool CWeapon::InventoryFastReloadAllowed(bool bForGL)
{
    if (bForGL == true)
        return false;

    return IsMagazine() && ParentIsActor() && (GetMainAmmoElapsed() < GetMainMagSize());
}

// Мгновенная перезарядка из инвентаря
void CWeapon::InventoryFastReload(u8 ammoType, bool bForGL)
{
    if (InventoryFastReloadAllowed(bForGL) == false)
        return;

    if (bForGL)
        ReloadGLMagazineWithType(ammoType);
    else
        ReloadMainMagazineWithType(ammoType);
}
