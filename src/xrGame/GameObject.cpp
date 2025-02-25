#include "pch_script.h"
#include "GameObject.h"

#include "Include/xrRender/RenderVisual.h"
#include "xrPhysics/PhysicsShell.h"
#include "ai_space.h"
#include "CustomMonster.h"
#include "physicobject.h"
#include "HangingLamp.h"
#include "xrPhysics/PhysicsShell.h"
#include "game_sv_single.h"
#include "xrAICore/Navigation/level_graph.h"
#include "ph_shell_interface.h"
#include "script_game_object.h"
#include "xrserver_objects_alife.h"
#include "xrServer_Objects_ALife_Items.h"
#include "game_cl_base.h"
#include "object_factory.h"
#include "Include/xrRender/Kinematics.h"
#include "xrAICore/Navigation/ai_object_location_impl.h"
#include "xrAICore/Navigation/game_graph.h"
#include "ai_debug.h"
#include "xrEngine/IGame_Level.h"
#include "Level.h"
#include "script_callback_ex.h"
#include "xrPhysics/MathUtils.h"
#include "game_cl_base_weapon_usage_statistic.h"
#include "game_cl_mp.h"
#include "reward_event_generator.h"
#include "xrAICore/Navigation/game_level_cross_table.h"
#include "ai_obstacle.h"
#include "magic_box3.h"
#include "animation_movement_controller.h"
#include "xrEngine/xr_collide_form.h"
#include "script_game_object.h"
#include "script_callback_ex.h"
#include "game_object_space.h"
#include "doors_door.h"
#include "doors.h"
#include "attachable_visual.h" //--#SM+#--

#pragma warning(push)
#pragma warning(disable : 4995)
#include <intrin.h>
#pragma warning(pop)

#pragma intrinsic(_InterlockedCompareExchange)

extern MagicBox3 MagicMinBox(int iQuantity, const Fvector* akPoint);

#ifdef DEBUG
#include "debug_renderer.h"
#include "PHDebug.h"
#endif

ENGINE_API bool g_dedicated_server;

static const float base_spu_epsP = 0.05f;
static const float base_spu_epsR = 0.05f;

CGameObject::CGameObject() : SpatialBase(g_SpatialSpace), scriptBinder(this)
{
    dwFrame_AsCrow = u32(-1);
    Props.storage = 0;
    Parent = nullptr;
    NameObject = nullptr;
    NameSection = nullptr;
    NameVisual = nullptr;
#ifdef DEBUG
    shedule.dbg_update_shedule = u32(-1) / 2;
    dbg_update_cl = u32(-1) / 2;
#endif
    // ~IGameObject ctor
    // CUsableScriptObject init
    m_bNonscriptUsable = true;
    set_tip_text_default();
    //
    m_ai_obstacle = 0;

    init();
    //-----------------------------------------
    m_bCrPr_Activated = false;
    m_dwCrPr_ActivationStep = 0;
    m_spawn_time = 0;
    m_ai_location = !g_dedicated_server ? new CAI_ObjectLocation() : 0;
    m_server_flags.one();

    m_callbacks = new CALLBACK_MAP();
    m_anim_mov_ctrl = 0;
}

CGameObject::~CGameObject()
{
    VERIFY(!animation_movement());
    VERIFY(!m_ini_file);
    VERIFY(!m_lua_game_object);
    VERIFY(!m_spawned);
    xr_delete(m_ai_location);
    xr_delete(m_callbacks);
    xr_delete(m_ai_obstacle);
    cNameVisual_set(0);
    cName_set(0);
    cNameSect_set(0);
}

void CGameObject::MakeMeCrow()
{
    if (Props.crow)
        return;
    if (!processing_enabled())
        return;
    u32 const device_frame_id = Device.dwFrame;
    u32 const object_frame_id = dwFrame_AsCrow;
    if ((u32)_InterlockedCompareExchange((long*)&dwFrame_AsCrow, device_frame_id, object_frame_id) == device_frame_id)
        return;
    VERIFY(dwFrame_AsCrow == device_frame_id);
    Props.crow = 1;
    g_pGameLevel->Objects.o_crow(this);
}

void CGameObject::cName_set(shared_str N) { NameObject = N; }
void CGameObject::cNameSect_set(shared_str N) { NameSection = N; }
void CGameObject::cNameVisual_set(shared_str N)
{
    // check if equal
    if (*N && *NameVisual)
        if (N == NameVisual)
            return;
    // replace model
    if (*N && N[0])
    {
        IRenderVisual* old_v = renderable.visual;
        NameVisual = N;
        renderable.visual = GlobalEnv.Render->model_Create(*N);
        IKinematics* old_k = old_v ? old_v->dcast_PKinematics() : NULL;
        IKinematics* new_k = renderable.visual->dcast_PKinematics();
        /*
        if(old_k && new_k){
        new_k->Update_Callback = old_k->Update_Callback;
        new_k->Update_Callback_Param = old_k->Update_Callback_Param;
        }
        */
        if (old_k && new_k)
        {
            new_k->SetUpdateCallback(old_k->GetUpdateCallback());
            new_k->SetUpdateCallbackParam(old_k->GetUpdateCallbackParam());
        }
        GlobalEnv.Render->model_Delete(old_v);
    }
    else
    {
        GlobalEnv.Render->model_Delete(renderable.visual);
        NameVisual = 0;
    }
    OnChangeVisual();
}

// flagging
void CGameObject::processing_activate()
{
    VERIFY3(255 != Props.bActiveCounter, "Invalid sequence of processing enable/disable calls: overflow", *cName());
    Props.bActiveCounter++;
    if (!(Props.bActiveCounter - 1))
        g_pGameLevel->Objects.o_activate(this);
}

void CGameObject::processing_deactivate()
{
    VERIFY3(Props.bActiveCounter, "Invalid sequence of processing enable/disable calls: underflow", *cName());
    Props.bActiveCounter--;
    if (!Props.bActiveCounter)
        g_pGameLevel->Objects.o_sleep(this);
}

void CGameObject::setEnabled(BOOL _enabled)
{
    if (_enabled)
    {
        Props.bEnabled = 1;
        if (CForm)
            spatial.type |= STYPE_COLLIDEABLE;
    }
    else
    {
        Props.bEnabled = 0;
        spatial.type &= ~STYPE_COLLIDEABLE;
    }
}

void CGameObject::setVisible(BOOL _visible)
{
    if (_visible)
    {
        // Parent should control object visibility itself (??????)
        Props.bVisible = 1;
        if (renderable.visual)
            spatial.type |= STYPE_RENDERABLE;
    }
    else
    {
        Props.bVisible = 0;
        spatial.type &= ~STYPE_RENDERABLE;
    }
}

void CGameObject::Center(Fvector& C) const
{
    VERIFY2(renderable.visual, *cName());
    renderable.xform.transform_tiny(C, renderable.visual->getVisData().sphere.P);
}

float CGameObject::Radius() const
{
    VERIFY2(renderable.visual, *cName());
    return renderable.visual->getVisData().sphere.R;
}

const Fbox& CGameObject::BoundingBox() const
{
    VERIFY2(renderable.visual, *cName());
    return renderable.visual->getVisData().box;
}

