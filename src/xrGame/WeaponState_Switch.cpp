/***********************************************************/
/***** Состояние "Переключение шутера\режима стрельбы" *****/ //--#SM+#--
/***********************************************************/

#include "stdafx.h"
#include "Weapon_Shared.h"

// Пробуем начать смену режима на клиенте
bool CWeapon::Try2Switch(bool bCheckOnlyMode)
{
    if (!bCheckOnlyMode && GetState() == eSwitch)
        return false;
    if (IsPending() == true)
        return false;

    if (m_bUseAmmoBeltMode)
        return false;

    if (!IsGrenadeLauncherAttached())
        return false;

    if (!bCheckOnlyMode)
        SwitchState(eSwitch);

    return true;
}

// Нужно остановить смену режима на клиенте
void CWeapon::Need2Stop_Switch()
{
    if (GetState() != eSwitch)
        return;
    Need2Idle();
}

// Переключение стэйта на "Смена режима"
void CWeapon::switch2_Switch()
{
    if (!Try2Switch(true)) //--> Повторная проверка для МП, где вызов стэйтов идёт в обход Try-функций
    {
        Need2Idle();
        return;
    }

    if (!SwitchMode())
    {
        Need2Idle();
        return;
    }

    return;
}

// Переключение на другой стэйт из стэйта "Смена режима"
void CWeapon::switchFrom_Switch(u32 newS) {}

// Обновление оружия в состоянии "Смена режима"
void CWeapon::state_Switch(float dt) {}

////////////////////////////////////////////////////////////////////
// ************************************************************** //
////////////////////////////////////////////////////////////////////

// Переключение на подствол и обратно
bool CWeapon::SwitchMode()
{
    if (GetState() == eSwitch || Try2Switch(true))
    {
        OnZoomOut();
        SetPending(TRUE);

        PerformSwitchGL();

        PlayAnimModeSwitch();

        m_BriefInfo_CalcFrame = 0;

        return true;
    }

    return false;
}