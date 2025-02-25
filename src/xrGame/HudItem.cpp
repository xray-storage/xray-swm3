#include "stdafx.h"
#include "HudItem.h"
#include "physic_item.h"
#include "actor.h"
#include "actoreffector.h"
#include "actorcondition.h" //--#SM+#--
#include "Missile.h"
#include "xrmessages.h"
#include "Level.h"
#include "inventory.h"
#include "xrEngine/CameraBase.h"
#include "player_hud.h"
#include "xrCore/Animation/SkeletonMotions.hpp"

CHudItem::CHudItem()
{
    RenderHud(TRUE);
    EnableHudInertion(TRUE);
    AllowHudInertion(TRUE);
    m_bStopAtEndAnimIsRunning = false;
    m_current_motion_def = NULL;
    m_started_rnd_anim_idx = u8(-1);
    m_fLastAnimStartTime = 0.0f; //--#SM+#--
    m_bEnableMovAnimAtCrouch = false; //--#SM+#--
    m_fIdleSpeedCrouchFactor = 1.f; //--#SM+#--
    m_fIdleSpeedNoAccelFactor = 1.f; //--#SM+#--
}

IFactoryObject* CHudItem::_construct()
{
    m_object = smart_cast<CPhysicItem*>(this);
    VERIFY(m_object);

    m_item = smart_cast<CInventoryItem*>(this);
    VERIFY(m_item);

    return (m_object);
}

CHudItem::~CHudItem() {}
void CHudItem::Load(LPCSTR section)
{
    hud_sect = pSettings->r_string(section, "hud");
    m_animation_slot = pSettings->r_u32(section, "animation_slot");

    m_bEnableMovAnimAtCrouch =
        READ_IF_EXISTS(pSettings, r_bool, section, "enable_mov_anim_at_crouch", false); //--#SM+#--

    m_fIdleSpeedCrouchFactor =
        READ_IF_EXISTS(pSettings, r_float, hud_sect, "idle_speed_crouch_factor", 1.f); //--#SM+#--
    m_fIdleSpeedNoAccelFactor =
        READ_IF_EXISTS(pSettings, r_float, hud_sect, "idle_speed_noaccel_factor", 1.f); //--#SM+#--

    m_sounds.LoadSound(section, "snd_bore", "sndBore", true);
}

void CHudItem::PlaySound(LPCSTR alias, const Fvector& position)
{
    m_sounds.PlaySound(alias, position, object().H_Root(), !!GetHUDmode());
}

void CHudItem::renderable_Render()
{
    UpdateXForm();
    BOOL _hud_render = GlobalEnv.Render->get_HUD() && GetHUDmode();

    if (_hud_render && !IsHidden())
    {
    }
    else
    {
        if (!object().H_Parent() || (!_hud_render && !IsHidden()))
        {
            on_renderable_Render();
            debug_draw_firedeps();
        }
        else if (object().H_Parent())
        {
            CInventoryOwner* owner = smart_cast<CInventoryOwner*>(object().H_Parent());
            VERIFY(owner);
            CInventoryItem* self = smart_cast<CInventoryItem*>(this);
            if (owner->attached(self))
                on_renderable_Render();
        }
    }
}

void CHudItem::SwitchState(u32 S)
{
    if (OnClient())
        return;

    SetNextState(S);

    if (object().Local() && !object().getDestroy())
    {
        // !!! Just single entry for given state !!!
        NET_Packet P;
        object().u_EventGen(P, GE_WPN_STATE_CHANGE, object().ID());
        P.w_u8(u8(S));
        object().u_EventSend(P);
    }
}

void CHudItem::OnEvent(NET_Packet& P, u16 type)
{
    switch (type)
    {
    case GE_WPN_STATE_CHANGE:
    {
        u8 S;
        P.r_u8(S);
        OnStateSwitch(u32(S));
    }
    break;
    }
}

void CHudItem::OnStateSwitch(u32 S)
{
    SetState(S);

    if (object().Remote())
        SetNextState(S);

    switch (S)
    {
    case eBore:
        SetPending(FALSE);

        PlayAnimBore();
        if (HudItemData())
        {
            Fvector P = HudItemData()->m_item_transform.c;
            m_sounds.PlaySound("sndBore", P, object().H_Root(), !!GetHUDmode(), false, m_started_rnd_anim_idx);
        }

        break;
    }
}

void CHudItem::OnAnimationEnd(u32 state)
{
    switch (state)
    {
    case eBore: { SwitchState(eIdle);
    }
    break;
    }
}

void CHudItem::PlayAnimBore() { PlayHUDMotion("anm_bore", TRUE, this, GetState()); }
bool CHudItem::ActivateItem()
{
    OnActiveItem();
    return true;
}