void CGameObject::Load(LPCSTR section)
{
    // Name
    R_ASSERT(section);
    cName_set(section);
    cNameSect_set(section);
    // Visual and light-track
    if (pSettings->line_exist(section, "visual"))
    {
        string_path tmp;
        xr_strcpy(tmp, pSettings->r_string(section, "visual"));
        if (strext(tmp))
            *strext(tmp) = 0;
        xr_strlwr(tmp);
        cNameVisual_set(tmp);
    }
    setVisible(false);
    // ~
    ISpatial* self = smart_cast<ISpatial*>(this);
    if (self)
    {
        // #pragma todo("to Dima: All objects are visible for AI ???")
        // self->spatial.type	|=	STYPE_VISIBLEFORAI;
        self->GetSpatialData().type &= ~STYPE_REACTTOSOUND;
    }

    // Загружаем начальное значение "тепло-излучаемости" объекта --#SM+#--
    m_common_values.m_fIRNV_value_max = READ_IF_EXISTS(pSettings, r_float, section, "shader_irnv_value_max", 0.0f);
    clamp(m_common_values.m_fIRNV_value_max, 0.0f, 1.0f);

    m_common_values.m_fIRNV_value_min =
        READ_IF_EXISTS(pSettings, r_float, section, "shader_irnv_value_min", m_common_values.m_fIRNV_value_max);
    clamp(m_common_values.m_fIRNV_value_min, 0.0f, 1.0f);
    if (m_common_values.m_fIRNV_value_min > m_common_values.m_fIRNV_value_max)
        m_common_values.m_fIRNV_value_min = m_common_values.m_fIRNV_value_max;

    m_common_values.m_fIRNV_cooling_speed =
        READ_IF_EXISTS(pSettings, r_float, section, "shader_irnv_cooling_speed", 0.0f) / 100.f;

    m_common_values.m_fIRNV_value = m_common_values.m_fIRNV_value_min;
    m_common_values.m_fIRNV_max_or_min =
        false; //--> По умолчанию для объектов юзаем их минимиальное тепло, если объект живой, то максимальное (Entity_alive.cpp) SM_TODO
}

// Вызывается после Load //--#SM+#--
void CGameObject::PostLoad(LPCSTR section) {}

void CGameObject::init()
{
    m_lua_game_object = 0;
    m_script_clsid = -1;
    m_ini_file = 0;
    m_spawned = false;
}

void CGameObject::reinit()
{
    m_visual_callback.clear();
    if (!g_dedicated_server)
        ai_location().reinit();

    // clear callbacks
    for (CALLBACK_MAP_IT it = m_callbacks->begin(); it != m_callbacks->end(); ++it)
        it->second.clear();
}

void CGameObject::reload(LPCSTR section) { m_script_clsid = object_factory().script_clsid(CLS_ID); }
void CGameObject::net_Destroy()
{
#ifdef DEBUG
    if (psAI_Flags.test(aiDestroy))
        Msg("Destroying client object [%d][%s][%x]", ID(), *cName(), this);
#endif

    VERIFY(m_spawned);

    delete_data(m_attached_visuals); // --#SM+#-- Удаляем присоединённые визуалы

    if (m_anim_mov_ctrl)
        destroy_anim_mov_ctrl();

    xr_delete(m_ini_file);

    m_script_clsid = -1;
    if (Visual() && smart_cast<IKinematics*>(Visual()))
        smart_cast<IKinematics*>(Visual())->Callback(0, 0);
    //
    VERIFY(getDestroy());
    xr_delete(CForm);
    if (register_schedule())
        shedule_unregister();
    spatial_unregister();
    // setDestroy (true); // commented in original src
    // remove visual
    cNameVisual_set(0);
    // ~
    setReady(FALSE);

    if (Level().IsDemoPlayStarted() && ID() == u16(-1))
    {
        Msg("Destroying demo_spectator object");
    }
    else
    {
        g_pGameLevel->Objects.net_Unregister(this);
    }

    if (this == Level().CurrentEntity())
    {
        if (!Level().IsDemoPlayStarted())
        {
            Level().SetControlEntity(0);
        }
        Level().SetEntity(0); // do not switch !!!
    }

    Level().RemoveObject_From_4CrPr(this);

    //.	Parent									= 0;

    scriptBinder.net_Destroy();

    xr_delete(m_lua_game_object);
    m_spawned = false;
}

void CGameObject::OnEvent(NET_Packet& P, u16 type)
{
    switch (type)
    {
    case GE_HIT:
    case GE_HIT_STATISTIC:
    {
        /*
                    u16				id,weapon_id;
                    Fvector			dir;
                    float			power, impulse;
                    s16				element;
                    Fvector			position_in_bone_space;
                    u16				hit_type;
                    float			ap = 0.0f;

                    P.r_u16			(id);
                    P.r_u16			(weapon_id);
                    P.r_dir			(dir);
                    P.r_float		(power);
                    P.r_s16			(element);
                    P.r_vec3		(position_in_bone_space);
                    P.r_float		(impulse);
                    P.r_u16			(hit_type);	//hit type
                    if ((ALife::EHitType)hit_type == ALife::eHitTypeFireWound)
                    {
                        P.r_float	(ap);
                    }

                    IGameObject*	Hitter = Level().Objects.net_Find(id);
                    IGameObject*	Weapon = Level().Objects.net_Find(weapon_id);

                    SHit	HDS = SHit(power, dir, Hitter, element, position_in_bone_space, impulse,
           (ALife::EHitType)hit_type, ap);
        */
        SHit HDS;
        HDS.PACKET_TYPE = type;
        HDS.Read_Packet_Cont(P);
        //			Msg("Hit received: %d[%d,%d]", HDS.whoID, HDS.weaponID, HDS.BulletID);
        IGameObject* Hitter = Level().Objects.net_Find(HDS.whoID);
        IGameObject* Weapon = Level().Objects.net_Find(HDS.weaponID);
        HDS.who = Hitter;
        if (!HDS.who)
        {
            Msg("! ERROR: hitter object [%d] is NULL on client.", HDS.whoID);
        }
        //-------------------------------------------------------
        switch (HDS.PACKET_TYPE)
        {
        case GE_HIT_STATISTIC:
        {
            if (GameID() != eGameIDSingle)
                Game().m_WeaponUsageStatistic->OnBullet_Check_Request(&HDS);
        }
        break;
        default: {
        }
        break;
        }
        SetHitInfo(Hitter, Weapon, HDS.bone(), HDS.p_in_bone_space, HDS.dir);
        Hit(&HDS);
        //---------------------------------------------------------------------------
        if (GameID() != eGameIDSingle)
        {
            Game().m_WeaponUsageStatistic->OnBullet_Check_Result(false);
            game_cl_mp* mp_game = smart_cast<game_cl_mp*>(&Game());
            if (mp_game->get_reward_generator())
                mp_game->get_reward_generator()->OnBullet_Hit(Hitter, this, Weapon, HDS.boneID);
        }
        //---------------------------------------------------------------------------
    }
    break;
    case GE_DESTROY:
    {
        if (H_Parent())
        {
            Msg("! ERROR (GameObject): GE_DESTROY arrived to object[%d][%s], that has parent[%d][%s], frame[%d]", ID(),
                cNameSect().c_str(), H_Parent()->ID(), H_Parent()->cName().c_str(), Device.dwFrame);

            // This object will be destroy on call function <H_Parent::Destroy>
            // or it will be call <H_Parent::Reject>  ==>  H_Parent = NULL
            // !!! ___ it is necessary to be check!
            break;
        }
#ifdef MP_LOGGING
        Msg("--- Object: GE_DESTROY of [%d][%s]", ID(), cNameSect().c_str());
#endif // MP_LOGGING

        setDestroy(TRUE);
        //			MakeMeCrow		();
    }
    break;
    }
}

void VisualCallback(IKinematics* tpKinematics);

