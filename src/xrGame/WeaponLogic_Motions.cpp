/***********************************/
/***** Анимации и звуки оружия *****/ //--#SM+#--
/***********************************/

#include "stdafx.h"
#include "Weapon_Shared.h"

// Колбэк на конец анимации
void CWeapon::OnAnimationEnd(u32 state)
{
    // Сбрасываем флаг анимации зума
    bool bIsFromZoom = (m_ZoomAnimState != eZANone);
    m_ZoomAnimState  = eZANone;

    // В режиме ножа стрельбу обрабатываем отдельно
    if (m_bKnifeMode == true)
    {
        switch (state)
        {
        case eFire:
        case eFire2: Need2Idle(); return;
        }
    }

    // Смена магазина
    if (state == eSwitchMag)
    {
        Need2SwitchMag();
        return;
    }

    // Перезарядку в три стадии обрабатываем отдельно
    if (IsTriStateReload() && state == eReload)
    {
        switch (m_sub_state)
        {
        case eSubstateReloadBegin:
        {
            if (m_bNeed2StopTriStateReload)
            {
                Need2Idle();
                return;
            }
            m_sub_state = eSubstateReloadInProcess;
            Need2Reload();
        }
        break;

        case eSubstateReloadInProcess:
        {
            if (m_bSwitchAddAnimation == false)
            {
                u8 cnt = (m_bIsReloadFromAB ? AddCartridgeFrAB(1) : AddCartridge(1));
                if (m_bNeed2StopTriStateReload || (0 != cnt))
                    m_sub_state = eSubstateReloadEnd;
            }
            Need2Reload();
        }
        break;

        case eSubstateReloadEnd:
        {
            m_sub_state = eSubstateReloadBegin;
            Need2Stop_Reload();
        }
        break;
        };
        return;
    }

    // Обрабатываем всё остальное
    switch (state)
    {
    case eReload: // End of reload animation
    {
        if (!m_bIsReloadFromAB)
            ReloadMagazine();
        else
            ReloadMagazineFrAB();
        Need2Stop_Reload();
        break;
    }
    case eSwitch:
        Need2Idle();
        break; // Switch Main\GL
    case eHiding:
        SwitchState(eHidden);
        break; // End of Hide
    case eShowing:
        Need2Idle();
        break; // End of Show
    case ePump:
        m_bNeed2Pump = false;
        Need2Idle();
        break; // End of Pump
    case eIdle:
        if (GetNextState() == eIdle)
            Need2Idle();
        break; // Keep showing idle
    case eKick:
        Need2Stop_Kick();
        break; // Stop kicking
    case eFire:
        Try2Pump();
        break; // Pump
    default: inherited::OnAnimationEnd(state);
    }

    // Для фикса ситуации когда мы стреляем и сразу целимся (idle-анимация не запускается)
    if (bIsFromZoom && state == eFire)
        Need2Idle();
}

// Колбэк на метки в анимация
void CWeapon::OnMotionMark(u32 state, const motion_marks& M)
{
    CWeaponKnifeHit* hitObj = NULL;

    switch (state)
    {
    // Смена магазина
    case eSwitchMag:
        if (m_sub_state != eSubstateMagazFinish)
        {
            // Остановим звук перезарядки
            m_sounds.StopSound("sndReload");
            m_sounds.StopSound("sndReloadEmpty");
            m_sounds.StopSound("sndReloadWGL");
            m_sounds.StopSound("sndReloadEmptyWGL");

            m_sounds.StopAllSoundsWhichContain("reload");

            // Передадим управление в стэйт SwitchMag
            Need2SwitchMag();
        }
        break;
    // Перезарядка помпы (вылет гильзы)
    case ePump:
        m_bNeed2Pump = false;
        DropShell();
        break;
    // Удар прикладом (момент удара)
    case eKick:
        KickHit(false);
        break;
    // Удары ножом
    case eFire: //--> Основной
        hitObj = m_first_attack;
        break;
    case eFire2: //--> Альтернативный
        hitObj = m_second_attack;
        break;
    default: inherited::OnMotionMark(state, M);
    }

    if (m_bKnifeMode == true && hitObj != NULL)
    {
        Fvector p1, d;
        p1.set(get_LastFP());
        d.set(get_LastFD());

        if (H_Parent())
        {
            smart_cast<CEntity*>(H_Parent())->g_fireParams(this, p1, d);
            hitObj->KnifeStrike(p1, d);
            PlaySound("sndKnife", p1);
        }
    }
}