void CHudItem::DeactivateItem() { OnHiddenItem(); }
void CHudItem::OnMoveToRuck(const SInvItemPlace& prev) { SwitchState(eHidden); }
void CHudItem::SendDeactivateItem() { SendHiddenItem(); }
void CHudItem::SendHiddenItem()
{
    if (!object().getDestroy())
    {
        NET_Packet P;
        object().u_EventGen(P, GE_WPN_STATE_CHANGE, object().ID());
        P.w_u8(u8(eHiding));
        object().u_EventSend(P, net_flags(TRUE, TRUE, FALSE, TRUE));
    }
}

void CHudItem::UpdateHudAdditonal(Fmatrix& hud_trans) {}
void CHudItem::UpdateCL()
{
    if (m_current_motion_def)
    {
        if (m_bStopAtEndAnimIsRunning)
        {
            const xr_vector<motion_marks>& marks = m_current_motion_def->marks;
            if (!marks.empty())
            {
                float motion_prev_time = ((float)m_dwMotionCurrTm - (float)m_dwMotionStartTm) / 1000.0f;
                float motion_curr_time = ((float)Device.dwTimeGlobal - (float)m_dwMotionStartTm) / 1000.0f;

                xr_vector<motion_marks>::const_iterator it = marks.begin();
                xr_vector<motion_marks>::const_iterator it_e = marks.end();
                for (; it != it_e; ++it)
                {
                    const motion_marks& M = (*it);
                    if (M.is_empty())
                        continue;

                    const motion_marks::interval* Iprev = M.pick_mark(motion_prev_time);
                    const motion_marks::interval* Icurr = M.pick_mark(motion_curr_time);
                    if (Iprev == NULL && Icurr != NULL /* || M.is_mark_between(motion_prev_time, motion_curr_time)*/)
                    {
                        OnMotionMark(m_startedMotionState, M);
                    }
                }
            }

            m_dwMotionCurrTm = Device.dwTimeGlobal;
            if (m_dwMotionCurrTm > m_dwMotionEndTm)
            {
                m_current_motion_def = NULL;
                m_dwMotionStartTm = 0;
                m_dwMotionEndTm = 0;
                m_dwMotionCurrTm = 0;
                m_bStopAtEndAnimIsRunning = false;
                OnAnimationEnd(m_startedMotionState);
            }
        }
    }
}

void CHudItem::OnH_A_Chield() {}
void CHudItem::OnH_B_Chield() { StopCurrentAnimWithoutCallback(); }
void CHudItem::OnH_B_Independent(bool just_before_destroy)
{
    m_sounds.StopAllSounds();
    UpdateXForm();

    // next code was commented
    /*
    if(HudItemData() && !just_before_destroy)
    {
        object().XFORM().set( HudItemData()->m_item_transform );
    }

    if (HudItemData())
    {
        g_player_hud->detach_item(this);
        Msg("---Detaching hud item [%s][%d]", this->HudSection().c_str(), this->object().ID());
    }*/
    // SetHudItemData			(NULL);
}

void CHudItem::OnH_A_Independent()
{
    if (HudItemData())
        g_player_hud->detach_item(this);
    StopCurrentAnimWithoutCallback();
}

void CHudItem::on_b_hud_detach() { m_sounds.StopAllSounds(); }
void CHudItem::on_a_hud_attach()
{
    if (m_current_motion_def)
    {
        PlayHUDMotion_noCB(m_current_motion, FALSE);
#ifdef DEBUG
//		Msg("continue playing [%s][%d]",m_current_motion.c_str(), Device.dwFrame);
#endif // #ifdef DEBUG
    }
    else
    {
#ifdef DEBUG
//		Msg("no active motion");
#endif // #ifdef DEBUG
    }
}

// Вызывается перед проигрыванием любой анимации.
// Возвращает необходимость проиграть анимацию не с начала, а с первой метки внутри неё.
bool CHudItem::OnBeforeMotionPlayed(const shared_str& sMotionName) //--#SM+#--
{
    CActor* pActor = smart_cast<CActor*>(object().H_Parent());
    if (pActor)
    {
        u32 actor_state = pActor->MovingState();
        bool bSprint = !!(actor_state & mcSprint); // Бежим

        if (!bSprint && strstr(sMotionName.c_str(), "anm_idle") != NULL)
        { // Мы играем Idle анимацию - выставляем её скорость в зависииости от состояния тела игрока
            bool bCrouch = !!(actor_state & mcCrouch); // На корточках
            bool bZooming = pActor->IsZoomAimingMode(); // Целимся
            bool bNotAccelerated = !isActorAccelerated(actor_state, bZooming); // Зажатый Shift (не ускоряться)

            float fIdleAnimSpeedFactor = 1.0f;
            if (bNotAccelerated && (bCrouch || !bZooming))
            {
                fIdleAnimSpeedFactor *= m_fIdleSpeedNoAccelFactor;
            }
            if (bCrouch)
            {
                fIdleAnimSpeedFactor *= m_fIdleSpeedCrouchFactor;
            }

            g_player_hud->SetAnimSpeedMod(fIdleAnimSpeedFactor);
        }
    }

    return false;
}