BOOL CGameObject::net_Spawn(CSE_Abstract* DC)
{
    VERIFY(!m_spawned);
    m_spawned = true;
    m_spawn_time = Device.dwFrame;
    m_ai_obstacle = new ai_obstacle(this);

    CSE_Abstract* E = (CSE_Abstract*)DC;
    VERIFY(E);

    const CSE_Visual* visual = smart_cast<const CSE_Visual*>(E);
    if (visual)
    {
        cNameVisual_set(visual_name(E));
        if (visual->flags.test(CSE_Visual::flObstacle))
        {
            ISpatial* self = smart_cast<ISpatial*>(this);
            self->GetSpatialData().type |= STYPE_OBSTACLE;
        }
    }

    // Naming
    cName_set(E->s_name);
    cNameSect_set(E->s_name);
    if (E->name_replace()[0])
        cName_set(E->name_replace());
    bool demo_spectator = false;

    if (Level().IsDemoPlayStarted() && E->ID == u16(-1))
    {
        Msg("* Spawning demo spectator ...");
        demo_spectator = true;
    }
    else
    {
        R_ASSERT(Level().Objects.net_Find(E->ID) == NULL);
    }

    setID(E->ID);
    //	if (GameID() != eGameIDSingle)
    //		Msg ("CGameObject::net_Spawn -- object %s[%x] setID [%d]", *(E->s_name), this, E->ID);

    // XForm
    XFORM().setXYZ(E->o_Angle);
    Position().set(E->o_Position);
#ifdef DEBUG
    if (ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject) && stricmp(PH_DBG_ObjectTrackName(), *cName()) == 0)
    {
        Msg("CGameObject::net_Spawn obj %s Position set from CSE_Abstract %f,%f,%f", PH_DBG_ObjectTrackName(),
            Position().x, Position().y, Position().z);
    }
#endif
    VERIFY(_valid(renderable.xform));
    VERIFY(!fis_zero(DET(renderable.xform)));
    CSE_ALifeObject* O = smart_cast<CSE_ALifeObject*>(E);
    if (O && xr_strlen(O->m_ini_string))
    {
#pragma warning(push)
#pragma warning(disable : 4238)
        m_ini_file = new CInifile(
            &IReader((void*)(*(O->m_ini_string)), O->m_ini_string.size()), FS.get_path("$game_config$")->m_Path);
#pragma warning(pop)
    }

    m_story_id = ALife::_STORY_ID(-1);
    if (O)
        m_story_id = O->m_story_id;

    // Net params
    setLocal(E->s_flags.is(M_SPAWN_OBJECT_LOCAL));
    if (Level().IsDemoPlay()) //&& OnClient())
    {
        if (!demo_spectator)
        {
            setLocal(FALSE);
        }
    };

    setReady(TRUE);
    if (!demo_spectator)
        g_pGameLevel->Objects.net_Register(this);

    m_server_flags.one();
    if (O)
    {
        m_server_flags = O->m_flags;
        if (O->m_flags.is(CSE_ALifeObject::flVisibleForAI))
            spatial.type |= STYPE_VISIBLEFORAI;
        else
            spatial.type = (spatial.type | STYPE_VISIBLEFORAI) ^ STYPE_VISIBLEFORAI;
    }

    reload(*cNameSect());
    if (!g_dedicated_server)
        scriptBinder.reload(*cNameSect());

    reinit();
    if (!g_dedicated_server)
        scriptBinder.reinit();
#ifdef DEBUG
    if (ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject) && stricmp(PH_DBG_ObjectTrackName(), *cName()) == 0)
    {
        Msg("CGameObject::net_Spawn obj %s After Script Binder reinit %f,%f,%f", PH_DBG_ObjectTrackName(), Position().x,
            Position().y, Position().z);
    }
#endif
    // load custom user data from server
    if (!E->client_data.empty())
    {
        //		Msg				("client data is present for object [%d][%s], load is processed",ID(),*cName());
        IReader ireader = IReader(&*E->client_data.begin(), E->client_data.size());
        net_Load(ireader);
    }
    else
    {
        //		Msg				("no client data for object [%d][%s], load is skipped",ID(),*cName());
    }

    // if we have a parent
    if (ai().get_level_graph())
    {
        if (E->ID_Parent == 0xffff)
        {
            CSE_ALifeObject* l_tpALifeObject = smart_cast<CSE_ALifeObject*>(E);
            if (l_tpALifeObject && ai().level_graph().valid_vertex_id(l_tpALifeObject->m_tNodeID))
                ai_location().level_vertex(l_tpALifeObject->m_tNodeID);
            else
            {
                CSE_Temporary* l_tpTemporary = smart_cast<CSE_Temporary*>(E);
                if (l_tpTemporary && ai().level_graph().valid_vertex_id(l_tpTemporary->m_tNodeID))
                    ai_location().level_vertex(l_tpTemporary->m_tNodeID);
            }

            if (l_tpALifeObject && ai().game_graph().valid_vertex_id(l_tpALifeObject->m_tGraphID))
                ai_location().game_vertex(l_tpALifeObject->m_tGraphID);

            validate_ai_locations(false);

            // validating position
            if (UsedAI_Locations() && ai().level_graph().inside(ai_location().level_vertex_id(), Position()) &&
                can_validate_position_on_spawn())
                Position().y = EPS_L +
                    ai().level_graph().vertex_plane_y(*ai_location().level_vertex(), Position().x, Position().z);
        }
        else
        {
            CSE_ALifeObject* const alife_object = smart_cast<CSE_ALifeObject*>(E);
            if (alife_object && ai().level_graph().valid_vertex_id(alife_object->m_tNodeID))
            {
                ai_location().level_vertex(alife_object->m_tNodeID);
                ai_location().game_vertex(alife_object->m_tGraphID);
            }
        }
    }
    //
    PositionStack.clear();
    VERIFY(_valid(renderable.xform));
    if (0 == Visual() && pSettings->line_exist(cNameSect(), "visual"))
        cNameVisual_set(pSettings->r_string(cNameSect(), "visual"));
    if (0 == CForm)
    {
        if (pSettings->line_exist(cNameSect(), "cform"))
        {
            VERIFY3(*NameVisual, "Model isn't assigned for object, but cform requisted", *cName());
            CForm = new CCF_Skeleton(this);
        }
    }
    R_ASSERT(spatial.space);
    spatial_register();
    if (register_schedule())
        shedule_register();
    // reinitialize flags
    processing_activate();
    setDestroy(false);
    MakeMeCrow();
    // ~
    m_bObjectRemoved = false;
    spawn_supplies();
#ifdef DEBUG
    if (ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject) && stricmp(PH_DBG_ObjectTrackName(), *cName()) == 0)
    {
        Msg("CGameObject::net_Spawn obj %s Before CScriptBinder::net_Spawn %f,%f,%f", PH_DBG_ObjectTrackName(),
            Position().x, Position().y, Position().z);
    }
    BOOL ret = scriptBinder.net_Spawn(DC);
#else
    return (scriptBinder.net_Spawn(DC));
#endif

#ifdef DEBUG
    if (ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject) && stricmp(PH_DBG_ObjectTrackName(), *cName()) == 0)
    {
        Msg("CGameObject::net_Spawn obj %s Before CScriptBinder::net_Spawn %f,%f,%f", PH_DBG_ObjectTrackName(),
            Position().x, Position().y, Position().z);
    }
    return ret;
#endif
}

