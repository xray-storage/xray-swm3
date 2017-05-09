#include "pch_script.h"
#include "WeaponHPSA.h"
#include "xrScriptEngine/ScriptExporter.hpp"

//--#SM+#--
CWeaponHPSA::CWeaponHPSA() {}
CWeaponHPSA::~CWeaponHPSA() {}

using namespace luabind;

SCRIPT_EXPORT(CWeaponHPSA, (CGameObject), { module(luaState)[class_<CWeaponHPSA, CGameObject>("CWeaponHPSA").def(constructor<>())]; });