// Вызывается перед проигрыванием любой анимации.
// Возвращает необходимость проиграть анимацию не с начала, а с первой метки внутри неё.
bool CWeapon::OnBeforeMotionPlayed(const shared_str& sMotionName)
{
    // Для MP анимация прятанья и доставания в два раза быстрее (TODO: Ускорение не работает (или частично [?]) с анимациями доставания <?>)
    if (!IsGameTypeSingle() && (GetState() == eHiding || GetState() == eShowing))
        g_player_hud->SetAnimSpeedMod(2.f);

    // Для анимации зума управляем её временем начала и направлением
    if (m_ZoomAnimState != eZANone)
    {
        // Время старта анимации зависит от того сколько оружие уже успело повернуться
        g_player_hud->SetAnimStartTime(GetZRotatingFactor() * -1); // -1 для указания времени старта в процентах от длины анимации

        if (m_ZoomAnimState == eZAOut) // Мы выходим из зума - пускаем анимацию реверсивно
            g_player_hud->SetAnimSpeedMod(-1.0f);
    }

    // Управляем анимациями смены магазинов для оружия с магазинным питанием
    if (GetState() == eSwitchMag && m_sub_state == eSubstateMagazFinish)
    {
        // Обрабатываем осечку
        if (IsMisfire())
        {
            bMisfire = false;
            return false; //--> При осечке анимация перезарядки должна отыграться целиком
        }

        // Определяем время старта анимации
        if (g_player_hud != NULL)
        {
            SAddonData* pAddonMagaz = GetAddonBySlot(eMagaz);
            if (pAddonMagaz->bActive)
            {
                if (pSettings->line_exist(pAddonMagaz->GetName(), "insert_anim_start_time"))
                {
                    m_fLastAnimStartTime = pSettings->r_float(pAddonMagaz->GetName(), "insert_anim_start_time");
                    g_player_hud->SetAnimStartTime(m_fLastAnimStartTime);
                }

                return READ_IF_EXISTS(pSettings, r_bool, pAddonMagaz->GetName(), "insert_anim_start_time_from_anim", false);
            }
            else
                return true; //--> Установленного магазина нет, значит мы его только что сняли -> проигрываем анимацию с метки
        }
    }

    return inherited::OnBeforeMotionPlayed(sMotionName);
}

// Проиграть анимацию со звуком (а также учитывая число патронов в основном магазине)
bool CWeapon::PlaySoundMotion(const shared_str& M, BOOL bMixIn, LPCSTR alias, bool bAssert, int anim_idx)
{
    bool bFound = false;

    // Сперва ищем анимацию с привязкой к кол-ву патронов в основном стволе
    int idx;
    if (anim_idx != -1)
        idx = anim_idx;
    else
        idx = GetMainAmmoElapsed();

    string256 sAnm;
    xr_sprintf(sAnm, "%s_%d", M.c_str(), idx);

    if (pSettings->line_exist(hud_sect, sAnm))
    { // Нашли анимацию с привязкой к числу патронов\индексу - играем её
        PlayHUDMotion(sAnm, bMixIn, NULL, GetState());
        bFound = true;
    }
    else
    { // Не нашли анимацию с привязкой к числу патронов\индексу - ищем без них
        xr_sprintf(sAnm, "%s", M.c_str());
    }

    // Ищем анимацию дальше, уже без привязки к числу патронов
    if (bFound == false)
    {
        if (pSettings->line_exist(hud_sect, sAnm))
        {
            PlayHUDMotion(sAnm, bMixIn, NULL, GetState());
            bFound = true;
        }
        else
        {
            // Если анимация не найдена, но она обязательна - крашим игру
            if (bAssert == true)
            {
                R_ASSERT2(false, make_string("hudItem model [%s] has no motion with alias [%s]", hud_sect.c_str(), sAnm).c_str());
            }
        }
    }

    // Если такая анимация существует...
    if (bFound)
    {
        // Отыгрыаем звук
        string256 sSnd;
        xr_sprintf(sSnd, "%s%s", "snd_", sAnm);

        if (m_sounds.FindSoundItem(sSnd, false))
        { //--> Пробуем подыскать к ней особый звук
            PlaySound(sSnd, get_LastFP());
            if (m_fLastAnimStartTime > 0.0f)
                m_sounds.SetCurentTime(sSnd, m_fLastAnimStartTime);
        }
        else
        { //--> Иначе пробуем играть стандартный
            if (alias != NULL)
            {
                PlaySound(alias, get_LastFP());
                if (m_fLastAnimStartTime > 0.0f)
                    m_sounds.SetCurentTime(alias, m_fLastAnimStartTime);
            }
        }
    }

    return bFound;
}

// Проиграть мировую анимацию оружия
bool CWeapon::PlayWorldMotion(const shared_str& M, BOOL bMixIn)
{
    IKinematicsAnimated* pWeaponVisual = Visual()->dcast_PKinematicsAnimated();
    if (pWeaponVisual != NULL && pWeaponVisual->ID_Cycle_Safe(M).valid())
    {
        pWeaponVisual->PlayCycle(M.c_str(), bMixIn);
        return true;
    }
    return false;
}

// Возвращает необходимость проиграть анимацию с префиксом _wgl
bool CWeapon::IsWGLAnimRequired() { return IsGrenadeLauncherAttached() || IsForegripAttached(); }

////////////////////////////////////////////////////////////////////
// ************************************************************** //
////////////////////////////////////////////////////////////////////

#define def_IsGL_Mode (m_bGrenadeMode == true && m_bUseAmmoBeltMode == false)