void CGameObject::net_Save(NET_Packet& net_packet)
{
    u32 position;
    net_packet.w_chunk_open16(position);
    save(net_packet);

// Script Binder Save ---------------------------------------
#ifdef DEBUG
    if (psAI_Flags.test(aiSerialize))
    {
        Msg(">> **** Save script object [%s] *****", *cName());
        Msg(">> Before save :: packet position = [%u]", net_packet.w_tell());
    }

#endif

    scriptBinder.save(net_packet);

#ifdef DEBUG

    if (psAI_Flags.test(aiSerialize))
    {
        Msg(">> After save :: packet position = [%u]", net_packet.w_tell());
    }
#endif

    // ----------------------------------------------------------

    net_packet.w_chunk_close16(position);
}

void CGameObject::net_Load(IReader& ireader)
{
    load(ireader);

// Script Binder Load ---------------------------------------
#ifdef DEBUG
    if (psAI_Flags.test(aiSerialize))
    {
        Msg(">> **** Load script object [%s] *****", *cName());
        Msg(">> Before load :: reader position = [%i]", ireader.tell());
    }

#endif

    scriptBinder.load(ireader);

#ifdef DEBUG

    if (psAI_Flags.test(aiSerialize))
    {
        Msg(">> After load :: reader position = [%i]", ireader.tell());
    }
#endif
// ----------------------------------------------------------
#ifdef DEBUG
    if (ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject) && stricmp(PH_DBG_ObjectTrackName(), *cName()) == 0)
    {
        Msg("CGameObject::net_Load obj %s (loaded) %f,%f,%f", PH_DBG_ObjectTrackName(), Position().x, Position().y,
            Position().z);
    }

#endif
}

void CGameObject::save(NET_Packet& output_packet) {}
void CGameObject::load(IReader& input_packet) {}
void CGameObject::spawn_supplies()
{
    if (!spawn_ini() || ai().get_alife())
        return;

    if (!spawn_ini()->section_exist("spawn"))
        return;

    LPCSTR N, V;
    float p;
    bool bScope = false;
    bool bSilencer = false;
    bool bLauncher = false;
    bool bMagaz = false; //--#SM+#--
    bool bSpec_1 = false; //--#SM+#--
    bool bSpec_2 = false; //--#SM+#--
    bool bSpec_3 = false; //--#SM+#--
    bool bSpec_4 = false; //--#SM+#--

    for (u32 k = 0, j; spawn_ini()->r_line("spawn", k, &N, &V); k++)
    {
        VERIFY(xr_strlen(N));
        j = 1;
        p = 1.f;

        float f_cond = 1.0f;
        if (V && xr_strlen(V))
        {
            int n = _GetItemCount(V);
            string16 temp;
            if (n > 0)
                j = atoi(_GetItem(V, 0, temp)); // count

            if (NULL != strstr(V, "prob="))
                p = (float)atof(strstr(V, "prob=") + 5);
            if (fis_zero(p))
                p = 1.f;
            if (!j)
                j = 1;
            if (NULL != strstr(V, "cond="))
                f_cond = (float)atof(strstr(V, "cond=") + 5);
            bScope = (NULL != strstr(V, "scope"));
            bSilencer = (NULL != strstr(V, "silencer"));
            bLauncher = (NULL != strstr(V, "launcher"));
            bMagaz = (NULL != strstr(V, "magaz")); //--#SM+#--
            bSpec_1 = (NULL != strstr(V, "special_1")); //--#SM+#--
            bSpec_2 = (NULL != strstr(V, "special_2")); //--#SM+#--
            bSpec_3 = (NULL != strstr(V, "special_3")); //--#SM+#--
            bSpec_4 = (NULL != strstr(V, "special_4")); //--#SM+#--
        }
        for (u32 i = 0; i < j; ++i)
            if (::Random.randF(1.f) < p)
            {
                CSE_Abstract* A = Level().spawn_item(N, Position(), ai_location().level_vertex_id(), ID(), true);

                CSE_ALifeInventoryItem* pSE_InventoryItem = smart_cast<CSE_ALifeInventoryItem*>(A);
                if (pSE_InventoryItem)
                    pSE_InventoryItem->m_fCondition = f_cond;

                CSE_ALifeItemWeapon* W = smart_cast<CSE_ALifeItemWeapon*>(A);
                if (W) //--#SM+#--
                {
                    // TODO: Возможность указывать конкретную секцию аддона в xml-е //SM_TODO - дубль код?
                    if (W->m_scope_status == ALife::eAddonAttachable && bScope)
                        W->m_scope_idx = 0;
                    if (W->m_silencer_status == ALife::eAddonAttachable && bSilencer)
                        W->m_muzzle_idx = 0;
                    if (W->m_grenade_launcher_status == ALife::eAddonAttachable && bLauncher)
                        W->m_launcher_idx = 0;
                    if (W->m_magazine_status == ALife::eAddonAttachable && bMagaz)
                        W->m_magaz_idx = 0;
                    if (W->m_spec_1_status == ALife::eAddonAttachable && bSpec_1)
                        W->m_spec_1_idx = 0;
                    if (W->m_spec_2_status == ALife::eAddonAttachable && bSpec_2)
                        W->m_spec_2_idx = 0;
                    if (W->m_spec_3_status == ALife::eAddonAttachable && bSpec_3)
                        W->m_spec_3_idx = 0;
                    if (W->m_spec_4_status == ALife::eAddonAttachable && bSpec_4)
                        W->m_spec_4_idx = 0;

                    W->AddonsUpdate();
                }

                NET_Packet P;
                A->Spawn_Write(P, TRUE);
                Level().Send(P, net_flags(TRUE));
                F_entity_Destroy(A);
            }
    }
}

void CGameObject::setup_parent_ai_locations(bool assign_position)
{
    //	CGameObject				*l_tpGameObject	= static_cast<CGameObject*>(H_Root());
    VERIFY(H_Parent());
    CGameObject* l_tpGameObject = smart_cast<CGameObject*>(H_Parent());
    VERIFY(l_tpGameObject);

    // get parent's position
    if (assign_position && use_parent_ai_locations())
        Position().set(l_tpGameObject->Position());

    // if ( assign_position &&
    //		( use_parent_ai_locations() &&
    //		!( cast_attachable_item() && cast_attachable_item()->enabled() )
    //		 )
    //	)
    //	Position().set		(l_tpGameObject->Position());

    // setup its ai locations
    if (!UsedAI_Locations())
        return;

    if (!ai().get_level_graph())
        return;

    if (l_tpGameObject->UsedAI_Locations() &&
        ai().level_graph().valid_vertex_id(l_tpGameObject->ai_location().level_vertex_id()))
        ai_location().level_vertex(l_tpGameObject->ai_location().level_vertex_id());
    else
        validate_ai_locations(false);
    //	VERIFY2						(l_tpGameObject->UsedAI_Locations(),*l_tpGameObject->cNameSect());
    //	VERIFY2
    //(ai().level_graph().valid_vertex_id(l_tpGameObject->ai_location().level_vertex_id()),*cNameSect());
    //	ai_location().level_vertex	(l_tpGameObject->ai_location().level_vertex_id());

    if (ai().game_graph().valid_vertex_id(l_tpGameObject->ai_location().game_vertex_id()))
        ai_location().game_vertex(l_tpGameObject->ai_location().game_vertex_id());
    else
        ai_location().game_vertex(ai().cross_table().vertex(ai_location().level_vertex_id()).game_vertex_id());
    //	VERIFY2
    //(ai().game_graph().valid_vertex_id(l_tpGameObject->ai_location().game_vertex_id()),*cNameSect());
    //	ai_location().game_vertex	(l_tpGameObject->ai_location().game_vertex_id());
}

u32 CGameObject::new_level_vertex_id() const
{
    Fvector center;
    Center(center);
    center.x = Position().x;
    center.z = Position().z;
    return (ai().level_graph().vertex(ai_location().level_vertex_id(), center));
}

