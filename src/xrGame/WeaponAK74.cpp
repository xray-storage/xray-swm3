#include "pch_script.h"
#include "WeaponAK74.h"
#include "xrScriptEngine/ScriptExporter.hpp"

//--#SM+#--
CWeaponAK74::CWeaponAK74(ESoundTypes eSoundType) {}
CWeaponAK74::~CWeaponAK74() {}
using namespace luabind;

SCRIPT_EXPORT(CWeaponAK74, (CGameObject),
{
    module(luaState)
    [
        class_<CWeaponAK74,CGameObject>("CWeaponAK74")
        .def(constructor<>())
    ];
});