// Анимация покоя (включая анимации ходьбы и бега)
void CWeapon::PlayAnimIdle()
{
    // Мировая анимация
    int  iAmmo       = GetMainAmmoElapsed();
    bool bEmptyExist = false;
    if (iAmmo == 0)
        bEmptyExist = PlayWorldMotion("idle_empty", true);
    if (bEmptyExist == false)
        PlayWorldMotion("idle", true);

    // Худовая анимация
    if (m_ZoomAnimState == eZANone)     // Проверяем что мы не играем анимацию зума
        if (TryPlayAnimIdle() == false) // Пробуем сперва проиграть анимации для ходьбы\бега
            PlayAnimIdleOnly();         // Иначе играем анимацию в покое

    m_bIdleFromZoomOut = false;
}

// Анимация покоя (не включая ходьбу и бег)
void CWeapon::PlayAnimIdleOnly()
{
    int iAmmo = GetMainAmmoElapsed();

    if (IsZoomed() && !m_bIdleFromZoomOut)
    {
        if (def_IsGL_Mode)
        {
            if (iAmmo == 0)
            {
                if (PlaySoundMotion("anm_idle_g_aim_empty", TRUE, NULL, false))
                    return;
            }
            if (PlaySoundMotion("anm_idle_g_aim", TRUE, NULL, false))
                return;
        }

        if (IsWGLAnimRequired())
        {
            if (iAmmo == 0)
            {
                if (PlaySoundMotion("anm_idle_aim_empty_w_gl", TRUE, NULL, false))
                    return;
            }
            if (PlaySoundMotion("anm_idle_aim_w_gl", TRUE, NULL, false))
                return;
        }

        if (true)
        {
            if (iAmmo == 0)
            {
                if (PlaySoundMotion("anm_idle_aim_empty", TRUE, NULL, false))
                    return;
            }
            if (PlaySoundMotion("anm_idle_aim", TRUE, NULL, false))
                return;
        }
    }

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_idle_g_empty", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_idle_g", TRUE, NULL, false))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_idle_empty_w_gl", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_idle_w_gl", TRUE, NULL, false))
            return;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_idle_empty", TRUE, NULL, false))
                return;
        }
    }

    PlaySoundMotion("anm_idle", TRUE, NULL, true);
}

// Анимация ходьбы
void CWeapon::PlayAnimIdleMoving()
{
    int iAmmo = GetMainAmmoElapsed();

    if (IsZoomed() && !m_bIdleFromZoomOut)
    {
        if (def_IsGL_Mode)
        {
            if (iAmmo == 0)
            {
                if (PlaySoundMotion("anm_idle_moving_g_aim_empty", TRUE, NULL, false))
                    return;
            }
            if (PlaySoundMotion("anm_idle_moving_g_aim", TRUE, NULL, false))
                return;
        }

        if (IsWGLAnimRequired())
        {
            if (iAmmo == 0)
            {
                if (PlaySoundMotion("anm_idle_moving_aim_empty_w_gl", TRUE, NULL, false))
                    return;
            }
            if (PlaySoundMotion("anm_idle_moving_aim_w_gl", TRUE, NULL, false))
                return;
        }

        if (true)
        {
            if (iAmmo == 0)
            {
                if (PlaySoundMotion("anm_idle_moving_aim_empty", TRUE, NULL, false))
                    return;
            }
            if (PlaySoundMotion("anm_idle_moving_aim", TRUE, NULL, false))
                return;
        }

        PlayAnimIdleOnly();
        return;
    }

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_idle_moving_g_empty", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_idle_moving_g", TRUE, NULL, false))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_idle_moving_empty_w_gl", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_idle_moving_w_gl", TRUE, NULL, false))
            return;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_idle_moving_empty", TRUE, NULL, false))
                return;
        }
    }

    if (PlaySoundMotion("anm_idle_moving", TRUE, NULL, false) == false)
        PlayAnimIdleOnly();
}

// Анимация бега
void CWeapon::PlayAnimIdleSprint()
{
    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_idle_sprint_g_empty", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_idle_sprint_g", TRUE, NULL, false))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_idle_sprint_empty_w_gl", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_idle_sprint_w_gl", TRUE, NULL, false))
            return;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_idle_sprint_empty", TRUE, NULL, false))
                return;
        }
    }

    if (PlaySoundMotion("anm_idle_sprint", TRUE, NULL, false) == false)
        PlayAnimIdleOnly();
}

// Анимация скуки
void CWeapon::PlayAnimBore()
{
    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_bore_g_empty", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_bore_g", TRUE, NULL, false))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_bore_empty_w_gl", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_bore_w_gl", TRUE, NULL, false))
            return;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_bore_empty", TRUE, NULL, false))
                return;
        }
    }

    PlaySoundMotion("anm_bore", TRUE, NULL, false);
}

// Анимация прятанья оружия
void CWeapon::PlayAnimHide()
{
    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_hide_g_empty", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_hide_g", TRUE, NULL, false))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_hide_empty_w_gl", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_hide_w_gl", TRUE, NULL, false))
            return;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_hide_empty", TRUE, NULL, false))
                return;
        }
    }

    PlaySoundMotion("anm_hide", TRUE, NULL, true);
}