u32 CHudItem::PlayHUDMotion(const shared_str& M, BOOL bMixIn, CHudItem* W, u32 state) //--#SM+#--
{
    m_fLastAnimStartTime = 0.0f;

    bool bTakeTimeFromMark = OnBeforeMotionPlayed(M);
    if (bTakeTimeFromMark)
    {
        PlayHUDMotion_noCB(M, bMixIn); //--> Обновим m_current_motion_def
        const xr_vector<motion_marks>& marks = m_current_motion_def->marks;
        if (!marks.empty())
        {
            m_fLastAnimStartTime = marks.begin()->time_to_next_mark(0.f);
            g_player_hud->SetAnimStartTime(m_fLastAnimStartTime); //--> Время считываем с метки из верхней полоски
        }
    }

    if (GetHUDmode() == true) //--#SM+#-- SM_TODO Консольную команду сделай?
    {
        // Значит в PlayHUDMotion_noCB функция motion_length не учитывает анимации из сокет аддонов <?!> SM_TODO
        Msg("PlayAnim [%s] Mixed = [%d] StartTime = [%f] Speed = [%f]", M.c_str(), bMixIn,
            g_player_hud->GetStartTimeOverridden(), g_player_hud->GetSpeedModOverridden());
    }

    u32 anim_time = PlayHUDMotion_noCB(M, bMixIn);
    if (anim_time > 0)
    {
        m_bStopAtEndAnimIsRunning = true;
        m_dwMotionStartTm = Device.dwTimeGlobal;
        m_dwMotionCurrTm = m_dwMotionStartTm;
        m_dwMotionEndTm = m_dwMotionStartTm + anim_time;
        m_startedMotionState = state;
    }
    else
        m_bStopAtEndAnimIsRunning = false;

    return anim_time;
}

u32 CHudItem::PlayHUDMotion_noCB(const shared_str& motion_name, BOOL bMixIn)
{
    m_current_motion = motion_name;

    if (bDebug && item().m_pInventory)
    {
        Msg("-[%s] as[%d] [%d]anim_play [%s][%d]", HudItemData() ? "HUD" : "Simulating",
            item().m_pInventory->GetActiveSlot(), item().object_id(), motion_name.c_str(), Device.dwFrame);
    }

    if (HudItemData())
    {
        return HudItemData()->anim_play(motion_name, bMixIn, m_current_motion_def, m_started_rnd_anim_idx);
    }
    else
    {
        m_started_rnd_anim_idx = 0;
        return g_player_hud->motion_length(motion_name, HudSection(), m_current_motion_def);
    }
}

void CHudItem::StopCurrentAnimWithoutCallback()
{
    m_dwMotionStartTm = 0;
    m_dwMotionEndTm = 0;
    m_dwMotionCurrTm = 0;
    m_bStopAtEndAnimIsRunning = false;
    m_current_motion_def = NULL;
}

BOOL CHudItem::GetHUDmode()
{
    if (object().H_Parent())
    {
        CActor* A = smart_cast<CActor*>(object().H_Parent());
        return (A && A->HUDview() && HudItemData());
    }
    else
        return FALSE;
}

void CHudItem::PlayAnimIdle()
{
    if (TryPlayAnimIdle())
        return;

    PlayHUDMotion("anm_idle", TRUE, NULL, GetState());
}

bool CHudItem::TryPlayAnimIdle()
{
    if (MovingAnimAllowedNow())
    {
        CActor* pActor = smart_cast<CActor*>(object().H_Parent());
        if (pActor)
        {
            CEntity::SEntityState st;
            pActor->g_State(st);
            if (st.bSprint)
            {
                PlayAnimIdleSprint();
                return true;
            }
            if ((m_bEnableMovAnimAtCrouch || !st.bCrouch) && pActor->AnyMove()) //--#SM+#--
            {
                PlayAnimIdleMoving();
                return true;
            }
        }
    }
    return false;
}

void CHudItem::PlayAnimIdleMoving() { PlayHUDMotion("anm_idle_moving", TRUE, NULL, GetState()); }
void CHudItem::PlayAnimIdleSprint() { PlayHUDMotion("anm_idle_sprint", TRUE, NULL, GetState()); }
void CHudItem::OnMovementChanged(ACTOR_DEFS::EMoveCommand cmd)
{
    if (GetState() == eIdle && !m_bStopAtEndAnimIsRunning)
    {
        if ((cmd == ACTOR_DEFS::mcAccel) || (cmd == ACTOR_DEFS::mcSprint) ||
            (cmd == ACTOR_DEFS::mcAnyMove)) //--#SM+#-- + ActorAnimation.cpp
        {
            PlayAnimIdle();
            ResetSubStateTime();
        }
    }
}

attachable_hud_item* CHudItem::HudItemData()
{
    attachable_hud_item* hi = NULL;
    if (!g_player_hud)
        return hi;

    hi = g_player_hud->attached_item(0);
    if (hi && hi->m_parent_hud_item == this)
        return hi;

    hi = g_player_hud->attached_item(1);
    if (hi && hi->m_parent_hud_item == this)
        return hi;

    return NULL;
}
