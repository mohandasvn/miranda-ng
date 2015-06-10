#ifndef _COMMON_H_
#define _COMMON_H_

#include <windows.h>

#include <newpluginapi.h>
#include <m_core.h>
#include <m_utils.h>
#include <m_langpack.h>
#include <m_folders.h>
#include <m_clist.h>
#include <m_genmenu.h>
#include <m_icolib.h>
#include <m_skin.h>

extern "C"
{
	#include "lua\lua.h"
	#include "lua\lualib.h"
	#include "lua\lauxlib.h"
}

#include "version.h"
#include "resource.h"

class CMLua;

#include "mlua.h"

#define MODULE "MirLua"

extern HINSTANCE g_hInstance;

#ifdef _UNICODE
	#define COMMON_SCRIPTS_PATHT MIRANDA_PATHW L"\\Scripts"
	#define CUSTOM_SCRIPTS_PATHT MIRANDA_USERDATAW L"\\Scripts"
#else
	#define COMMON_SCRIPTS_PATHT MIRANDA_PATH "\\Scripts"
	#define CUSTOM_SCRIPTS_PATHT MIRANDA_USERDATA "\\Scripts"
#endif

#define M_ICONSLIBNAME	"m_icons"
int luaopen_m_icons(lua_State *L);

#define M_MENUSLIBNAME	"m_menus"
int luaopen_m_menus(lua_State *L);

#endif //_COMMON_H_