// Анимация доставания оружия
void CWeapon::PlayAnimShow()
{
    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_show_g_empty", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_show_g", TRUE, NULL, false))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_show_empty_w_gl", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_show_w_gl", TRUE, NULL, false))
            return;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_show_empty", TRUE, NULL, false))
                return;
        }
    }

    PlaySoundMotion("anm_show", TRUE, NULL, true);
}

// Анимация переключения на подствол и обратно
void CWeapon::PlayAnimModeSwitch()
{
    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_switch_g_empty", TRUE, "sndSwitch", false))
                return;
        }
        PlaySoundMotion("anm_switch_g", TRUE, "sndSwitch", true);
    }
    else
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_switch_empty", TRUE, "sndSwitch", false))
                return;
        }
        PlaySoundMotion("anm_switch", TRUE, "sndSwitch", true);
    }
}

// Анимация перезарядки оружия
void CWeapon::PlayAnimReload()
{
    UpdBulletHideTimer();

    int iAmmo = m_overridenAmmoForReloadAnm;
    if (iAmmo < 0)
        iAmmo = GetMainAmmoElapsed();

    // Мировая анимация
    if (!def_IsGL_Mode)
    {
        bool bPlayAnim = true;
        if (GetState() == eSwitchMag &&
            (m_sub_state != eSubstateReloadBegin && m_sub_state != eSubstateMagazDetach && m_sub_state != eSubstateMagazMisfire))
            bPlayAnim = false;

        if (bPlayAnim)
        {
            bool bEmptyExist = false;
            if (iAmmo == 0)
                bEmptyExist = PlayWorldMotion("reload_world_empty", false);
            if (bEmptyExist == false)
                PlayWorldMotion("reload_world", false);
        }
    }

    // Худовая анимация
    if (m_bIsReloadFromAB)
        return PlayAnimReloadFrAB();

    if (IsAmmoBeltReloadNow())
        return PlayAnimReloadAB();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_reload_g_empty", TRUE, "sndReloadG", false))
                return;
        }
        if (PlaySoundMotion("anm_reload_g", TRUE, "sndReloadG", false))
            return;
    }

    // Если у нас установлен магазин и подствол\рукоятка\цевье, то требуется использовать особоую анимацию перезарядки под их комбинацию
    LPCSTR    sMagaz   = "";
    LPCSTR    sUBarrel = "";
    string256 combo_wombo;
    bool      bNeed2UseCombinedAnim = false; //--> Флаг необходимости использования комбинированной анимации

    if (IsWGLAnimRequired())
    {
        if ((IsGrenadeLauncherAttached() || IsForegripAttached()) && IsMagazineAttached() == true)
            bNeed2UseCombinedAnim = true; //--> Комбинированная анимация магазин + рукоятка\подствол

        if (bNeed2UseCombinedAnim)
        {
            sMagaz = GetAddonBySlot(eMagaz)->GetName().c_str(); //--> Секция текущего магазина

            do
            { //--> Секция текущего подствольного устройства
                if (IsGrenadeLauncherAttached())
                {
                    sUBarrel = GetAddonBySlot(eLauncher)->GetName().c_str();
                    break;
                }

                if (IsForegripAttached())
                {
                    sUBarrel = GetAddonBySlot(m_ForegripSlot)->GetName().c_str();
                    break;
                }
            } while (0);
        }

        if (iAmmo == 0)
        {
            if (bNeed2UseCombinedAnim)
            {
                xr_sprintf(combo_wombo, "anm_reload_empty_w_gl_%s_%s", sMagaz, sUBarrel);
                if (PlaySoundMotion(combo_wombo, TRUE, "sndReloadEmptyWGL", false))
                    return;
            }
            // Иначе играем обычную
            if (PlaySoundMotion("anm_reload_empty_w_gl", TRUE, "sndReloadEmptyWGL", false))
                return;
        }

        if (bNeed2UseCombinedAnim)
        {
            xr_sprintf(combo_wombo, "anm_reload_w_gl_%s_%s", sMagaz, sUBarrel);
            if (PlaySoundMotion(combo_wombo, TRUE, "sndReloadWGL", false))
                return;
        }
        // Иначе играем обычную
        if (PlaySoundMotion("anm_reload_w_gl", TRUE, "sndReloadWGL", false))
            return;

        bNeed2UseCombinedAnim = false;
    }

    if (IsForendAttached() == true && IsMagazineAttached() == true)
        bNeed2UseCombinedAnim = true; //--> Комбинированная анимация магазин + цевье

    if (bNeed2UseCombinedAnim)
    {
        sMagaz   = GetAddonBySlot(eMagaz)->GetName().c_str();       //--> Секция текущего магазина
        sUBarrel = GetAddonBySlot(m_ForendSlot)->GetName().c_str(); //--> Секция цевья
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (bNeed2UseCombinedAnim)
            {
                xr_sprintf(combo_wombo, "anm_reload_empty_%s_%s", sMagaz, sUBarrel);
                if (PlaySoundMotion(combo_wombo, TRUE, "sndReloadEmpty", false))
                    return;
            }
            if (PlaySoundMotion("anm_reload_empty", TRUE, "sndReloadEmpty", false))
                return;
        }
    }

    if (bNeed2UseCombinedAnim)
    {
        xr_sprintf(combo_wombo, "anm_reload_%s_%s", sMagaz, sUBarrel);
        if (PlaySoundMotion(combo_wombo, TRUE, "sndReload", false))
            return;
    }
    PlaySoundMotion("anm_reload", TRUE, "sndReload", true);
}

