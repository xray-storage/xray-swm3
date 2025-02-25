/***************************************/
/***** Состояние "Оружие спрятано" *****/ //--#SM+#--
/***************************************/

#include "stdafx.h"
#include "Weapon_Shared.h"

// Переключение стэйта на "Спрятано"
void CWeapon::switch2_Hidden()
{
    StopCurrentAnimWithoutCallback();
    StopAllEffects();
    signal_HideComplete();
    m_nearwall_last_hud_fov = psHUD_FOV_def;
}

// Переключение на другой стэйт из стэйта "Спрятано"
void CWeapon::switchFrom_Hidden(u32 newS) {}

// Обновление оружия в состоянии "Спрятано"
void CWeapon::state_Hidden(float dt) {}

////////////////////////////////////////////////////////////////////
// ************************************************************** //
////////////////////////////////////////////////////////////////////

// Вызывается либо сразу после скрытия, либо всегда когда оружие скрыто
void CWeapon::SendHiddenItem()
{
    if (!CHudItem::object().getDestroy() && m_pInventory)
    {
        // !!! Just single entry for given state !!!
        NET_Packet P;
        CHudItem::object().u_EventGen(P, GE_WPN_STATE_CHANGE, CHudItem::object().ID());
        P.w_u8(u8(eHiding));
        P.w_u8(u8(m_sub_state));
        P.w_u8(m_ammoType);
        P.w_u8(u8(iAmmoElapsed & 0xff));
        P.w_u8(m_set_next_ammoType_on_reload);
        P.w_u8(m_set_next_magaz_on_reload);
        P.w_u16(m_set_next_magaz_by_id);
        CHudItem::object().u_EventSend(P, net_flags(TRUE, TRUE, FALSE, TRUE));
        SetPending(TRUE);
    }
}