void CGameObject::update_ai_locations(bool decrement_reference)
{
    u32 l_dwNewLevelVertexID = new_level_vertex_id();
    VERIFY(ai().level_graph().valid_vertex_id(l_dwNewLevelVertexID));
    if (decrement_reference && (ai_location().level_vertex_id() == l_dwNewLevelVertexID))
        return;

    ai_location().level_vertex(l_dwNewLevelVertexID);

    if (!ai().get_game_graph() && ai().get_cross_table())
        return;

    ai_location().game_vertex(ai().cross_table().vertex(ai_location().level_vertex_id()).game_vertex_id());
    VERIFY(ai().game_graph().valid_vertex_id(ai_location().game_vertex_id()));
}

void CGameObject::validate_ai_locations(bool decrement_reference)
{
    if (!ai().get_level_graph())
        return;

    if (!UsedAI_Locations())
        return;

    update_ai_locations(decrement_reference);
}

void CGameObject::spatial_register()
{
    Center(spatial.sphere.P);
    spatial.sphere.R = Radius();
    SpatialBase::spatial_register();
}

void CGameObject::spatial_unregister() { SpatialBase::spatial_unregister(); }
void CGameObject::spatial_move()
{
    if (H_Parent())
        setup_parent_ai_locations();
    else if (Visual())
        validate_ai_locations();
    //
    Center(spatial.sphere.P);
    spatial.sphere.R = Radius();
    SpatialBase::spatial_move();
}

void CGameObject::spatial_update(float eps_P, float eps_R)
{
    //
    BOOL bUpdate = FALSE;
    if (PositionStack.empty())
    {
        // Empty
        bUpdate = TRUE;
        PositionStack.push_back(GameObjectSavedPosition());
        PositionStack.back().dwTime = Device.dwTimeGlobal;
        PositionStack.back().vPosition = Position();
    }
    else
    {
        if (PositionStack.back().vPosition.similar(Position(), eps_P))
        {
            // Just update time
            PositionStack.back().dwTime = Device.dwTimeGlobal;
        }
        else
        {
            // Register _new_ record
            bUpdate = TRUE;
            if (PositionStack.size() < 4)
            {
                PositionStack.push_back(GameObjectSavedPosition());
            }
            else
            {
                PositionStack[0] = PositionStack[1];
                PositionStack[1] = PositionStack[2];
                PositionStack[2] = PositionStack[3];
            }
            PositionStack.back().dwTime = Device.dwTimeGlobal;
            PositionStack.back().vPosition = Position();
        }
    }

    if (bUpdate)
    {
        spatial_move();
    }
    else
    {
        if (spatial.node_ptr)
        {
            // Object registered!
            if (!fsimilar(Radius(), spatial.sphere.R, eps_R))
                spatial_move();
            else
            {
                Fvector C;
                Center(C);
                if (!C.similar(spatial.sphere.P, eps_P))
                    spatial_move();
            }
            // else nothing to do :_)
        }
    }
}

#ifdef DEBUG
void CGameObject::dbg_DrawSkeleton()
{
    CCF_Skeleton* Skeleton = smart_cast<CCF_Skeleton*>(CForm);
    if (!Skeleton)
        return;
    Skeleton->_dbg_refresh();

    const CCF_Skeleton::ElementVec& Elements = Skeleton->_GetElements();
    for (CCF_Skeleton::ElementVec::const_iterator I = Elements.begin(); I != Elements.end(); I++)
    {
        if (!I->valid())
            continue;
        switch (I->type)
        {
        case SBoneShape::stBox:
        {
            Fmatrix M;
            M.invert(I->b_IM);
            Fvector h_size = I->b_hsize;
            Level().debug_renderer().draw_obb(M, h_size, color_rgba(0, 255, 0, 255));
        }
        break;
        case SBoneShape::stCylinder:
        {
            Fmatrix M;
            M.c.set(I->c_cylinder.m_center);
            M.k.set(I->c_cylinder.m_direction);
            Fvector h_size;
            h_size.set(I->c_cylinder.m_radius, I->c_cylinder.m_radius, I->c_cylinder.m_height * 0.5f);
            Fvector::generate_orthonormal_basis(M.k, M.j, M.i);
            Level().debug_renderer().draw_obb(M, h_size, color_rgba(0, 127, 255, 255));
        }
        break;
        case SBoneShape::stSphere:
        {
            Fmatrix l_ball;
            l_ball.scale(I->s_sphere.R, I->s_sphere.R, I->s_sphere.R);
            l_ball.translate_add(I->s_sphere.P);
            Level().debug_renderer().draw_ellipse(l_ball, color_rgba(0, 255, 0, 255));
        }
        break;
        };
    };
}
#endif

IGameObject* CGameObject::H_SetParent(IGameObject* new_parent, bool just_before_destroy)
{
    if (new_parent == Parent)
        return new_parent;
    IGameObject* old_parent = Parent;
    VERIFY2(!new_parent || !old_parent, "Before set parent - execute H_SetParent(0)");
    // if (Parent) Parent->H_ChildRemove (this);
    if (!old_parent)
        OnH_B_Chield(); // before attach
    else
        OnH_B_Independent(just_before_destroy); // before detach
    if (new_parent)
        spatial_unregister();
    else
        spatial_register();
    Parent = new_parent;
    if (!old_parent)
        OnH_A_Chield(); // after attach
    else
        OnH_A_Independent(); // after detach
    // if (Parent) Parent->H_ChildAdd (this);
    MakeMeCrow();
    return old_parent;
}

void CGameObject::OnH_A_Chield() {}
void CGameObject::OnH_B_Chield()
{
    //
    setVisible(false);
    // ~
    /// PHSetPushOut();????
}

void CGameObject::OnH_A_Independent() { setVisible(true); }
void CGameObject::OnH_B_Independent(bool just_before_destroy)
{
    setup_parent_ai_locations(false);
    CGameObject* parent = smart_cast<CGameObject*>(H_Parent());
    VERIFY(parent);
    if (ai().get_level_graph() && ai().level_graph().valid_vertex_id(parent->ai_location().level_vertex_id()))
        validate_ai_locations(false);
}

void CGameObject::setDestroy(BOOL _destroy)
{
    if (_destroy == (BOOL)Props.bDestroy)
        return;
    Props.bDestroy = _destroy ? 1 : 0;
    if (_destroy)
    {
        g_pGameLevel->Objects.register_object_to_destroy(this);
#ifdef DEBUG
        if (debug_destroy)
            Msg("cl setDestroy [%d][%d]", ID(), Device.dwFrame);
#endif
#ifdef MP_LOGGING
        Msg("cl setDestroy [%d][%d]", ID(), Device.dwFrame);
#endif //#ifdef MP_LOGGING
    }
    else
        VERIFY(!g_pGameLevel->Objects.registered_object_to_destroy(this));
}

Fvector CGameObject::get_new_local_point_on_mesh(u16& bone_id) const
{
    bone_id = u16(-1);
    return Fvector().random_dir().mul(.7f);
}

Fvector CGameObject::get_last_local_point_on_mesh(Fvector const& local_point, u16 const bone_id) const
{
    VERIFY(bone_id == u16(-1));
    Fvector result;
    // Fetch data
    Fmatrix mE;
    const Fmatrix& M = XFORM();
    const Fbox& B = CForm->getBBox();
    // Build OBB + Ellipse and X-form point
    Fvector c, r;
    Fmatrix T, mR, mS;
    B.getcenter(c);
    B.getradius(r);
    T.translate(c);
    mR.mul_43(M, T);
    mS.scale(r);
    mE.mul_43(mR, mS);
    mE.transform_tiny(result, local_point);
    return result;
}