// Анимация перезарядки патронташа (цельная)
void CWeapon::PlayAnimReloadAB()
{
    int iAmmo = GetMainAmmoElapsed();

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_reload_ab_empty_w_gl", TRUE, "sndReloadABEmptyWGL", false))
                return;
        }
        if (PlaySoundMotion("anm_reload_ab_w_gl", TRUE, "sndReloadABWGL", false))
            return;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_reload_ab_empty", TRUE, "sndReloadABEmpty", false))
                return;
        }
    }

    PlaySoundMotion("anm_reload_ab", TRUE, "sndReloadAB", true);
}

// Анимация перезарядки оружия из патронташа
void CWeapon::PlayAnimReloadFrAB()
{
    int iAmmo     = GetMainAmmoElapsed();
    int iAmmoMain = GetMainAmmoElapsed();

    if (IsWGLAnimRequired())
    {
        if (iAmmoMain == 0)
        {
            if (PlaySoundMotion("anm_reload_fr_ab_empty_w_gl", TRUE, "sndReloadFrABEmptyWGL", false))
                return;
        }
        if (PlaySoundMotion("anm_reload_fr_ab_w_gl", TRUE, "sndReloadFrABWGL", false))
            return;
    }

    if (true)
    {
        if (iAmmoMain == 0)
        {
            if (PlaySoundMotion("anm_reload_fr_ab_empty", TRUE, "sndReloadFrABEmpty", false))
                return;
        }
    }

    PlaySoundMotion("anm_reload_fr_ab", TRUE, "sndReloadFrAB", true);
}

// Анимация перезарядки оружия в три стадии (открытие)
bool CWeapon::PlayAnimOpenWeapon()
{
    if (PlayAnimOpenWeaponFrAB())
        return true;

    if (IsAmmoBeltReloadNow())
        return PlayAnimOpenWeaponAB();

    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_open_g_empty", TRUE, "sndOpenEmptyG", false))
                return true;
        }
        if (PlaySoundMotion("anm_open_g", TRUE, "sndOpenG", false))
            return true;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_open_empty_w_gl", TRUE, "sndOpenEmptyWGL", false))
                return true;
        }
        if (PlaySoundMotion("anm_open_w_gl", TRUE, "sndOpenWGL", false))
            return true;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_open_empty", TRUE, "sndOpenEmpty", false))
                return true;
        }
    }

    return PlaySoundMotion("anm_open", TRUE, "sndOpen", false);
}

// Анимация перезарядки оружия в три стадии (открытие из патронташа)
bool CWeapon::PlayAnimOpenWeaponFrAB()
{
    if (m_bIsReloadFromAB == false)
        return false;

    int iAmmo     = GetGLAmmoElapsed();
    int iAmmoMain = GetMainAmmoElapsed();

    if (IsWGLAnimRequired())
    {
        if (iAmmoMain == 0)
        {
            if (PlaySoundMotion("anm_open_fr_ab_empty_w_gl", TRUE, "sndOpenFrABEmptyWGL", false, iAmmo))
                return true;
        }
        if (PlaySoundMotion("anm_open_fr_ab_w_gl", TRUE, "sndOpenFrABWGL", false, iAmmo))
            return true;
    }

    if (true)
    {
        if (iAmmoMain == 0)
        {
            if (PlaySoundMotion("anm_open_fr_ab_empty", TRUE, "sndOpenFrABEmpty", false, iAmmo))
                return true;
        }
    }

    return PlaySoundMotion("anm_open_fr_ab", TRUE, "sndOpenFrAB", false, iAmmo);
}

// Анимация перезарядки оружия в три стадии (вставка)
void CWeapon::PlayAnimAddOneCartridgeWeapon()
{
    UpdBulletHideTimer();

    if (m_bIsReloadFromAB)
    {
        PlayAnimAddOneCartridgeWeaponFrAB();
        return;
    }

    if (IsAmmoBeltReloadNow())
    {
        PlayAnimAddOneCartridgeWeaponAB();
        return;
    }

    if (PlayAnimSwitchAddOneCartridge())
        return;

    m_bSwitchAddAnimation = false;

    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_add_cartridge_g_empty", FALSE, "sndAddCartridgeEmptyG", false))
                return;
        }
        if (PlaySoundMotion("anm_add_cartridge_g", FALSE, "sndAddCartridgeG", false))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_add_cartridge_empty_w_gl", FALSE, "sndAddCartridgeEmptyWGL", false))
                return;
        }
        if (PlaySoundMotion("anm_add_cartridge_w_gl", FALSE, "sndAddCartridgeWGL", false))
            return;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_add_cartridge_empty", FALSE, "sndAddCartridgeEmpty", false))
                return;
        }
    }

    PlaySoundMotion("anm_add_cartridge", FALSE, "sndAddCartridge", true);
}

