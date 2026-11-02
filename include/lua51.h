#pragma once
#ifndef LUA51_H
#define LUA51_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lua_State lua_State;
typedef int (__cdecl *lua_CFunction)(lua_State *L);
typedef double lua_Number;
typedef ptrdiff_t lua_Integer;

#define LUA_REGISTRYINDEX (-10000)
#define LUA_ENVIRONINDEX  (-10001)
#define LUA_GLOBALSINDEX  (-10002)
#define lua_upvalueindex(i) (LUA_GLOBALSINDEX - (i))

#define LUA_MULTRET (-1)

#define LUA_TNIL           0
#define LUA_TBOOLEAN       1
#define LUA_TLIGHTUSERDATA 2
#define LUA_TNUMBER        3
#define LUA_TSTRING        4
#define LUA_TTABLE         5
#define LUA_TFUNCTION      6
#define LUA_TUSERDATA      7
#define LUA_TTHREAD        8

typedef struct luaL_Reg
{
    const char *name;
    lua_CFunction func;
} luaL_Reg;

// core Lua 5.1 C API imported from lua51.dll
int __cdecl lua_gettop(lua_State *L);
void __cdecl lua_settop(lua_State *L, int idx);
void __cdecl lua_pushvalue(lua_State *L, int idx);
void __cdecl lua_remove(lua_State *L, int idx);
void __cdecl lua_insert(lua_State *L, int idx);

int __cdecl lua_isnumber(lua_State *L, int idx);
int __cdecl lua_isstring(lua_State *L, int idx);
int __cdecl lua_type(lua_State *L, int idx);
const char *__cdecl lua_typename(lua_State *L, int tp);

lua_Number __cdecl lua_tonumber(lua_State *L, int idx);
lua_Integer __cdecl lua_tointeger(lua_State *L, int idx);
int __cdecl lua_toboolean(lua_State *L, int idx);
const char *__cdecl lua_tolstring(lua_State *L, int idx, size_t *len);
size_t __cdecl lua_objlen(lua_State *L, int idx);
void *__cdecl lua_touserdata(lua_State *L, int idx);

void __cdecl lua_pushnil(lua_State *L);
void __cdecl lua_pushnumber(lua_State *L, lua_Number n);
void __cdecl lua_pushinteger(lua_State *L, lua_Integer n);
void __cdecl lua_pushstring(lua_State *L, const char *s);
void __cdecl lua_pushcclosure(lua_State *L, lua_CFunction fn, int n);
void __cdecl lua_pushboolean(lua_State *L, int b);
void __cdecl lua_pushlightuserdata(lua_State *L, void *p);

void __cdecl lua_getfield(lua_State *L, int idx, const char *k);
void __cdecl lua_rawgeti(lua_State *L, int idx, int n);
void __cdecl lua_createtable(lua_State *L, int narr, int nrec);

void __cdecl lua_settable(lua_State *L, int idx);
void __cdecl lua_setfield(lua_State *L, int idx, const char *k);
void __cdecl lua_rawseti(lua_State *L, int idx, int n);

void __cdecl lua_call(lua_State *L, int nargs, int nresults);
int __cdecl lua_next(lua_State *L, int idx);

void __cdecl luaL_register(lua_State *L, const char *libname, const luaL_Reg *l);
int __cdecl luaL_loadfile(lua_State *L, const char *filename);
int __cdecl luaL_loadstring(lua_State *L, const char *s);

#define lua_pop(L, n)            lua_settop(L, -(n) - 1)
#define lua_newtable(L)          lua_createtable(L, 0, 0)
#define lua_pushcfunction(L, f)  lua_pushcclosure(L, (f), 0)
#define lua_tostring(L, i)       lua_tolstring(L, (i), NULL)
#define lua_getglobal(L, s)      lua_getfield(L, LUA_GLOBALSINDEX, (s))
#define lua_setglobal(L, s)      lua_setfield(L, LUA_GLOBALSINDEX, (s))

#define lua_isfunction(L, n)     (lua_type(L, (n)) == LUA_TFUNCTION)
#define lua_istable(L, n)        (lua_type(L, (n)) == LUA_TTABLE)
#define lua_islightuserdata(L,n) (lua_type(L, (n)) == LUA_TLIGHTUSERDATA)
#define lua_isnil(L, n)          (lua_type(L, (n)) == LUA_TNIL)
#define lua_isboolean(L, n)      (lua_type(L, (n)) == LUA_TBOOLEAN)
#define lua_isnone(L, n)         (lua_type(L, (n)) == (-1))
#define lua_isnoneornil(L, n)    (lua_type(L, (n)) <= 0)

#ifdef __cplusplus
}
#endif

#endif // LUA51_H