void CGameObject::renderable_Render()
{
    //
    MakeMeCrow();
    // ~
    GlobalEnv.Render->set_Transform(&XFORM());
    GlobalEnv.Render->add_Visual(Visual());
    Visual()->getVisData().hom_frame = Device.dwFrame;

    // Рендерим приатаченные визуалы --#SM+#--
    for (u32 i = 0; i < this->m_attached_visuals.size(); i++)
        this->m_attached_visuals[i]->Render();
}

/*
float CGameObject::renderable_Ambient	()
{
    return (ai().get_level_graph() && ai().level_graph().valid_vertex_id(level_vertex_id()) ?
float(level_vertex()->light()/15.f) : 1.f);
}
*/

GameObjectSavedPosition CGameObject::ps_Element(u32 ID) const
{
    VERIFY(ID < ps_Size());
    GameObjectSavedPosition SP = PositionStack[ID];
    SP.dwTime += Level().timeServer_Delta();
    return SP;
}

void CGameObject::u_EventGen(NET_Packet& P, u32 type, u32 dest)
{
    P.w_begin(M_EVENT);
    P.w_u32(Level().timeServer());
    P.w_u16(u16(type & 0xffff));
    P.w_u16(u16(dest & 0xffff));
}

void CGameObject::u_EventSend(NET_Packet& P, u32 dwFlags) { Level().Send(P, dwFlags); }
#include "bolt.h"

BOOL CGameObject::UsedAI_Locations() { return (m_server_flags.test(CSE_ALifeObject::flUsedAI_Locations)); }
BOOL CGameObject::TestServerFlag(u32 Flag) const { return (m_server_flags.test(Flag)); }
void CGameObject::add_visual_callback(visual_callback callback)
{
    VERIFY(smart_cast<IKinematics*>(Visual()));
    CALLBACK_VECTOR_IT I = std::find(visual_callbacks().begin(), visual_callbacks().end(), callback);
    if (I != visual_callbacks().end())
        return; //--#SM+#-- Код ниже не подразумевает наличие нескольких каллбэков у объекта
    // SM_TODO разберись вообще с этими колбеками, зачем тут вектор, если у скелета может быть лишь один каллбэк???

    if (m_visual_callback.empty())
        SetKinematicsCallback(true);
    //		smart_cast<IKinematics*>(Visual())->Callback(VisualCallback,this);
    m_visual_callback.push_back(callback);
}

void CGameObject::remove_visual_callback(visual_callback callback)
{
    CALLBACK_VECTOR_IT I = std::find(m_visual_callback.begin(), m_visual_callback.end(), callback);
    if (I == m_visual_callback.end())
        return; //--#SM+#-- SM_TODO
    m_visual_callback.erase(I);
    if (m_visual_callback.empty())
        SetKinematicsCallback(false);
    //		smart_cast<IKinematics*>(Visual())->Callback(0,0);
}

void CGameObject::SetKinematicsCallback(bool set)
{
    if (!Visual())
        return;
    if (set)
        smart_cast<IKinematics*>(Visual())->Callback(VisualCallback, this);
    else
        smart_cast<IKinematics*>(Visual())->Callback(0, 0);
};

void VisualCallback(IKinematics* tpKinematics)
{
    CGameObject* game_object =
        smart_cast<CGameObject*>(static_cast<IGameObject*>(tpKinematics->GetUpdateCallbackParam()));
    VERIFY(game_object);
    for (auto cb : game_object->visual_callbacks())
        cb(tpKinematics);
}

CScriptGameObject* CGameObject::lua_game_object() const
{
#ifdef DEBUG
    if (!m_spawned)
        Msg("! you are trying to use a destroyed object [%x]", this);
#endif
    THROW(m_spawned);
    if (!m_lua_game_object)
        m_lua_game_object = new CScriptGameObject(const_cast<CGameObject*>(this));
    return (m_lua_game_object);
}

bool CGameObject::NeedToDestroyObject() const { return false; }
void CGameObject::DestroyObject()
{
    if (m_bObjectRemoved)
        return;
    m_bObjectRemoved = true;
    if (getDestroy())
        return;

    if (Local())
    {
        NET_Packet P;
        u_EventGen(P, GE_DESTROY, ID());
        u_EventSend(P);
    }
}

void CGameObject::shedule_Update(u32 dt)
{
    //уничтожить
    if (NeedToDestroyObject())
    {
#ifndef MASTER_GOLD
        Msg("--NeedToDestroyObject for [%d][%d]", ID(), Device.dwFrame);
#endif // #ifndef MASTER_GOLD
        DestroyObject();
    }
    // Msg("-SUB-:[%x][%s] CGameObject::shedule_Update",smart_cast<void*>(this),*cName());
    // IGameObject::shedule_Update(dt);
    // consistency check
    // Msg ("-SUB-:[%x][%s] IGameObject::shedule_Update",dynamic_cast<void*>(this),*cName());
    ScheduledBase::shedule_Update(dt);
    spatial_update(base_spu_epsP * 1, base_spu_epsR * 1);
    // Always make me crow on shedule-update
    // Makes sure that update-cl called at least with freq of shedule-update
    MakeMeCrow();
    /*
    if (AlwaysTheCrow()) MakeMeCrow ();
    else if (Device.vCameraPosition.distance_to_sqr(Position()) < CROW_RADIUS*CROW_RADIUS) MakeMeCrow ();
    */

    //--#SM+ Begin#--
    // IRNV увеличиваем\уменьшаем теплоту
    if (m_common_values.m_fIRNV_value_max != m_common_values.m_fIRNV_value_min)
    {
        if (m_common_values.m_fIRNV_max_or_min)
            m_common_values.m_fIRNV_value += (dt * m_common_values.m_fIRNV_cooling_speed);
        else
            m_common_values.m_fIRNV_value -= (dt * m_common_values.m_fIRNV_cooling_speed);

        clamp(m_common_values.m_fIRNV_value, m_common_values.m_fIRNV_value_min, m_common_values.m_fIRNV_value_max);
    }
    //--#SM+ End#--

    // ~
    if (!g_dedicated_server)
        scriptBinder.shedule_Update(dt);
}

BOOL CGameObject::net_SaveRelevant() { return scriptBinder.net_SaveRelevant(); }
//игровое имя объекта
LPCSTR CGameObject::Name() const { return (*cName()); }
u32 CGameObject::ef_creature_type() const
{
    string16 temp;
    CLSID2TEXT(CLS_ID, temp);
    R_ASSERT3(false, "Invalid creature type request, virtual function is not properly overridden!", temp);
    return (u32(-1));
}

u32 CGameObject::ef_equipment_type() const
{
    string16 temp;
    CLSID2TEXT(CLS_ID, temp);
    R_ASSERT3(false, "Invalid equipment type request, virtual function is not properly overridden!", temp);
    return (u32(-1));
    //	return		(6);
}

u32 CGameObject::ef_main_weapon_type() const
{
    string16 temp;
    CLSID2TEXT(CLS_ID, temp);
    R_ASSERT3(false, "Invalid main weapon type request, virtual function is not properly overridden!", temp);
    return (u32(-1));
    //	return		(5);
}

u32 CGameObject::ef_anomaly_type() const
{
    string16 temp;
    CLSID2TEXT(CLS_ID, temp);
    R_ASSERT3(false, "Invalid anomaly type request, virtual function is not properly overridden!", temp);
    return (u32(-1));
}