// Анимация перехода перезарядки из патронташа в перезарядку из инвентаря
bool CWeapon::PlayAnimSwitchAddOneCartridge()
{
    UpdBulletHideTimer();

    if (m_bSwitchAddAnimation == false)
        return false;

    int iAmmo = GetGLAmmoElapsed();

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_switch_add_cartridge_empty_w_gl", TRUE, "sndSwAddCartridgeEmptyWGL", false, iAmmo))
                return true;
        }
        if (PlaySoundMotion("anm_switch_add_cartridge_w_gl", TRUE, "sndSwAddCartridgeWGL", false, iAmmo))
            return true;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_switch_add_cartridge_empty", TRUE, "sndSwAddCartridgeEmpty", false, iAmmo))
                return true;
        }
    }

    return PlaySoundMotion("anm_switch_add_cartridge", TRUE, "sndSwAddCartridge", false, iAmmo);
}

// Анимация перезарядки оружия в три стадии (вставка из патронташа)
bool CWeapon::PlayAnimAddOneCartridgeWeaponFrAB()
{
    int iAmmo     = GetGLAmmoElapsed();
    int iAmmoMain = GetMainAmmoElapsed();

    if (IsWGLAnimRequired())
    {
        if (iAmmoMain == 0)
        {
            if (PlaySoundMotion("anm_add_cartridge_fr_ab_empty_w_gl", FALSE, "sndAddCartridgeFrABEmptyWGL", false, iAmmo))
                return true;
        }
        if (PlaySoundMotion("anm_add_cartridge_fr_ab_w_gl", FALSE, "sndAddCartridgeFrABWGL", false, iAmmo))
            return true;
    }

    if (true)
    {
        if (iAmmoMain == 0)
        {
            if (PlaySoundMotion("anm_add_cartridge_fr_ab_empty", FALSE, "sndAddCartridgeFrABEmpty", false, iAmmo))
                return true;
        }
    }

    return PlaySoundMotion("anm_add_cartridge_fr_ab", FALSE, "sndAddCartridgeFrAB", false, iAmmo);
}

// Анимация перезарядки оружия в три стадии (закрытие)
bool CWeapon::PlayAnimCloseWeapon()
{
    if (PlayAnimCloseWeaponFrAB())
        return true;

    if (IsAmmoBeltReloadNow())
        return PlayAnimCloseWeaponAB();

    if (PlayAnimCloseWeaponFromEmpty())
        return true;

    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_close_g_empty", FALSE, "sndCloseEmptyG", false))
                return true;
        }
        if (PlaySoundMotion("anm_close_g", FALSE, "sndCloseG", false))
            return true;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_close_empty_w_gl", FALSE, "sndCloseEmptyWGL", false))
                return true;
        }
        if (PlaySoundMotion("anm_close_w_gl", FALSE, "sndCloseWGL", false))
            return true;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_close_empty", FALSE, "sndCloseEmpty", false))
                return true;
        }
    }

    return PlaySoundMotion("anm_close", FALSE, "sndClose", false);
}

// Анимация перезарядки оружия в три стадии (закрытие из патронташа)
bool CWeapon::PlayAnimCloseWeaponFrAB()
{
    if (m_bIsReloadFromAB == false)
        return false;

    if (PlayAnimCloseWeaponFrABFromEmpty())
        return true;

    int iAmmo = GetGLAmmoElapsed();

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_close_fr_ab_empty_w_gl", FALSE, "sndCloseFrABEmptyWGL", false, iAmmo))
                return true;
        }
        if (PlaySoundMotion("anm_close_fr_ab_w_gl", FALSE, "sndCloseFrABWGL", false, iAmmo))
            return true;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_close_fr_ab_empty", FALSE, "sndCloseFrABEmpty", false, iAmmo))
                return true;
        }
    }

    return PlaySoundMotion("anm_close_fr_ab", FALSE, "sndCloseFrAB", false, iAmmo);
}

// Анимация перезарядки оружия в три стадии (закрытие, если зарядка была с пустого магазина)
bool CWeapon::PlayAnimCloseWeaponFromEmpty()
{
    if (m_overridenAmmoForReloadAnm != 0)
        return false;

    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_close_fempt_g_empty", FALSE, "sndCloseEmptyFEG", false))
                return true;
        }
        if (PlaySoundMotion("anm_close_fempt_g", FALSE, "sndCloseFEG", false))
            return true;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_close_fempt_empty_w_gl", FALSE, "sndCloseEmptyFEWGL", false))
                return true;
        }
        if (PlaySoundMotion("anm_close_fempt_w_gl", FALSE, "sndCloseFEWGL", false))
            return true;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_close_fempt_empty", FALSE, "sndCloseFEEmpty", false))
                return true;
        }
    }

    return PlaySoundMotion("anm_close_fempt", FALSE, "sndCloseFE", false);
}