u32 CGameObject::ef_weapon_type() const
{
    string16 temp;
    CLSID2TEXT(CLS_ID, temp);
    R_ASSERT3(false, "Invalid weapon type request, virtual function is not properly overridden!", temp);
    return (u32(-1));
    //	return		(u32(0));
}

u32 CGameObject::ef_detector_type() const
{
    string16 temp;
    CLSID2TEXT(CLS_ID, temp);
    R_ASSERT3(false, "Invalid detector type request, virtual function is not properly overridden!", temp);
    return (u32(-1));
}

void CGameObject::net_Relcase(IGameObject* O)
{
    if (!g_dedicated_server)
        scriptBinder.net_Relcase(O);
}

CGameObject::CScriptCallbackExVoid& CGameObject::callback(GameObject::ECallbackType type) const
{
    return ((*m_callbacks)[type]);
}

LPCSTR CGameObject::visual_name(CSE_Abstract* server_entity)
{
    const CSE_Visual* visual = smart_cast<const CSE_Visual*>(server_entity);
    VERIFY(visual);
    return (visual->get_visual());
}
bool CGameObject::animation_movement_controlled() const
{
    return !!animation_movement() && animation_movement()->IsActive();
}

void CGameObject::update_animation_movement_controller()
{
    if (!m_anim_mov_ctrl)
        return;

    if (m_anim_mov_ctrl->IsActive())
    {
        m_anim_mov_ctrl->OnFrame();
        return;
    }

    destroy_anim_mov_ctrl();
}

bool CGameObject::is_ai_obstacle() const { return (false); }
void CGameObject::OnChangeVisual()
{
    if (m_anim_mov_ctrl)
        destroy_anim_mov_ctrl();
}

// Каллбэк на смену визуала в одном из присоединённых доп. визуалов  --#SM+#--
void CGameObject::OnAdditionalVisualModelChange(attachable_visual* pChangedVisual) {}

bool CGameObject::shedule_Needed() { return (!getDestroy()); }
void CGameObject::create_anim_mov_ctrl(CBlend* b, Fmatrix* start_pose, bool local_animation)
{
    if (animation_movement_controlled())
    {
        m_anim_mov_ctrl->NewBlend(b, start_pose ? *start_pose : XFORM(), local_animation);
    }
    else
    {
        //		start_pose		= &renderable.xform;
        if (m_anim_mov_ctrl)
            destroy_anim_mov_ctrl();

        VERIFY2(start_pose,
            make_string("start pose hasn't been specified for animation [%s][%s]",
                smart_cast<IKinematicsAnimated&>(*Visual()).LL_MotionDefName_dbg(b->motionID).first,
                smart_cast<IKinematicsAnimated&>(*Visual()).LL_MotionDefName_dbg(b->motionID).second));

        VERIFY2(!animation_movement(),
            make_string("start pose hasn't been specified for animation [%s][%s]",
                smart_cast<IKinematicsAnimated&>(*Visual()).LL_MotionDefName_dbg(b->motionID).first,
                smart_cast<IKinematicsAnimated&>(*Visual()).LL_MotionDefName_dbg(b->motionID).second));

        VERIFY(Visual());
        IKinematics* K = Visual()->dcast_PKinematics();
        VERIFY(K);

        m_anim_mov_ctrl = new animation_movement_controller(&XFORM(), *start_pose, K, b);
    }
}

void CGameObject::destroy_anim_mov_ctrl() { xr_delete(m_anim_mov_ctrl); }
IC bool similar(const Fmatrix& _0, const Fmatrix& _1, const float& epsilon = EPS)
{
    if (!_0.i.similar(_1.i, epsilon))
        return (false);

    if (!_0.j.similar(_1.j, epsilon))
        return (false);

    if (!_0.k.similar(_1.k, epsilon))
        return (false);

    if (!_0.c.similar(_1.c, epsilon))
        return (false);

    // note: we do not compare projection here
    return (true);
}

void CGameObject::UpdateCL()
{
// IGameObject::UpdateCL();
// consistency check
#ifdef DEBUG
    VERIFY2(_valid(renderable.xform), *cName());
    if (Device.dwFrame == dbg_update_cl)
        xrDebug::Fatal(DEBUG_INFO, "'UpdateCL' called twice per frame for %s", *cName());
    dbg_update_cl = Device.dwFrame;
    if (Parent && spatial.node_ptr)
        xrDebug::Fatal(DEBUG_INFO, "Object %s has parent but is still registered inside spatial DB", *cName());
    if (!CForm && (spatial.type & STYPE_COLLIDEABLE))
        xrDebug::Fatal(DEBUG_INFO, "Object %s registered as 'collidable' but has no collidable model", *cName());
#endif
    spatial_update(base_spu_epsP * 5, base_spu_epsR * 5);
    // crow
    if (Parent == g_pGameLevel->CurrentViewEntity() || AlwaysTheCrow())
        MakeMeCrow();
    else
    {
        float dist = Device.vCameraPosition.distance_to_sqr(Position());
        if (dist < CROW_RADIUS * CROW_RADIUS)
            MakeMeCrow();
        else if ((Visual() && Visual()->getVisData().hom_frame + 2 > Device.dwFrame) &&
            dist < CROW_RADIUS2 * CROW_RADIUS2)
            MakeMeCrow();
    }
    // ~
    //	if (!is_ai_obstacle())
    //		return;

    for (u32 i = 0; i < m_attached_visuals.size(); i++) //--#SM+#--
        m_attached_visuals[i]->Update();

    if (H_Parent())
        return;

    if (similar(XFORM(), m_previous_matrix, EPS))
        return;

    on_matrix_change(m_previous_matrix);
    m_previous_matrix = XFORM();
}

// Вызывается на каждом кадре после\вместо UpdateCL --#SM+#--
void CGameObject::PostUpdateCL(bool bUpdateCL_disabled)
{
    if (!g_dedicated_server)
    {
        // Обновляем присоединённые визуалы
        for (u32 i = 0; i < this->m_attached_visuals.size(); i++)
            this->m_attached_visuals[i]->Update();

        // Обновляем шейдерные данные
        IRenderVisual* _visual = this->Visual();
        if (_visual != NULL)
        {
            //Обновляем параметры объекта для шейдеров
            CEntity* entity_item = this->cast_entity();
            if (entity_item)
                _visual->getVisData().obj_data->sh_entity_data.x =
                    entity_item->GetfHealth(); // Обновляем инфу о здоровье объекта

            CEntityAlive* ealive_item = this->cast_entity_alive();
            if (ealive_item)
                _visual->getVisData().obj_data->sh_entity_data.y =
                    ealive_item->conditions().radiation(); // Обновляем инфу о радиации объекта

            CInventoryItem* inv_item = this->cast_inventory_item();
            if (inv_item)
                _visual->getVisData().obj_data->sh_entity_data.z =
                    inv_item->GetCondition(); // Обновляем инфу о износе объекта

            _visual->getVisData().obj_data->sh_entity_data.w =
                m_common_values.m_fIRNV_value; // Обновляем инфу о тепло-излучении объекта
        }
    }
}

void CGameObject::on_matrix_change(const Fmatrix& previous) { obstacle().on_move(); }
#ifdef DEBUG

void render_box(
    IRenderVisual* visual, const Fmatrix& xform, const Fvector& additional, bool draw_child_boxes, const u32& color)
{
    CDebugRenderer& renderer = Level().debug_renderer();
    IKinematics* kinematics = smart_cast<IKinematics*>(visual);
    VERIFY(kinematics);
    u16 bone_count = kinematics->LL_BoneCount();
    VERIFY(bone_count);
    u16 visible_bone_count = kinematics->LL_VisibleBoneCount();
    if (!visible_bone_count)
        return;

    Fmatrix matrix;
    Fvector* points = (Fvector*)_alloca(visible_bone_count * 8 * sizeof(Fvector));
    Fvector* I = points;
    for (u16 i = 0; i < bone_count; ++i)
    {
        if (!kinematics->LL_GetBoneVisible(i))
            continue;

        const Fobb& obb = kinematics->LL_GetData(i).obb;
        if (fis_zero(obb.m_halfsize.square_magnitude()))
        {
            VERIFY(visible_bone_count > 1);
            --visible_bone_count;
            continue;
        }

        Fmatrix Mbox;
        obb.xform_get(Mbox);

        const Fmatrix& Mbone = kinematics->LL_GetBoneInstance(i).mTransform;
        Fmatrix X;
        matrix.mul_43(xform, X.mul_43(Mbone, Mbox));

        Fvector half_size = Fvector().add(obb.m_halfsize, additional);
        matrix.mulB_43(Fmatrix().scale(half_size));

        if (draw_child_boxes)
            renderer.draw_obb(matrix, color);

        static const Fvector local_points[8] = {Fvector().set(-1.f, -1.f, -1.f), Fvector().set(-1.f, -1.f, +1.f),
            Fvector().set(-1.f, +1.f, +1.f), Fvector().set(-1.f, +1.f, -1.f), Fvector().set(+1.f, +1.f, +1.f),
            Fvector().set(+1.f, +1.f, -1.f), Fvector().set(+1.f, -1.f, +1.f), Fvector().set(+1.f, -1.f, -1.f)};

        for (u32 i = 0; i < 8; ++i, ++I)
            matrix.transform_tiny(*I, local_points[i]);
    }

    VERIFY(visible_bone_count);
    if (visible_bone_count == 1)
    {
        renderer.draw_obb(matrix, color);
        return;
    }

    VERIFY((I - points) == (visible_bone_count * 8));
    MagicBox3 box = MagicMinBox(visible_bone_count * 8, points);
    box.ComputeVertices(points);

    Fmatrix result;
    result.identity();

    result.c = box.Center();

    result.i.sub(points[3], points[2]).normalize();
    result.j.sub(points[2], points[1]).normalize();
    result.k.sub(points[2], points[6]).normalize();

    Fvector scale;
    scale.x = points[3].distance_to(points[2]) * .5f;
    scale.y = points[2].distance_to(points[1]) * .5f;
    scale.z = points[2].distance_to(points[6]) * .5f;
    result.mulB_43(Fmatrix().scale(scale));

    renderer.draw_obb(result, color);
}

void CGameObject::OnRender()
{
    if (!ai().get_level_graph())
        return;

    CDebugRenderer& renderer = Level().debug_renderer();
    if (/**bDebug && /**/ Visual())
    {
        float half_cell_size = 1.f * ai().level_graph().header().cell_size() * .5f;
        Fvector additional = Fvector().set(half_cell_size, half_cell_size, half_cell_size);

        render_box(Visual(), XFORM(), Fvector().set(0.f, 0.f, 0.f), true, color_rgba(0, 0, 255, 255));
        render_box(Visual(), XFORM(), additional, false, color_rgba(0, 255, 0, 255));
    }

    if (0)
    {
        Fvector bc, bd;
        Visual()->getVisData().box.get_CD(bc, bd);
        Fmatrix M = Fidentity;
        float half_cell_size = ai().level_graph().header().cell_size() * .5f;
        bd.add(Fvector().set(half_cell_size, half_cell_size, half_cell_size));
        M.scale(bd);
        Fmatrix T = XFORM();
        T.c.add(bc);
        renderer.draw_obb(T, bd, color_rgba(255, 255, 255, 255));
    }
}
#endif // DEBUG

using namespace luabind; // XXX: is it required here?

bool CGameObject::use(IGameObject* obj)
{
    VERIFY(obj);
    CScriptGameObject* scriptObj = lua_game_object();
    if (scriptObj && scriptObj->m_door)
    {
        doors::door* door = scriptObj->m_door;
        if (door->is_blocked(doors::door_state_open) || door->is_blocked(doors::door_state_closed))
            return false;
    }
    callback(GameObject::eUseObject)(scriptObj, obj->lua_game_object());
    return true;
}

LPCSTR CGameObject::tip_text() { return *m_sTipText; }
void CGameObject::set_tip_text(LPCSTR new_text) { m_sTipText = new_text; }
void CGameObject::set_tip_text_default() { m_sTipText = NULL; }
bool CGameObject::nonscript_usable() { return m_bNonscriptUsable; }
void CGameObject::set_nonscript_usable(bool usable) { m_bNonscriptUsable = usable; }

// Присоединить дополнительный визуал к главному --#SM+#--
bool CGameObject::AttachAdditionalVisual(const shared_str& sect_name, attachable_visual** pOut)
{
    attachable_visual* pFindVis = FindAdditionalVisual(sect_name);

    if (pFindVis != NULL)
    {
        if (pOut != NULL) *pOut = pFindVis;
        return false;
    }

    attachable_visual* vis = new attachable_visual(this, sect_name);
    m_attached_visuals.push_back(vis);

    if (pOut != NULL) *pOut = vis;
    return true;
}

// Отсоединить дополнительный визуал от главного --#SM+#--
bool CGameObject::DetachAdditionalVisual(const shared_str& sect_name)
{
    xr_vector<attachable_visual*>::iterator it_child;
    attachable_visual* vis = FindAdditionalVisual(sect_name, &it_child);
    if (vis == NULL)
        return false;

    m_attached_visuals.erase(it_child);
    xr_delete(vis);

    return true;
}

// Найти присоединённый визуал по его секции (не ищет внутри самих визуалов <!>) --#SM+#--
attachable_visual* CGameObject::FindAdditionalVisual(
    const shared_str& sect_name, xr_vector<attachable_visual*>::iterator* it_child)
{
    xr_vector<attachable_visual*>::iterator it = m_attached_visuals.begin();
    while (it != m_attached_visuals.end())
    {
        attachable_visual* vis = (*it);
        if (vis->m_sect_name.equal(sect_name))
        {
            if (it_child != NULL)
                *it_child = it;
            return vis;
        }
        else
            ++it;
    }
    return NULL;
}

// Получить список всех визуалов, связанных с нашим объектом --#SM+#--
void CGameObject::GetAllInheritedVisuals(xr_vector<IRenderVisual*>& tOutVisList)
{
    // Заносим свой основной визуал
    if (renderable.visual != NULL)
        tOutVisList.push_back(renderable.visual);

    // Заносим все визуалы из m_attached_visuals
    if (m_attached_visuals.size() > 0)
    {
        xr_vector<attachable_visual*> tAVisList;

        // Получаем список всех attachable_visual связанных с нами
        for (u32 i = 0; i < m_attached_visuals.size(); i++)
        {
            attachable_visual* pAVis = m_attached_visuals[i];
            if (pAVis != NULL)
            {
                // Заносим его в общий лист
                tAVisList.push_back(pAVis);

                // А также заносим всех его детей
                pAVis->GetAllInheritedAVisuals(tAVisList);
            }
        }

        // Теперь считываем модель из каждого attachable_visual
        for (u32 i = 0; i < tAVisList.size(); i++)
        {
            IRenderVisual* pVis = (tAVisList[i]->m_model != NULL ? tAVisList[i]->m_model->dcast_RenderVisual() : NULL);
            if (pVis != NULL)
                tOutVisList.push_back(pVis);
        }
    }
}