// Анимация перезарядки оружия в три стадии (закрытие из патроншата, если зарядка была с пустого магазина)
bool CWeapon::PlayAnimCloseWeaponFrABFromEmpty()
{
    if (m_overridenAmmoForReloadAnm != 0)
        return false;

    int iAmmo = GetGLAmmoElapsed();

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_close_fempt_fr_ab_empty_w_gl", FALSE, "sndCloseEmptyFrABFEWGL", false, iAmmo))
                return true;
        }
        if (PlaySoundMotion("anm_close_fempt_fr_ab_w_gl", FALSE, "sndCloseFrABFEWGL", false, iAmmo))
            return true;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_close_fempt_fr_ab_empty", FALSE, "sndCloseFrABEmptyFE", false, iAmmo))
                return true;
        }
    }

    return PlaySoundMotion("anm_close_fempt_fr_ab", FALSE, "sndCloseFrABFE", false, iAmmo);
}

// Анимация перезарядки патронташа в три стадии (открытие)
bool CWeapon::PlayAnimOpenWeaponAB()
{
    if (IsWGLAnimRequired())
    {
        if (PlaySoundMotion("anm_open_ab_w_gl", TRUE, "sndOpenABWGL", false))
            return true;
    }
    return PlaySoundMotion("anm_open_ab", TRUE, "sndOpenAB", false);
}

// Анимация перезарядки патронташа в три стадии (вставка)
bool CWeapon::PlayAnimAddOneCartridgeWeaponAB()
{
    int iAmmo = GetGLAmmoElapsed();

    if (IsWGLAnimRequired())
    {
        if (PlaySoundMotion("anm_add_cartridge_ab_w_gl", FALSE, "sndAddCartridgeABWGL", false, iAmmo))
            return true;
    }
    return PlaySoundMotion("anm_add_cartridge_ab", FALSE, "sndAddCartridgeAB", false, iAmmo);
}

// Анимация перезарядки патронташа в три стадии (закрытие)
bool CWeapon::PlayAnimCloseWeaponAB()
{
    if (IsWGLAnimRequired())
    {
        if (PlaySoundMotion("anm_close_ab_w_gl", FALSE, "sndCloseABWGL", false))
            return true;
    }
    return PlaySoundMotion("anm_close_ab", FALSE, "sndCloseAB", false);
}

// Анимация помпы \ болтовки
void CWeapon::PlayAnimPump()
{
    if (IsZoomed() && !m_bIdleFromZoomOut)
    {
        if (IsWGLAnimRequired())
        {
            if (PlaySoundMotion("anm_pump_aim_w_gl", TRUE, "sndPumpAimWGL", false))
                return;
        }

        if (PlaySoundMotion("anm_pump_aim", TRUE, "sndPumpAim", false))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (PlaySoundMotion("anm_pump_w_gl", TRUE, "sndPumpWGL", false))
            return;
    }

    PlaySoundMotion("anm_pump", TRUE, "sndPump", true);
}

// Анимация прицеливания (реверсивная для входа и выхода)
void CWeapon::PlayAnimZoom()
{
    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_zoom_g_empty", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_zoom_g", TRUE, NULL, false))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_zoom_empty_w_gl", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_zoom_w_gl", TRUE, NULL, false))
            return;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_zoom_empty", TRUE, NULL, false))
                return;
        }
    }

    if (PlaySoundMotion("anm_zoom", TRUE, NULL, false))
        return;

    m_ZoomAnimState = eZANone; // Не нашли в оружии анимацию для зума
    PlayAnimIdle();
}

// Анимация стрельбы при пустом магазине
void CWeapon::PlayAnimEmptyClick()
{
    int iAmmo = GetMainAmmoElapsed();

    if (IsZoomed())
    {
        if (def_IsGL_Mode)
        {
            if (PlaySoundMotion("anm_empty_click_g_aim", TRUE, "sndEmptyClick", false, iAmmo))
                return;
        }

        if (IsWGLAnimRequired())
        {
            if (PlaySoundMotion("anm_empty_click_aim_w_gl", TRUE, "sndEmptyClick", false))
                return;
        }

        if (true)
        {
            if (PlaySoundMotion("anm_empty_click_aim", TRUE, "sndEmptyClick", false))
                return;
        }
    }

    if (def_IsGL_Mode)
    {
        if (PlaySoundMotion("anm_empty_click_g", TRUE, "sndEmptyClick", false, iAmmo))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (PlaySoundMotion("anm_empty_click_w_gl", TRUE, "sndEmptyClick", false))
            return;
    }

    PlaySoundMotion("anm_empty_click", TRUE, "sndEmptyClick", false);
}

// Анимация стрельбы
void CWeapon::PlayAnimShoot()
{
    int  iAmmo       = GetMainAmmoElapsed() + 1; //--> Т.к анимация стрельбы играется ПОСЛЕ выстрела
    bool bLastBullet = (iAmmo == 1);

    // Мировая анимация
    if (!def_IsGL_Mode)
    {
        bool bLastExist = false;
        if (bLastBullet)
            bLastExist = PlayWorldMotion("shoot_last", false);
        if (bLastExist == false)
            PlayWorldMotion("shoot", false);
    }

    // Худовая анимация
    if (IsZoomed())
    {
        if (def_IsGL_Mode)
        {
            if (bLastBullet)
            {
                if (PlaySoundMotion("anm_shot_l_g_aim", FALSE, NULL, false, iAmmo))
                    return;
            }
            if (PlaySoundMotion("anm_shots_g_aim", FALSE, NULL, false, iAmmo))
                return;
        }

        if (IsWGLAnimRequired())
        {
            if (bLastBullet)
            {
                if (PlaySoundMotion("anm_shot_l_aim_w_gl", FALSE, NULL, false, iAmmo))
                    return;
            }
            if (PlaySoundMotion("anm_shots_aim_w_gl", FALSE, NULL, false, iAmmo))
                return;
        }

        if (true)
        {
            if (bLastBullet)
            {
                if (PlaySoundMotion("anm_shot_l_aim", FALSE, NULL, false, iAmmo))
                    return;
            }
            if (PlaySoundMotion("anm_shots_aim", FALSE, NULL, false, iAmmo))
                return;
        }
    }

    if (def_IsGL_Mode)
    {
        if (bLastBullet)
        {
            if (PlaySoundMotion("anm_shot_l_g", FALSE, NULL, false, iAmmo))
                return;
        }
        if (PlaySoundMotion("anm_shots_g", FALSE, NULL, false, iAmmo))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (bLastBullet)
        {
            if (PlaySoundMotion("anm_shot_l_w_gl", FALSE, NULL, false, iAmmo))
                return;
        }
        if (PlaySoundMotion("anm_shots_w_gl", FALSE, NULL, false, iAmmo))
            return;
    }

    if (bLastBullet)
    {
        if (PlaySoundMotion("anm_shot_l", FALSE, NULL, false, iAmmo))
            return;
    }

    if (PlaySoundMotion("anm_shot", FALSE, NULL, false, iAmmo))
        return;
    PlaySoundMotion("anm_shots", FALSE, NULL, true, iAmmo);
}

// Анимация атаки ножом
void CWeapon::PlayAnimKnifeAttack()
{
    int state = GetState();
    switch (state)
    {
    case eFire: PlaySoundMotion("anm_attack", FALSE, NULL, false); return;
    case eFire2: PlaySoundMotion("anm_attack2", FALSE, NULL, false); return;
    }
    Need2Idle();
}

// Анимация удара прикладом (основной)
void CWeapon::PlayAnimKick()
{
    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_g_empty", FALSE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_kick_g", FALSE, NULL, false))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_empty_w_gl", FALSE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_kick_w_gl", FALSE, NULL, false))
            return;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_empty", FALSE, NULL, false))
                return;
        }
    }

    PlaySoundMotion("anm_kick", FALSE, NULL, true);
}

// Анимация удара прикладом (альтернативный)
void CWeapon::PlayAnimKickAlt()
{
    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_alt_g_empty", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_kick_alt_g", TRUE, NULL, false))
            return;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_alt_empty_w_gl", TRUE, NULL, false))
                return;
        }
        if (PlaySoundMotion("anm_kick_alt_w_gl", TRUE, NULL, false))
            return;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_alt_empty", TRUE, NULL, false))
                return;
        }
    }

    PlaySoundMotion("anm_kick_alt", TRUE, NULL, true);
}

// Анимация выхода из удара прикладом (основной)
bool CWeapon::PlayAnimKickOut()
{
    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_out_g_empty", FALSE, NULL, false))
                return true;
        }
        if (PlaySoundMotion("anm_kick_out_g", FALSE, NULL, false))
            return true;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_out_empty_w_gl", FALSE, NULL, false))
                return true;
        }
        if (PlaySoundMotion("anm_kick_out_w_gl", FALSE, NULL, false))
            return true;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_out_empty", FALSE, NULL, false))
                return true;
        }
    }

    if (PlaySoundMotion("anm_kick_out", FALSE, NULL, false))
        return true;

    return false;
}

// Анимация выхода из удара прикладом (альтернативный)
bool CWeapon::PlayAnimKickOutAlt()
{
    int iAmmo = GetMainAmmoElapsed();

    if (def_IsGL_Mode)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_alt_out_g_empty", FALSE, NULL, false))
                return true;
        }
        if (PlaySoundMotion("anm_kick_alt_out_g", FALSE, NULL, false))
            return true;
    }

    if (IsWGLAnimRequired())
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_alt_out_empty_w_gl", FALSE, NULL, false))
                return true;
        }
        if (PlaySoundMotion("anm_kick_alt_out_w_gl", FALSE, NULL, false))
            return true;
    }

    if (true)
    {
        if (iAmmo == 0)
        {
            if (PlaySoundMotion("anm_kick_alt_out_empty", FALSE, NULL, false))
                return true;
        }
    }

    if (PlaySoundMotion("anm_kick_alt_out", FALSE, NULL, false))
        return true;

    return false;
}

#undef def_IsGL_Mode