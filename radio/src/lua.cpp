/*
 * Authors (alphabetical order)
 * - Andre Bernet <bernet.andre@gmail.com>
 * - Andreas Weitl
 * - Bertrand Songis <bsongis@gmail.com>
 * - Bryan J. Rentoul (Gruvin) <gruvin@gmail.com>
 * - Cameron Weeks <th9xer@gmail.com>
 * - Erez Raviv
 * - Gabriel Birkus
 * - Jean-Pierre Parisy
 * - Karl Szmutny
 * - Michael Blandford
 * - Michal Hlavinka
 * - Pat Mackenzie
 * - Philip Moss
 * - Rob Thomson
 * - Romolo Manfredini <romolo.manfredini@gmail.com>
 * - Thomas Husterer
 *
 * opentx is based on code named
 * gruvin9x by Bryan J. Rentoul: http://code.google.com/p/gruvin9x/,
 * er9x by Erez Raviv: http://code.google.com/p/er9x/,
 * and the original (and ongoing) project by
 * Thomas Husterer, th9x: http://code.google.com/p/th9x/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <ctype.h>
#include <stdio.h>
#include "opentx.h"
#include "stamp-opentx.h"
#include "timers.h"
#include "bin_allocator.h"

#if !defined(SIMU)
extern "C" {
#endif
  #include <lua.h>
  #include <lauxlib.h>
  #include <lualib.h>
  #include <lrotable.h>
#if !defined(SIMU)
}
#endif

#include "lua_exports.cpp"   // this line must be after lua headers

#define lua_registernumber(L, n, i)    (lua_pushnumber(L, (i)), lua_setglobal(L, (n)))
#define lua_registerint(L, n, i)       (lua_pushinteger(L, (i)), lua_setglobal(L, (n)))
#define lua_pushtablenil(L, k)         (lua_pushstring(L, (k)), lua_pushnil(L), lua_settable(L, -3))
#define lua_pushtableboolean(L, k, v)  (lua_pushstring(L, (k)), lua_pushboolean(L, (v)), lua_settable(L, -3))
#define lua_pushtableinteger(L, k, v)  (lua_pushstring(L, (k)), lua_pushinteger(L, (v)), lua_settable(L, -3))
#define lua_pushtablenumber(L, k, v)   (lua_pushstring(L, (k)), lua_pushnumber(L, (v)), lua_settable(L, -3))
#define lua_pushtablestring(L, k, v)   (lua_pushstring(L, (k)), lua_pushstring(L, (v)), lua_settable(L, -3))
#define lua_pushtablenzstring(L, k, v) { char tmp[sizeof(v)+1]; strncpy(tmp, (v), sizeof(v)); tmp[sizeof(v)] = '\0'; lua_pushstring(L, (k)); lua_pushstring(L, tmp); lua_settable(L, -3); }
#define lua_pushtablezstring(L, k, v)  { char tmp[sizeof(v)+1]; zchar2str(tmp, (v), sizeof(v)); lua_pushstring(L, (k)); lua_pushstring(L, tmp); lua_settable(L, -3); }
#define lua_registerlib(L, name, tab)  (luaL_newmetatable(L, name), luaL_setfuncs(L, tab, 0), lua_setglobal(L, name))

lua_State *L = NULL;
uint8_t luaState = 0;
uint8_t luaScriptsCount = 0;
ScriptInternalData scriptInternalData[MAX_SCRIPTS] = { { SCRIPT_NOFILE, 0 } };
ScriptInputsOutputs scriptInputsOutputs[MAX_SCRIPTS] = { {0} };
ScriptInternalData standaloneScript = { SCRIPT_NOFILE, 0 };
uint16_t maxLuaInterval = 0;
uint16_t maxLuaDuration = 0;

#define PERMANENT_SCRIPTS_MAX_INSTRUCTIONS (10000/100)
#define MANUAL_SCRIPTS_MAX_INSTRUCTIONS    (20000/100)
#define SET_LUA_INSTRUCTIONS_COUNT(x)      (instructionsPercent=0, lua_sethook(L, hook, LUA_MASKCOUNT, x))

struct our_longjmp * global_lj = 0;

/* custom panic handler */
static int custom_lua_atpanic(lua_State *lua)
{
  TRACE("PANIC: unprotected error in call to Lua API (%s)\n", lua_tostring(L, -1));
  if (global_lj) {
    longjmp(global_lj->b, 1);
    /* will never return */
  }
  return 0;
}

static int instructionsPercent = 0;
void hook(lua_State* L, lua_Debug *ar)
{
  instructionsPercent++;
  if (instructionsPercent > 100) {
    // From now on, as soon as a line is executed, error
    // keep erroring until you're script reaches the top
    lua_sethook(L, hook, LUA_MASKLINE, 0);
    luaL_error(L, "");
  }
}

static int luaGetVersion(lua_State *L)
{
  lua_pushstring(L, VERS_STR);
  return 1;
}

static int luaGetTime(lua_State *L)
{
  lua_pushunsigned(L, get_tmr10ms());
  return 1;
}

static void luaGetValueAndPush(int src)
{
  if (src >= MIXSRC_FIRST_TELEM && src <= MIXSRC_LAST_TELEM) {
    // telemetry values
   if ((src != MIXSRC_FIRST_TELEM-1+TELEM_TX_VOLTAGE) && !TELEMETRY_STREAMING()) {
      // telemetry not working, return zero for telemetry sources
      // except for "tx voltage"
      lua_pushinteger(L, (int)0);
      return;
    }
  }

  int idx = src;
  switch (src) {
    case MIXSRC_FIRST_TELEM-1+TELEM_TX_VOLTAGE:
    case MIXSRC_FIRST_TELEM-1+TELEM_VFAS:
    case MIXSRC_FIRST_TELEM-1+TELEM_MIN_VFAS:
    case MIXSRC_FIRST_TELEM-1+TELEM_CELLS_SUM:
    case MIXSRC_FIRST_TELEM-1+TELEM_MIN_CELLS_SUM:
    case MIXSRC_FIRST_TELEM-1+TELEM_CURRENT:
    case MIXSRC_FIRST_TELEM-1+TELEM_MAX_CURRENT:
    case MIXSRC_FIRST_TELEM-1+TELEM_ASPEED:
    case MIXSRC_FIRST_TELEM-1+TELEM_MAX_ASPEED:
      // these need to be divided by 10
      lua_pushnumber(L, getValue(src)/10.0);
      break;

    case MIXSRC_FIRST_TELEM-1+TELEM_MIN_A1:
    case MIXSRC_FIRST_TELEM-1+TELEM_MIN_A2:
    case MIXSRC_FIRST_TELEM-1+TELEM_MIN_A3:
    case MIXSRC_FIRST_TELEM-1+TELEM_MIN_A4:
      idx -= TELEM_MIN_A1-TELEM_A1;
      // no break
    case MIXSRC_FIRST_TELEM-1+TELEM_A1:
    case MIXSRC_FIRST_TELEM-1+TELEM_A2:
    case MIXSRC_FIRST_TELEM-1+TELEM_A3:
    case MIXSRC_FIRST_TELEM-1+TELEM_A4:
      // convert raw Ax values to calibrated values
      idx -= (MIXSRC_FIRST_TELEM+TELEM_A1-1);
      lua_pushnumber(L, applyChannelRatio(idx, getValue(src))/100.0);
      break;

    case MIXSRC_FIRST_TELEM-1+TELEM_CELL:
    case MIXSRC_FIRST_TELEM-1+TELEM_MIN_CELL:
    case MIXSRC_FIRST_TELEM-1+TELEM_ALT:
    case MIXSRC_FIRST_TELEM-1+TELEM_ACCx:
    case MIXSRC_FIRST_TELEM-1+TELEM_ACCy:
    case MIXSRC_FIRST_TELEM-1+TELEM_ACCz:
    case MIXSRC_FIRST_TELEM-1+TELEM_VSPEED:
      // these need to be divided by 100
      lua_pushnumber(L, getValue(src)/100.0);
      break;

    //TODO: add other values that need special treatment

    default:
      lua_pushinteger(L, getValue(src));
  }
}

struct LuaField {
  uint16_t id;
  char desc[50];
};

#define FIND_FIELD_DESC  0x01

/**
  Return field data for a given field name
*/
bool luaFindFieldByName(const char * name, LuaField & field, unsigned int flags=0)
{
  // TODO better search method (binary lookup)
  for (unsigned int n=0; n<DIM(luaSingleFields); ++n) {
    if (!strcmp(name, luaSingleFields[n].name)) {
      field.id = luaSingleFields[n].id;
      if (flags & FIND_FIELD_DESC) {
        strncpy(field.desc, luaSingleFields[n].desc, sizeof(field.desc)-1);
        field.desc[sizeof(field.desc)-1] = '\0';
      }
      else {
        field.desc[0] = '\0';
      }
      return true;
    }
  }

  // search in multiples
  unsigned int len = strlen(name);
  for (unsigned int n=0; n<DIM(luaMultipleFields); ++n) {
    const char * fieldName = luaMultipleFields[n].name;
    unsigned int fieldLen = strlen(fieldName);
    if (!strncmp(name, fieldName, fieldLen)) {
      unsigned int index;
      if (len == fieldLen+1 && isdigit(name[fieldLen])) {
        index = name[fieldLen] - '1';
      }
      else if (len == fieldLen+2 && isdigit(name[fieldLen]) && isdigit(name[fieldLen+1])) {
        index = 10 * (name[fieldLen] - '0') + (name[fieldLen+1] - '1');
      }
      else {
        continue;
      }
      if (index < luaMultipleFields[n].count) {
        field.id = luaMultipleFields[n].id + index;
        if (flags & FIND_FIELD_DESC) {
          snprintf(field.desc, sizeof(field.desc)-1, luaMultipleFields[n].desc, index+1);
          field.desc[sizeof(field.desc)-1] = '\0';
        }
        else {
          field.desc[0] = '\0';
        }
        return true;
      }
    }
  }
  return false;  // not found
}

// get a detailed info about particular field
static int luaGetFieldInfo(lua_State *L)
{
  const char * what = luaL_checkstring(L, 1);
  LuaField field;
  bool found = luaFindFieldByName(what, field, FIND_FIELD_DESC);
  if (found) {
    lua_newtable(L);
    lua_pushtableinteger(L, "id", field.id);
    lua_pushtablestring(L, "name", what);
    lua_pushtablestring(L, "desc", field.desc);
    return 1;
  }
  return 0;
}

static int luaGetValue(lua_State *L)
{
  int src = 0;
  if (lua_isnumber(L, 1)) {
    src = luaL_checkinteger(L, 1);
  }
  else {
    // convert from field name to its id
    const char *name = luaL_checkstring(L, 1);
    LuaField field;
    bool found = luaFindFieldByName(name, field);
    if (found) {
      src = field.id;
    }
  }
  if (src >= EXTRA_FIRST ) {
    // one of the extra fields
    return luaGetExtraValue(src);
  }
  luaGetValueAndPush(src);
  return 1;
}

static int luaPlayFile(lua_State *L)
{
  const char * filename = luaL_checkstring(L, 1);
  PLAY_FILE(filename, 0, 0);
  return 0;
}

static int luaPlayNumber(lua_State *L)
{
  int number = luaL_checkinteger(L, 1);
  int unit = luaL_checkinteger(L, 2);
  int att = luaL_checkinteger(L, 3);
  playNumber(number, unit, att, 0);
  return 0;
}

static int luaKillEvents(lua_State *L)
{
  int event = luaL_checkinteger(L, 1);
  killEvents(event);
  return 0;
}

static int luaGrey(lua_State *L)
{
  int index = luaL_checkinteger(L, 1);
  lua_pushunsigned(L, GREY(index));
  return 1;
}

static int luaGetGeneralSettings(lua_State *L)
{
  lua_newtable(L);
  lua_pushtablenumber(L, "battMin", double(90+g_eeGeneral.vBatMin)/10);
  lua_pushtablenumber(L, "battMax", double(120+g_eeGeneral.vBatMax)/10);
  return 1;
}

static int luaLcdLock(lua_State *L)
{
  lcd_locked = true;
  return 0;
}

static int luaLcdClear(lua_State *L)
{
  lcd_clear();
  return 0;
}

static int luaLcdDrawPoint(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  lcd_plot(x, y);
  return 0;
}

static int luaLcdDrawLine(lua_State *L)
{
  int x1 = luaL_checkinteger(L, 1);
  int y1 = luaL_checkinteger(L, 2);
  int x2 = luaL_checkinteger(L, 3);
  int y2 = luaL_checkinteger(L, 4);
  int pat = luaL_checkinteger(L, 5);
  int flags = luaL_checkinteger(L, 6);
  lcd_line(x1, y1, x2, y2, pat, flags);
  return 0;
}

static int luaLcdGetLastPos(lua_State *L)
{
  lua_pushinteger(L, lcdLastPos);
  return 1;
}

static int luaLcdDrawText(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  const char * s = luaL_checkstring(L, 3);
  int att = luaL_checkinteger(L, 4);
  lcd_putsAtt(x, y, s, att);
  return 0;
}

static int luaLcdDrawTimer(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int seconds = luaL_checkinteger(L, 3);
  int att = luaL_checkinteger(L, 4);
  putsTimer(x, y, seconds, att|LEFT, att);
  return 0;
}

static int luaLcdDrawNumber(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int n = luaL_checkinteger(L, 3);
  int att = luaL_checkinteger(L, 4);
  lcd_outdezAtt(x, y, n, att);
  return 0;
}

static int luaLcdDrawChannel(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int channel = -1;
  if (lua_isnumber(L, 3)) {
    channel = luaL_checkinteger(L, 3);
  }
  else {
    const char * what = luaL_checkstring(L, 3);
    LuaField field;
    bool found = luaFindFieldByName(what, field);
    if (found) {
      channel = field.id;
    }
  }
  int att = luaL_checkinteger(L, 4);
  getvalue_t value = getValue(channel);
  putsTelemetryChannel(x, y, channel-MIXSRC_FIRST_TELEM, value, att);
  return 0;
}

static int luaLcdDrawSwitch(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int s = luaL_checkinteger(L, 3);
  int att = luaL_checkinteger(L, 4);
  putsSwitches(x, y, s, att);
  return 0;
}

static int luaLcdDrawSource(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int s = luaL_checkinteger(L, 3);
  int att = luaL_checkinteger(L, 4);
  putsMixerSource(x, y, s, att);
  return 0;
}

static int luaLcdDrawPixmap(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  const char * filename = luaL_checkstring(L, 3);
  uint8_t bitmap[BITMAP_BUFFER_SIZE(LCD_W/2, LCD_H)]; // width max is LCD_W/2 pixels for saving stack and avoid a malloc here
  const pm_char * error = bmpLoad(bitmap, filename, LCD_W/2, LCD_H);
  if (!error) {
    lcd_bmp(x, y, bitmap);
  }
  return 0;
}

static int luaLcdDrawRectangle(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);
  lcd_rect(x, y, w, h, 0xff, 0);
  return 0;
}

static int luaLcdDrawFilledRectangle(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);
  int flags = luaL_checkinteger(L, 5);
  lcd_filled_rect(x, y, w, h, SOLID, flags);
  return 0;
}

static int luaLcdDrawGauge(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);
  int num = luaL_checkinteger(L, 5);
  int den = luaL_checkinteger(L, 6);
  // int flags = luaL_checkinteger(L, 7);
  lcd_rect(x, y, w, h);
  uint8_t len = limit((uint8_t)1, uint8_t(w*num/den), uint8_t(w));
  for (int i=1; i<h-1; i++) {
    lcd_hline(x+1, y+i, len);
  }
  return 0;
}

static int luaLcdDrawScreenTitle(lua_State *L)
{
  const char * str = luaL_checkstring(L, 1);
  int idx = luaL_checkinteger(L, 2);
  int cnt = luaL_checkinteger(L, 3);

  if (cnt) displayScreenIndex(idx-1, cnt, 0);
  lcd_filled_rect(0, 0, LCD_W, FH, SOLID, FILL_WHITE|GREY_DEFAULT);
  title(str);

  return 0;
}

static int luaLcdDrawCombobox(lua_State *L)
{
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  luaL_checktype(L, 4, LUA_TTABLE);
  int count = luaL_len(L, 4);  /* get size of table */
  int idx = luaL_checkinteger(L, 5);
  int flags = luaL_checkinteger(L, 6);
  if (idx >= count) {
    // TODO error
  }
  if (flags & BLINK) {
    lcd_filled_rect(x, y, w-9, count*9+2, SOLID, ERASE);
    lcd_rect(x, y, w-9, count*9+2);
    for (int i=0; i<count; i++) {
      lua_rawgeti(L, 4, i+1);
      const char * item = luaL_checkstring(L, -1);
      lcd_putsAtt(x+2, y+2+9*i, item, 0);
    }
    lcd_filled_rect(x+1, y+1+9*idx, w-11, 9);
    lcd_filled_rect(x+w-10, y, 10, 11, SOLID, ERASE);
    lcd_rect(x+w-10, y, 10, 11);
  }
  else if (flags & INVERS) {
    lcd_filled_rect(x, y, w, 11);
    lcd_filled_rect(x+w-9, y+1, 8, 9, SOLID, ERASE);
    lua_rawgeti(L, 4, idx+1);
    const char * item = luaL_checkstring(L, -1);
    lcd_putsAtt(x+2, y+2, item, INVERS);
  }
  else {
    lcd_filled_rect(x, y, w, 11, SOLID, ERASE);
    lcd_rect(x, y, w, 11);
    lcd_filled_rect(x+w-10, y+1, 9, 9, SOLID);
    lua_rawgeti(L, 4, idx+1);
    const char * item = luaL_checkstring(L, -1);
    lcd_putsAtt(x+2, y+2, item, 0);
  }

  lcd_hline(x+w-8, y+3, 6);
  lcd_hline(x+w-8, y+5, 6);
  lcd_hline(x+w-8, y+7, 6);

  return 0;
}

static int luaModelGetInfo(lua_State *L)
{
  lua_newtable(L);
  lua_pushtablezstring(L, "name", g_model.header.name);
  lua_pushtablenzstring(L, "bitmap", g_model.header.bitmap);
  lua_pushtableinteger(L, "id", g_model.header.modelId);
  return 1;
}

static int luaModelSetInfo(lua_State *L)
{
  luaL_checktype(L, -1, LUA_TTABLE);
  for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
    luaL_checktype(L, -2, LUA_TSTRING); // key is string
    const char * key = luaL_checkstring(L, -2);
    if (!strcmp(key, "name")) {
      const char * name = luaL_checkstring(L, -1);
      str2zchar(g_model.header.name, name, sizeof(g_model.header.name));
      memcpy(modelHeaders[g_eeGeneral.currModel].name, g_model.header.name, sizeof(g_model.header.name));
    }
    else if (!strcmp(key, "bitmap")) {
      const char * name = luaL_checkstring(L, -1);
      strncpy(g_model.header.bitmap, name, sizeof(g_model.header.bitmap));
    }
    else if (!strcmp(key, "id")) {
      g_model.header.modelId = luaL_checkinteger(L, -1);
      modelHeaders[g_eeGeneral.currModel].modelId = g_model.header.modelId;
    }
  }
  eeDirty(EE_MODEL);
  return 0;
}

static int luaModelGetTimer(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  if (idx < MAX_TIMERS) {
    TimerData & timer = g_model.timers[idx];
    lua_newtable(L);
    lua_pushtableinteger(L, "mode", timer.mode);
    lua_pushtableinteger(L, "start", timer.start);
    lua_pushtableinteger(L, "value", timersStates[idx].val);
    lua_pushtableinteger(L, "countdownBeep", timer.countdownBeep);
    lua_pushtableboolean(L, "minuteBeep", timer.minuteBeep);
    lua_pushtableinteger(L, "persistent", timer.persistent);
  }
  else {
    lua_pushnil(L);
  }
  return 1;
}

static int luaModelSetTimer(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  if (idx < MAX_TIMERS) {
    TimerData & timer = g_model.timers[idx];
    luaL_checktype(L, -1, LUA_TTABLE);
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      luaL_checktype(L, -2, LUA_TSTRING); // key is string
      const char * key = luaL_checkstring(L, -2);
      if (!strcmp(key, "mode")) {
        timer.mode = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "start")) {
        timer.start = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "value")) {
        timersStates[idx].val = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "countdownBeep")) {
        timer.countdownBeep = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "minuteBeep")) {
        timer.minuteBeep = lua_toboolean(L, -1);
      }
      else if (!strcmp(key, "persistent")) {
        timer.persistent = luaL_checkinteger(L, -1);
      }
    }
    eeDirty(EE_MODEL);
  }
  return 0;
}

static int luaModelResetTimer(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  if (idx < MAX_TIMERS) {
    timerReset(idx);
  }
  return 0;
}

static unsigned int getFirstInput(unsigned int chn)
{
  for (unsigned int i=0; i<MAX_INPUTS; i++) {
    ExpoData * expo = expoAddress(i);
    if (!expo->srcRaw || expo->chn >= chn) {
      return i;
    }
  }
  return 0;
}

static unsigned int getInputsCountFromFirst(unsigned int chn, unsigned int first)
{
  unsigned int count = 0;
  for (unsigned int i=first; i<MAX_INPUTS; i++) {
    ExpoData * expo = expoAddress(i);
    if (!expo->srcRaw || expo->chn!=chn) break;
    count++;
  }
  return count;
}

static unsigned int getInputsCount(unsigned int chn)
{
  return getInputsCountFromFirst(chn, getFirstInput(chn));
}

static int luaModelGetInputsCount(lua_State *L)
{
  unsigned int chn = luaL_checkunsigned(L, 1);
  int count = getInputsCount(chn);
  lua_pushinteger(L, count);
  return 1;
}

static int luaModelGetInput(lua_State *L)
{
  unsigned int chn = luaL_checkunsigned(L, 1);
  unsigned int idx = luaL_checkunsigned(L, 2);
  unsigned int first = getFirstInput(chn);
  unsigned int count = getInputsCountFromFirst(chn, first);
  if (idx < count) {
    ExpoData * expo = expoAddress(first+idx);
    lua_newtable(L);
    lua_pushtablezstring(L, "name", expo->name);
    lua_pushtableinteger(L, "source", expo->srcRaw);
    lua_pushtableinteger(L, "weight", expo->weight);
    lua_pushtableinteger(L, "offset", expo->offset);
    lua_pushtableinteger(L, "switch", expo->swtch);
  }
  else {
    lua_pushnil(L);
  }
  return 1;
}

static int luaModelInsertInput(lua_State *L)
{
  unsigned int chn = luaL_checkunsigned(L, 1);
  unsigned int idx = luaL_checkunsigned(L, 2);

  unsigned int first = getFirstInput(chn);
  unsigned int count = getInputsCountFromFirst(chn, first);

  if (chn<MAX_INPUTS && getExpoMixCount(1)<MAX_INPUTS && idx<=count) {
    idx = first + idx;
    s_currCh = chn + 1;
    insertExpoMix(1, idx);
    ExpoData * expo = expoAddress(idx);
    luaL_checktype(L, -1, LUA_TTABLE);
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      luaL_checktype(L, -2, LUA_TSTRING); // key is string
      const char * key = luaL_checkstring(L, -2);
      if (!strcmp(key, "name")) {
        const char * name = luaL_checkstring(L, -1);
        str2zchar(expo->name, name, sizeof(expo->name));
      }
      else if (!strcmp(key, "source")) {
        expo->srcRaw = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "weight")) {
        expo->weight = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "offset")) {
        expo->offset = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "switch")) {
        expo->swtch = luaL_checkinteger(L, -1);
      }
    }
  }

  return 0;
}

static int luaModelDeleteInput(lua_State *L)
{
  unsigned int chn = luaL_checkunsigned(L, 1);
  unsigned int idx = luaL_checkunsigned(L, 2);

  int first = getFirstInput(chn);
  unsigned int count = getInputsCountFromFirst(chn, first);

  if (idx < count) {
    deleteExpoMix(1, first+idx);
  }

  return 0;
}

static int luaModelDeleteInputs(lua_State *L)
{
  clearInputs();
  return 0;
}

static int luaModelDefaultInputs(lua_State *L)
{
  defaultInputs();
  return 0;
}

static unsigned int getFirstMix(unsigned int chn)
{
  for (unsigned int i=0; i<MAX_MIXERS; i++) {
    MixData * mix = mixAddress(i);
    if (!mix->srcRaw || mix->destCh>=chn) {
      return i;
    }
  }
  return 0;
}

static unsigned int getMixesCountFromFirst(unsigned int chn, unsigned int first)
{
  unsigned int count = 0;
  for (unsigned int i=first; i<MAX_MIXERS; i++) {
    MixData * mix = mixAddress(i);
    if (!mix->srcRaw || mix->destCh!=chn) break;
    count++;
  }
  return count;
}

static unsigned int getMixesCount(unsigned int chn)
{
  return getMixesCountFromFirst(chn, getFirstMix(chn));
}

static int luaModelGetMixesCount(lua_State *L)
{
  unsigned int chn = luaL_checkunsigned(L, 1);
  unsigned int count = getMixesCount(chn);
  lua_pushinteger(L, count);
  return 1;
}

static int luaModelGetMix(lua_State *L)
{
  unsigned int chn = luaL_checkunsigned(L, 1);
  unsigned int idx = luaL_checkunsigned(L, 2);
  unsigned int first = getFirstMix(chn);
  unsigned int count = getMixesCountFromFirst(chn, first);
  if (idx < count) {
    MixData * mix = mixAddress(first+idx);
    lua_newtable(L);
    lua_pushtablezstring(L, "name", mix->name);
    lua_pushtableinteger(L, "source", mix->srcRaw);
    lua_pushtableinteger(L, "weight", mix->weight);
    lua_pushtableinteger(L, "offset", mix->offset);
    lua_pushtableinteger(L, "switch", mix->swtch);
    lua_pushtableinteger(L, "curveType", mix->curve.type);
    lua_pushtableinteger(L, "curveValue", mix->curve.value);
    lua_pushtableinteger(L, "multiplex", mix->mltpx);
    lua_pushtableinteger(L, "flightModes", mix->flightModes);
    lua_pushtableboolean(L, "carryTrim", mix->carryTrim);
    lua_pushtableinteger(L, "mixWarn", mix->mixWarn);
    lua_pushtableinteger(L, "delayUp", mix->delayUp);
    lua_pushtableinteger(L, "delayDown", mix->delayDown);
    lua_pushtableinteger(L, "speedUp", mix->speedUp);
    lua_pushtableinteger(L, "speedDown", mix->speedDown);
  }
  else {
    lua_pushnil(L);
  }
  return 1;
}

static int luaModelInsertMix(lua_State *L)
{
  unsigned int chn = luaL_checkunsigned(L, 1);
  unsigned int idx = luaL_checkunsigned(L, 2);

  unsigned int first = getFirstMix(chn);
  unsigned int count = getMixesCountFromFirst(chn, first);

  if (chn<NUM_CHNOUT && getExpoMixCount(0)<MAX_MIXERS && idx<=count) {
    idx += first;
    s_currCh = chn+1;
    insertExpoMix(0, idx);
    MixData *mix = mixAddress(idx);
    luaL_checktype(L, -1, LUA_TTABLE);
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      luaL_checktype(L, -2, LUA_TSTRING); // key is string
      const char * key = luaL_checkstring(L, -2);
      if (!strcmp(key, "name")) {
        const char * name = luaL_checkstring(L, -1);
        str2zchar(mix->name, name, sizeof(mix->name));
      }
      else if (!strcmp(key, "source")) {
        mix->srcRaw = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "weight")) {
        mix->weight = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "offset")) {
        mix->offset = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "switch")) {
        mix->swtch = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "curveType")) {
        mix->curve.type = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "curveValue")) {
        mix->curve.value = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "multiplex")) {
        mix->mltpx = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "flightModes")) {
        mix->flightModes = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "carryTrim")) {
        mix->carryTrim = lua_toboolean(L, -1);
      }
      else if (!strcmp(key, "mixWarn")) {
        mix->mixWarn = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "delayUp")) {
        mix->delayUp = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "delayDown")) {
        mix->delayDown = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "speedUp")) {
        mix->speedUp = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "speedDown")) {
        mix->speedDown = luaL_checkinteger(L, -1);
      }
    }
  }

  return 0;
}

static int luaModelDeleteMix(lua_State *L)
{
  unsigned int chn = luaL_checkunsigned(L, 1);
  unsigned int idx = luaL_checkunsigned(L, 2);

  unsigned int first = getFirstMix(chn);
  unsigned int count = getMixesCountFromFirst(chn, first);

  if (idx < count) {
    deleteExpoMix(0, first+idx);
  }

  return 0;
}

static int luaModelDeleteMixes(lua_State *L)
{
  memset(g_model.mixData, 0, sizeof(g_model.mixData));
  return 0;
}

static int luaModelGetLogicalSwitch(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  if (idx < NUM_LOGICAL_SWITCH) {
    LogicalSwitchData * sw = lswAddress(idx);
    lua_newtable(L);
    lua_pushtableinteger(L, "func", sw->func);
    lua_pushtableinteger(L, "v1", sw->v1);
    lua_pushtableinteger(L, "v2", sw->v2);
    lua_pushtableinteger(L, "v3", sw->v3);
    lua_pushtableinteger(L, "and", sw->andsw);
    lua_pushtableinteger(L, "delay", sw->delay);
    lua_pushtableinteger(L, "duration", sw->duration);
  }
  else {
    lua_pushnil(L);
  }
  return 1;
}

static int luaModelSetLogicalSwitch(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  if (idx < NUM_LOGICAL_SWITCH) {
    LogicalSwitchData * sw = lswAddress(idx);
    memclear(sw, sizeof(LogicalSwitchData));
    luaL_checktype(L, -1, LUA_TTABLE);
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      luaL_checktype(L, -2, LUA_TSTRING); // key is string
      const char * key = luaL_checkstring(L, -2);
      if (!strcmp(key, "func")) {
        sw->func = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "v1")) {
        sw->v1 = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "v2")) {
        sw->v2 = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "v3")) {
        sw->v3 = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "and")) {
        sw->andsw = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "delay")) {
        sw->delay = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "duration")) {
        sw->duration = luaL_checkinteger(L, -1);
      }
    }
    eeDirty(EE_MODEL);
  }

  return 0;
}

static int luaModelGetCurve(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  if (idx < MAX_CURVES) {
    CurveInfo & curveInfo = g_model.curves[idx];
    lua_newtable(L);
    lua_pushtablezstring(L, "name", g_model.curveNames[idx]);
    lua_pushtableinteger(L, "type", curveInfo.type);
    lua_pushtableboolean(L, "smooth", curveInfo.smooth);
    lua_pushtableinteger(L, "points", curveInfo.points+5);
    lua_pushstring(L, "y");
    lua_newtable(L);
    int8_t * point = curveAddress(idx);
    for (int i=0; i<curveInfo.points+5; i++) {
      lua_pushinteger(L, i);
      lua_pushinteger(L, *point++);
      lua_settable(L, -3);
    }
    lua_settable(L, -3);
    if (curveInfo.type == CURVE_TYPE_CUSTOM) {
      lua_pushstring(L, "x");
      lua_newtable(L);
      lua_pushinteger(L, 0);
      lua_pushinteger(L, 0);
      lua_settable(L, -3);
      for (int i=0; i<curveInfo.points+3; i++) {
        lua_pushinteger(L, i+1);
        lua_pushinteger(L, *point++);
        lua_settable(L, -3);
      }
      lua_pushinteger(L, curveInfo.points+4);
      lua_pushinteger(L, 100);
      lua_settable(L, -3);
      lua_settable(L, -3);
    }
  }
  else {
    lua_pushnil(L);
  }
  return 1;
}

static int luaModelGetCustomFunction(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  if (idx < NUM_CFN) {
    CustomFnData * cfn = &g_model.funcSw[idx];
    lua_newtable(L);
    lua_pushtableinteger(L, "switch", CFN_SWITCH(cfn));
    lua_pushtableinteger(L, "func", CFN_FUNC(cfn));
    if (CFN_FUNC(cfn) == FUNC_PLAY_TRACK || CFN_FUNC(cfn) == FUNC_BACKGND_MUSIC || CFN_FUNC(cfn) == FUNC_PLAY_SCRIPT) {
      lua_pushtablenzstring(L, "name", cfn->play.name);
    }
    else {
      lua_pushtableinteger(L, "value", cfn->all.val);
      lua_pushtableinteger(L, "mode", cfn->all.mode);
      lua_pushtableinteger(L, "param", cfn->all.param);
    }
    lua_pushtableinteger(L, "active", CFN_ACTIVE(cfn));
  }
  else {
    lua_pushnil(L);
  }
  return 1;
}

static int luaModelSetCustomFunction(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  if (idx < NUM_CFN) {
    CustomFnData * cfn = &g_model.funcSw[idx];
    memclear(cfn, sizeof(CustomFnData));
    luaL_checktype(L, -1, LUA_TTABLE);
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      luaL_checktype(L, -2, LUA_TSTRING); // key is string
      const char * key = luaL_checkstring(L, -2);
      if (!strcmp(key, "switch")) {
        CFN_SWITCH(cfn) = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "func")) {
        CFN_FUNC(cfn) = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "name")) {
        const char * name = luaL_checkstring(L, -1);
        strncpy(cfn->play.name, name, sizeof(cfn->play.name));
      }
      else if (!strcmp(key, "value")) {
        cfn->all.val = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "mode")) {
        cfn->all.mode = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "param")) {
        cfn->all.param = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "active")) {
        CFN_ACTIVE(cfn) = luaL_checkinteger(L, -1);
      }
    }
    eeDirty(EE_MODEL);
  }

  return 0;
}

static int luaModelGetOutput(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  if (idx < NUM_CHNOUT) {
    LimitData * limit = limitAddress(idx);
    lua_newtable(L);
    lua_pushtablezstring(L, "name", limit->name);
    lua_pushtableinteger(L, "min", limit->min-1000);
    lua_pushtableinteger(L, "max", limit->max+1000);
    lua_pushtableinteger(L, "offset", limit->offset);
    lua_pushtableinteger(L, "ppmCenter", limit->ppmCenter);
    lua_pushtableinteger(L, "symetrical", limit->symetrical);
    lua_pushtableinteger(L, "revert", limit->revert);
    if (limit->curve)
      lua_pushtableinteger(L, "curve", limit->curve-1);
    else
      lua_pushtablenil(L, "curve");
  }
  else {
    lua_pushnil(L);
  }
  return 1;
}

static int luaModelSetOutput(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  if (idx < NUM_CHNOUT) {
    LimitData * limit = limitAddress(idx);
    luaL_checktype(L, -1, LUA_TTABLE);
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      luaL_checktype(L, -2, LUA_TSTRING); // key is string
      const char * key = luaL_checkstring(L, -2);
      if (!strcmp(key, "name")) {
        const char * name = luaL_checkstring(L, -1);
        str2zchar(limit->name, name, sizeof(limit->name));
      }
      else if (!strcmp(key, "min")) {
        limit->min = luaL_checkinteger(L, -1)+1000;
      }
      else if (!strcmp(key, "max")) {
        limit->max = luaL_checkinteger(L, -1)-1000;
      }
      else if (!strcmp(key, "offset")) {
        limit->offset = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "ppmCenter")) {
        limit->ppmCenter = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "symetrical")) {
        limit->symetrical = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "revert")) {
        limit->revert = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "curve")) {
        if (lua_isnil(L, -1))
          limit->curve = 0;
        else
          limit->curve = luaL_checkinteger(L, -1) + 1;
      }
    }
    eeDirty(EE_MODEL);
  }

  return 0;
}

static int luaModelGetGlobalVariable(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  unsigned int phase = luaL_checkunsigned(L, 2);
  if (phase < MAX_FLIGHT_MODES && idx < MAX_GVARS)
    lua_pushinteger(L, g_model.flightModeData[phase].gvars[idx]);
  else
    lua_pushnil(L);
  return 1;
}

static int luaModelSetGlobalVariable(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  unsigned int phase = luaL_checkunsigned(L, 2);
  int value = luaL_checkinteger(L, 3);
  if (phase < MAX_FLIGHT_MODES && idx < MAX_GVARS && value >= -GVAR_MAX && value <= GVAR_MAX) {
    g_model.flightModeData[phase].gvars[idx] = value;
    eeDirty(EE_MODEL);
  }
  return 0;
}

static int luaModelGetTelemetryChannel(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);
  if (idx < MAX_FRSKY_A_CHANNELS) {
    FrSkyChannelData & channel = g_model.frsky.channels[idx];
    lua_newtable(L);
    double range = applyChannelRatio(idx, 255-channel.offset);
    double offset = applyChannelRatio(idx, 0);
    double alarm1 = applyChannelRatio(idx, channel.alarms_value[0]);
    double alarm2 = applyChannelRatio(idx, channel.alarms_value[1]);
    if (ANA_CHANNEL_UNIT(idx) >= UNIT_RAW) {
      range /= 10;
      offset /= 10;
      alarm1 /= 10;
      alarm2 /= 10;
    }
    else {
      range /= 100;
      offset /= 100;
      alarm1 /= 100;
      alarm2 /= 100;
    }
    lua_pushtablenumber(L, "range", range);
    lua_pushtablenumber(L, "offset", offset);
    lua_pushtablenumber(L, "alarm1", alarm1);
    lua_pushtablenumber(L, "alarm2", alarm2);
    lua_pushtableinteger(L, "unit", channel.type);
  }
  else {
    lua_pushnil(L);
  }
  return 1;
}

int findmult(float value, float base)
{
  int vvalue = value*10;
  int vbase = base*10;
  vvalue--;

  int mult = 0;
  for (int i=8; i>=0; i--) {
    if (vvalue/vbase >= (1<<i)) {
      mult = i+1;
      break;
    }
  }

  return mult;
}

static int luaModelSetTelemetryChannel(lua_State *L)
{
  unsigned int idx = luaL_checkunsigned(L, 1);

  if (idx < MAX_FRSKY_A_CHANNELS) {
    FrSkyChannelData & channel = g_model.frsky.channels[idx];
    double range = applyChannelRatio(idx, 255-channel.offset);
    double offset = applyChannelRatio(idx, 0);
    double alarm1 = applyChannelRatio(idx, channel.alarms_value[0]);
    double alarm2 = applyChannelRatio(idx, channel.alarms_value[1]);
    if (ANA_CHANNEL_UNIT(idx) >= UNIT_RAW) {
      range /= 10;
      offset /= 10;
      alarm1 /= 10;
      alarm2 /= 10;
    }
    else {
      range /= 100;
      offset /= 100;
      alarm1 /= 100;
      alarm2 /= 100;
    }
    luaL_checktype(L, -1, LUA_TTABLE);
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      luaL_checktype(L, -2, LUA_TSTRING); // key is string
      const char * key = luaL_checkstring(L, -2);
      if (!strcmp(key, "unit")) {
        channel.type = luaL_checkinteger(L, -1);
      }
      else if (!strcmp(key, "range")) {
        range = luaL_checknumber(L, -1);
        double value = range;
        if (ANA_CHANNEL_UNIT(idx) < UNIT_RAW) {
          value *= 10;
        }
        channel.multiplier = findmult(value, 255);
        channel.ratio = (int)(round(value)) / (1<<channel.multiplier);
      }
      else if (!strcmp(key, "offset")) {
        offset = luaL_checknumber(L, -1);
      }
      else if (!strcmp(key, "alarm1")) {
        alarm1 = luaL_checknumber(L, -1);
      }
      else if (!strcmp(key, "alarm2")) {
        alarm2 = luaL_checknumber(L, -1);
      }
    }
    if (range > 0) {
      channel.offset = round((offset * 255) / range);
      channel.alarms_value[0] = limit<int>(0, round((alarm1-offset)*255/range), 255);
      channel.alarms_value[1] = limit<int>(0, round((alarm2-offset)*255/range), 255);
    }
    else {
      channel.offset = 0;;
      channel.alarms_value[0] = 0;
      channel.alarms_value[1] = 0;
    }
    eeDirty(EE_MODEL);
  }
  return 0;
}

static int luaPopupInput(lua_State *L)
{
  uint8_t event = luaL_checkinteger(L, 2);
  s_warning_input_value = luaL_checkinteger(L, 3);
  s_warning_input_min = luaL_checkinteger(L, 4);
  s_warning_input_max = luaL_checkinteger(L, 5);
  s_warning = luaL_checkstring(L, 1);
  s_warning_type = WARNING_TYPE_INPUT;
  displayWarning(event);
  if (s_warning_result) {
    s_warning_result = 0;
    lua_pushstring(L, "OK");
  }
  else if (!s_warning) {
    lua_pushstring(L, "CANCEL");
  }
  else {
    lua_pushinteger(L, s_warning_input_value);
  }
  s_warning = NULL;
  return 1;
}

static int luaDefaultStick(lua_State *L)
{
  uint8_t channel = luaL_checkinteger(L, 1);
  lua_pushinteger(L, channel_order(channel+1)-1);
  return 1;
}

static int luaDefaultChannel(lua_State *L)
{
  uint8_t stick = luaL_checkinteger(L, 1);
  for (int i=1; i<=4; i++) {
    int tmp = channel_order(i) - 1;
    if (tmp == stick) {
      lua_pushinteger(L, i-1);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

int luaGetInputs(ScriptInputsOutputs & sid)
{
  if (!lua_istable(L, -1))
    return -1;

  sid.inputsCount = 0;
  for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
    luaL_checktype(L, -2, LUA_TNUMBER); // key is number
    luaL_checktype(L, -1, LUA_TTABLE); // value is table
    if (sid.inputsCount<MAX_SCRIPT_INPUTS) {
      uint8_t field = 0;
      for (lua_pushnil(L); lua_next(L, -2) && field<5; lua_pop(L, 1), field++) {
        switch (field) {
          case 0:
            luaL_checktype(L, -2, LUA_TNUMBER); // key is number
            luaL_checktype(L, -1, LUA_TSTRING); // value is string
            sid.inputs[sid.inputsCount].name = lua_tostring(L, -1);
            break;
          case 1:
            luaL_checktype(L, -2, LUA_TNUMBER); // key is number
            luaL_checktype(L, -1, LUA_TNUMBER); // value is number
            sid.inputs[sid.inputsCount].type = lua_tointeger(L, -1);
            if (sid.inputs[sid.inputsCount].type == 0) {
              sid.inputs[sid.inputsCount].min = -100;
              sid.inputs[sid.inputsCount].max = 100;
            }
            else {
              sid.inputs[sid.inputsCount].max = MIXSRC_LAST_TELEM;
            }
            break;
          case 2:
            luaL_checktype(L, -2, LUA_TNUMBER); // key is number
            luaL_checktype(L, -1, LUA_TNUMBER); // value is number
            sid.inputs[sid.inputsCount].min = lua_tointeger(L, -1);
            break;
          case 3:
            luaL_checktype(L, -2, LUA_TNUMBER); // key is number
            luaL_checktype(L, -1, LUA_TNUMBER); // value is number
            sid.inputs[sid.inputsCount].max = lua_tointeger(L, -1);
            break;
          case 4:
            luaL_checktype(L, -2, LUA_TNUMBER); // key is number
            luaL_checktype(L, -1, LUA_TNUMBER); // value is number
            sid.inputs[sid.inputsCount].def = lua_tointeger(L, -1);
            break;
        }
      }
      sid.inputsCount++;
    }
  }

  return 0;
}

int luaGetOutputs(ScriptInputsOutputs & sid)
{
  if (!lua_istable(L, -1))
    return -1;

  sid.outputsCount = 0;
  for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
    luaL_checktype(L, -2, LUA_TNUMBER); // key is number
    luaL_checktype(L, -1, LUA_TSTRING); // value is string
    if (sid.outputsCount<MAX_SCRIPT_OUTPUTS) {
      sid.outputs[sid.outputsCount++].name = lua_tostring(L, -1);
    }
  }

  return 0;
}

const luaL_Reg opentxLib[] = {
  { "getTime", luaGetTime },
  { "getVersion", luaGetVersion },
  { "getGeneralSettings", luaGetGeneralSettings },
  { "getValue", luaGetValue },
  { "getFieldInfo", luaGetFieldInfo },
  { "playFile", luaPlayFile },
  { "playNumber", luaPlayNumber },
//  { "playDuration", luaPlayDuration },
//  { "playTone", luaPlayTone },
  { "popupInput", luaPopupInput },
  { "defaultStick", luaDefaultStick },
  { "defaultChannel", luaDefaultChannel },
  { "killEvents", luaKillEvents },
  { "GREY", luaGrey },
  { NULL, NULL }  /* sentinel */
};

const luaR_value_entry opentxConstants[] = {
  { "FULLSCALE", RESX },
  { "XXLSIZE", XXLSIZE },
  { "DBLSIZE", DBLSIZE },
  { "MIDSIZE", MIDSIZE },
  { "SMLSIZE", SMLSIZE },
  { "INVERS", INVERS },
  { "BOLD", BOLD },
  { "BLINK", BLINK },
  { "FIXEDWIDTH", FIXEDWIDTH },
  { "LEFT", LEFT },
  { "PREC1", PREC1 },
  { "PREC2", PREC2 },
  { "VALUE", 0 },
  { "SOURCE", 1 },
  { "REPLACE", MLTPX_REP },
  { "MIXSRC_FIRST_INPUT", MIXSRC_FIRST_INPUT },
  { "MIXSRC_Rud", MIXSRC_Rud },
  { "MIXSRC_Ele", MIXSRC_Ele },
  { "MIXSRC_Thr", MIXSRC_Thr },
  { "MIXSRC_Ail", MIXSRC_Ail },
  { "MIXSRC_SA", MIXSRC_SA },
  { "MIXSRC_SB", MIXSRC_SB },
  { "MIXSRC_SC", MIXSRC_SC },
  { "MIXSRC_SD", MIXSRC_SD },
  { "MIXSRC_SE", MIXSRC_SE },
  { "MIXSRC_SF", MIXSRC_SF },
  { "MIXSRC_SG", MIXSRC_SG },
  { "MIXSRC_SH", MIXSRC_SH },
  { "MIXSRC_CH1", MIXSRC_CH1 },
  { "SWSRC_LAST", SWSRC_LAST_LOGICAL_SWITCH },
  { "EVT_MENU_BREAK", EVT_KEY_BREAK(KEY_MENU) },
  { "EVT_PAGE_BREAK", EVT_KEY_BREAK(KEY_PAGE) },
  { "EVT_PAGE_LONG", EVT_KEY_LONG(KEY_PAGE) },
  { "EVT_ENTER_BREAK", EVT_KEY_BREAK(KEY_ENTER) },
  { "EVT_ENTER_LONG", EVT_KEY_LONG(KEY_ENTER) },
  { "EVT_EXIT_BREAK", EVT_KEY_BREAK(KEY_EXIT) },
  { "EVT_PLUS_BREAK", EVT_KEY_BREAK(KEY_PLUS) },
  { "EVT_MINUS_BREAK", EVT_KEY_BREAK(KEY_MINUS) },
  { "EVT_PLUS_FIRST", EVT_KEY_FIRST(KEY_PLUS) },
  { "EVT_MINUS_FIRST", EVT_KEY_FIRST(KEY_MINUS) },
  { "EVT_PLUS_REPT", EVT_KEY_REPT(KEY_PLUS) },
  { "EVT_MINUS_REPT", EVT_KEY_REPT(KEY_MINUS) },
  { "FILL_WHITE", FILL_WHITE },
  { "GREY_DEFAULT", GREY_DEFAULT },
  { "SOLID", SOLID },
  { "DOTTED", DOTTED },
  { "FORCE", FORCE },
  { "ERASE", ERASE },
  { "ROUND", ROUND },
  { "LCD_W", LCD_W },
  { "LCD_H", LCD_H },
  { "PLAY_NOW", PLAY_NOW },
  { "PLAY_BACKGROUND", PLAY_BACKGROUND },
  { "TIMEHOUR", TIMEHOUR },
  { NULL, 0 }  /* sentinel */
};

const luaL_Reg modelLib[] = {
  { "getInfo", luaModelGetInfo },
  { "setInfo", luaModelSetInfo },
  { "getTimer", luaModelGetTimer },
  { "setTimer", luaModelSetTimer },
  { "resetTimer", luaModelResetTimer },
  { "getInputsCount", luaModelGetInputsCount },
  { "getInput", luaModelGetInput },
  { "insertInput", luaModelInsertInput },
  { "deleteInput", luaModelDeleteInput },
  { "deleteInputs", luaModelDeleteInputs },
  { "defaultInputs", luaModelDefaultInputs },
  { "getMixesCount", luaModelGetMixesCount },
  { "getMix", luaModelGetMix },
  { "insertMix", luaModelInsertMix },
  { "deleteMix", luaModelDeleteMix },
  { "deleteMixes", luaModelDeleteMixes },
  { "getLogicalSwitch", luaModelGetLogicalSwitch },
  { "setLogicalSwitch", luaModelSetLogicalSwitch },
  { "getCustomFunction", luaModelGetCustomFunction },
  { "setCustomFunction", luaModelSetCustomFunction },
  { "getCurve", luaModelGetCurve },
  { "getOutput", luaModelGetOutput },
  { "setOutput", luaModelSetOutput },
  { "getGlobalVariable", luaModelGetGlobalVariable },
  { "setGlobalVariable", luaModelSetGlobalVariable },
  { "getTelemetryChannel", luaModelGetTelemetryChannel },
  { "setTelemetryChannel", luaModelSetTelemetryChannel },
  { NULL, NULL }  /* sentinel */
};

const luaL_Reg lcdLib[] = {
  { "lock", luaLcdLock },
  { "clear", luaLcdClear },
  { "getLastPos", luaLcdGetLastPos },
  { "drawPoint", luaLcdDrawPoint },
  { "drawLine", luaLcdDrawLine },
  { "drawRectangle", luaLcdDrawRectangle },
  { "drawFilledRectangle", luaLcdDrawFilledRectangle },
  { "drawGauge", luaLcdDrawGauge },
  { "drawText", luaLcdDrawText },
  { "drawTimer", luaLcdDrawTimer },
  { "drawNumber", luaLcdDrawNumber },
  { "drawChannel", luaLcdDrawChannel },
  { "drawSwitch", luaLcdDrawSwitch },
  { "drawSource", luaLcdDrawSource },
  { "drawPixmap", luaLcdDrawPixmap },
  { "drawScreenTitle", luaLcdDrawScreenTitle },
  { "drawCombobox", luaLcdDrawCombobox },
  { NULL, NULL }  /* sentinel */
};

void luaDisable()
{
  POPUP_WARNING("Lua disabled!");
  luaState = INTERPRETER_PANIC;
}

void luaClose()
{
  if (L) {
    PROTECT_LUA() {
      lua_close(L);  // this should not panic, but we make sure anyway
    }
    else {
      // we can only disable Lua for the rest of the session
      luaDisable();
    }
    UNPROTECT_LUA();
    L = NULL;
  }
}

void luaRegisterAll()
{
  // Init lua
  luaL_openlibs(L);
}

void luaInit()
{
  luaClose();
  if (luaState != INTERPRETER_PANIC) {
#if defined(USE_BIN_ALLOCATOR)
    L = lua_newstate(bin_l_alloc, NULL);   //we use our own allocator!
#else
    L = lua_newstate(l_alloc, NULL);   //we use Lua default allocator
#endif
    if (L) {
      // install our panic handler
      lua_atpanic(L, &custom_lua_atpanic);

      // protect libs and constants registration
      PROTECT_LUA() {
        luaRegisterAll();
      }
      else {
        // if we got panic during registration
        // we disable Lua for this session
        luaDisable();
      }
      UNPROTECT_LUA();
    }
    else {
      /* log error and return */
      luaDisable();
    }
  }
}

void luaFree(ScriptInternalData & sid)
{
  PROTECT_LUA() {
    if (sid.run) {
      luaL_unref(L, LUA_REGISTRYINDEX, sid.run);
      sid.run = 0;
    }
    if (sid.background) {
      luaL_unref(L, LUA_REGISTRYINDEX, sid.background);
      sid.background = 0;
    }
    lua_gc(L, LUA_GCCOLLECT, 0);
  }
  else {
    luaDisable();
  }
  UNPROTECT_LUA();
}

int luaLoad(const char *filename, ScriptInternalData & sid, ScriptInputsOutputs * sio=NULL)
{
  int init = 0;

  sid.instructions = 0;
  sid.state = SCRIPT_OK;

#if 0
  // not needed, we just called luaInit
  luaFree(sid);
#endif

  if (luaState == INTERPRETER_PANIC) {
	return SCRIPT_PANIC;
  }

  SET_LUA_INSTRUCTIONS_COUNT(MANUAL_SCRIPTS_MAX_INSTRUCTIONS);

  PROTECT_LUA() {
    if (luaL_loadfile(L, filename) == 0 &&
        lua_pcall(L, 0, 1, 0) == 0 &&
        lua_istable(L, -1)) {

      luaL_checktype(L, -1, LUA_TTABLE);

      for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
        const char *key = lua_tostring(L, -2);
        if (!strcmp(key, "init")) {
          init = luaL_ref(L, LUA_REGISTRYINDEX);
          lua_pushnil(L);
        }
        else if (!strcmp(key, "run")) {
          sid.run = luaL_ref(L, LUA_REGISTRYINDEX);
          lua_pushnil(L);
        }
        else if (!strcmp(key, "background")) {
          sid.background = luaL_ref(L, LUA_REGISTRYINDEX);
          lua_pushnil(L);
        }
        else if (sio && !strcmp(key, "input")) {
          luaGetInputs(*sio);
        }
        else if (sio && !strcmp(key, "output")) {
          luaGetOutputs(*sio);
        }
      }

      if (init) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, init);
        if (lua_pcall(L, 0, 0, 0) != 0) {
          TRACE("Error in script %s init: %s", filename, lua_tostring(L, -1));
          sid.state = SCRIPT_SYNTAX_ERROR;
        }
        luaL_unref(L, LUA_REGISTRYINDEX, init);
        lua_gc(L, LUA_GCCOLLECT, 0);
      }
    }
    else {
      TRACE("Error in script %s: %s", filename, lua_tostring(L, -1));
      sid.state = SCRIPT_SYNTAX_ERROR;
    }
  }
  else {
    luaDisable();
    return SCRIPT_PANIC;
  }
  UNPROTECT_LUA();

  if (sid.state != SCRIPT_OK) {
    luaFree(sid);
  }

  return sid.state;
}

bool luaLoadMixScript(uint8_t index)
{
  ScriptData & sd = g_model.scriptsData[index];

  if (ZEXIST(sd.file)) {
    ScriptInternalData & sid = scriptInternalData[luaScriptsCount++];
    ScriptInputsOutputs * sio = &scriptInputsOutputs[index];
    sid.reference = SCRIPT_MIX_FIRST+index;
    sid.state = SCRIPT_NOFILE;
    char filename[sizeof(SCRIPTS_MIXES_PATH)+sizeof(sd.file)+sizeof(SCRIPTS_EXT)] = SCRIPTS_MIXES_PATH "/";
    strncpy(filename+sizeof(SCRIPTS_MIXES_PATH), sd.file, sizeof(sd.file));
    filename[sizeof(SCRIPTS_MIXES_PATH)+sizeof(sd.file)] = '\0';
    strcat(filename+sizeof(SCRIPTS_MIXES_PATH), SCRIPTS_EXT);
    if (luaLoad(filename, sid, sio) == SCRIPT_PANIC) {
      return false;
    }
  }
  return true;
}

bool luaLoadFunctionScript(uint8_t index)
{
  CustomFnData & fn = g_model.funcSw[index];

  if (fn.func == FUNC_PLAY_SCRIPT && ZEXIST(fn.play.name)) {
    if (luaScriptsCount < MAX_SCRIPTS) {
      ScriptInternalData & sid = scriptInternalData[luaScriptsCount++];
      sid.reference = SCRIPT_FUNC_FIRST+index;
      sid.state = SCRIPT_NOFILE;
      char filename[sizeof(SCRIPTS_FUNCS_PATH)+sizeof(fn.play.name)+sizeof(SCRIPTS_EXT)] = SCRIPTS_FUNCS_PATH "/";
      strncpy(filename+sizeof(SCRIPTS_FUNCS_PATH), fn.play.name, sizeof(fn.play.name));
      filename[sizeof(SCRIPTS_FUNCS_PATH)+sizeof(fn.play.name)] = '\0';
      strcat(filename+sizeof(SCRIPTS_FUNCS_PATH), SCRIPTS_EXT);
      if (luaLoad(filename, sid) == SCRIPT_PANIC) {
        return false;
      }
    }
    else {
      POPUP_WARNING(STR_TOO_MANY_LUA_SCRIPTS);
      return false;
    }
  }
  return true;
}

void getTelemetryScriptPath(char * path, uint8_t index)
{
  strcpy(path, SCRIPTS_PATH "/");
  char * curs = strcat_modelname(path+sizeof(SCRIPTS_PATH), g_eeGeneral.currModel);
  if (index == TELEMETRY_VOLTAGES_SCREEN) {
    strcpy(curs, "/telempw.lua");
  }
  else if (index == TELEMETRY_AFTER_FLIGHT_SCREEN) {
    strcpy(curs, "/telemaf.lua");
  }
  else {
    strcpy(curs, "/telemX.lua");
    curs[6] = '1' + index;
  }
}

bool luaLoadTelemetryScript(uint8_t index)
{
  char path[256];
  getTelemetryScriptPath(path, index);
  if (isFileAvailable(path)) {
    if (luaScriptsCount < MAX_SCRIPTS) {
      ScriptInternalData & sid = scriptInternalData[luaScriptsCount++];
      sid.reference = SCRIPT_TELEMETRY_FIRST+index;
      sid.state = SCRIPT_NOFILE;
      if (luaLoad(path, sid) == SCRIPT_PANIC) {
        return false;
      }
    }
    else {
      POPUP_WARNING(STR_TOO_MANY_LUA_SCRIPTS);
      return false;
    }
  }
  return true;
}

uint8_t isTelemetryScriptAvailable(uint8_t index)
{
  for (int i=0; i<luaScriptsCount; i++) {
    ScriptInternalData & sid = scriptInternalData[i];
    if (sid.reference == SCRIPT_TELEMETRY_FIRST+index) {
      return sid.state;
    }
  }
  return SCRIPT_NOFILE;
}

void luaLoadPermanentScripts()
{
  luaScriptsCount = 0;
  memset(scriptInternalData, 0, sizeof(scriptInternalData));
  memset(scriptInputsOutputs, 0, sizeof(scriptInputsOutputs));

  // Load model scripts
  for (int i=0; i<MAX_SCRIPTS; i++) {
	if (!luaLoadMixScript(i)) {
	  return;
	}
  }

  // Load custom function scripts
  for (int i=0; i<NUM_CFN; i++) {
    if (!luaLoadFunctionScript(i)) {
      return;
    }
  }

  // Load custom telemetry scripts
  for (int i=0; i<MAX_SCRIPTS; i++) {
    if (!luaLoadTelemetryScript(i)) {
      return;
    }
  }
  if (!luaLoadTelemetryScript(TELEMETRY_VOLTAGES_SCREEN)) {
    return;
  }
  if (!luaLoadTelemetryScript(TELEMETRY_AFTER_FLIGHT_SCREEN)) {
    return;
  }
}

char lua_warning_str[WARNING_LINE_LEN+1];
char lua_warning_info[WARNING_LINE_LEN+1];

void luaError(uint8_t error)
{
  const char *msg = lua_tostring(L, -1);
  if (msg) {
#if defined(SIMU)
    if (!strncmp(msg, "./", 2)) msg += 2;
#else
    if (!strncmp(msg, "/SCRIPTS/", 9)) msg += 9;
#endif
    strncpy(lua_warning_str, msg, WARNING_LINE_LEN);
    lua_warning_str[WARNING_LINE_LEN] = '\0';
    POPUP_WARNING(lua_warning_str);
    for (int i=0; i<WARNING_LINE_LEN; i++) {
      if (msg[i] == ':' && msg[i+1] == ' ') {
        lua_warning_str[i] = '\0';
        strncpy(lua_warning_info, &msg[i+2], WARNING_LINE_LEN);
        lua_warning_info[WARNING_LINE_LEN] = '\0';
        SET_WARNING_INFO(lua_warning_info, WARNING_LINE_LEN, 0);
      }
    }
  }
  else {
    switch (error) {
      case SCRIPT_SYNTAX_ERROR:
        msg = "Script syntax error";
        break;
      case SCRIPT_KILLED:
        msg = "Script killed";
        break;
      case SCRIPT_PANIC:
        msg = "Script panic";
        break;
    }
    POPUP_WARNING(msg);
  }
}

void luaExec(const char *filename)
{
  luaInit();
  if (luaState != INTERPRETER_PANIC) {
    standaloneScript.state = SCRIPT_NOFILE;
    int result = luaLoad(filename, standaloneScript);
    // TODO the same with run ...
    if (result == SCRIPT_OK) {
      luaState = INTERPRETER_RUNNING_STANDALONE_SCRIPT;
    }
    else {
      luaError(result);
      luaState = INTERPRETER_RELOAD_PERMANENT_SCRIPTS;
    }
  }
}

void luaDoOneRunStandalone(uint8_t evt)
{
  static uint8_t luaDisplayStatistics = false;

  if (standaloneScript.state == SCRIPT_OK && standaloneScript.run) {
    SET_LUA_INSTRUCTIONS_COUNT(MANUAL_SCRIPTS_MAX_INSTRUCTIONS);
    lua_rawgeti(L, LUA_REGISTRYINDEX, standaloneScript.run);
    lua_pushinteger(L, evt);
    if (lua_pcall(L, 1, 1, 0) == 0) {
      if (!lua_isnumber(L, -1)) {
        if (instructionsPercent > 100) {
          TRACE("Script killed");
          standaloneScript.state = SCRIPT_KILLED;
          luaState = INTERPRETER_RELOAD_PERMANENT_SCRIPTS;
        }
        else if (lua_isstring(L, -1)) {
          char nextScript[_MAX_LFN+1];
          strncpy(nextScript, lua_tostring(L, -1), _MAX_LFN);
          nextScript[_MAX_LFN] = '\0';
          luaExec(nextScript);
        }
        else {
          TRACE("Script error");
          standaloneScript.state = SCRIPT_SYNTAX_ERROR;
          luaState = INTERPRETER_RELOAD_PERMANENT_SCRIPTS;
        }
      }
      else {
        int scriptResult = lua_tointeger(L, -1);
        lua_pop(L, 1);  /* pop returned value */
        if (scriptResult != 0) {
          TRACE("Script finished with status %d", scriptResult);
          standaloneScript.state = SCRIPT_NOFILE;
          luaState = INTERPRETER_RELOAD_PERMANENT_SCRIPTS;
          return;
        }
        else if (luaDisplayStatistics) {
          lcd_hline(0, 7*FH-1, lcdLastPos+FW, ERASE);
          lcd_puts(0, 7*FH, "GV Use: ");
          lcd_outdezAtt(lcdLastPos, 7*FH, luaGetMemUsed(), LEFT);
          lcd_putc(lcdLastPos, 7*FH, 'b');
          lcd_hline(0, 7*FH-2, lcdLastPos+FW, FORCE);
          lcd_vlineStip(lcdLastPos+FW, 7*FH-2, FH+2, SOLID, FORCE);
        }
      }
    }
    else {
      TRACE("Script error: %s", lua_tostring(L, -1));
      standaloneScript.state = (instructionsPercent > 100 ? SCRIPT_KILLED : SCRIPT_SYNTAX_ERROR);
      luaState = INTERPRETER_RELOAD_PERMANENT_SCRIPTS;
    }

    if (standaloneScript.state != SCRIPT_OK) {
      luaError(standaloneScript.state);
      luaState = INTERPRETER_RELOAD_PERMANENT_SCRIPTS;
    }

    if (evt == EVT_KEY_LONG(KEY_EXIT)) {
      TRACE("Script force exit");
      killEvents(evt);
      standaloneScript.state = SCRIPT_NOFILE;
      luaState = INTERPRETER_RELOAD_PERMANENT_SCRIPTS;
    }
    else if (evt == EVT_KEY_LONG(KEY_MENU)) {
      killEvents(evt);
      luaDisplayStatistics = !luaDisplayStatistics;
    }
  }
}

void luaDoOneRunPermanentScript(uint8_t evt, int i)
{
  ScriptInternalData & sid = scriptInternalData[i];
  if (sid.state == SCRIPT_OK) {
    SET_LUA_INSTRUCTIONS_COUNT(PERMANENT_SCRIPTS_MAX_INSTRUCTIONS);
    int inputsCount = 0;
#if defined(SIMU) || defined(DEBUG)
    const char *filename;
#endif
    ScriptInputsOutputs * sio = NULL;
#if SCRIPT_MIX_FIRST > 0
    if (sid.reference >= SCRIPT_MIX_FIRST && sid.reference <= SCRIPT_MIX_LAST) {
#else
    if (sid.reference <= SCRIPT_MIX_LAST) {
#endif
      ScriptData & sd = g_model.scriptsData[sid.reference-SCRIPT_MIX_FIRST];
      sio = &scriptInputsOutputs[sid.reference-SCRIPT_MIX_FIRST];
      inputsCount = sio->inputsCount;
#if defined(SIMU) || defined(DEBUG)
      filename = sd.file;
#endif
      lua_rawgeti(L, LUA_REGISTRYINDEX, sid.run);
      for (int j=0; j<sio->inputsCount; j++) {
        if (sio->inputs[j].type == 1)
          luaGetValueAndPush((uint8_t)sd.inputs[j]);
        else
          lua_pushinteger(L, sd.inputs[j] + sio->inputs[j].def);
      }
    }
    else if (sid.reference >= SCRIPT_FUNC_FIRST && sid.reference <= SCRIPT_FUNC_LAST) {
      CustomFnData & fn = g_model.funcSw[sid.reference-SCRIPT_FUNC_FIRST];
      if (!getSwitch(fn.swtch)) {
        return;
      }
#if defined(SIMU) || defined(DEBUG)
      filename = fn.play.name;
#endif
      lua_rawgeti(L, LUA_REGISTRYINDEX, sid.run);
    }
    else {
#if defined(SIMU) || defined(DEBUG)
      filename = "[telem]";
#endif
      if (g_menuStack[0]==menuTelemetryFrsky && sid.reference==SCRIPT_TELEMETRY_FIRST+s_frsky_view) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, sid.run);
        lua_pushinteger(L, evt);
        inputsCount = 1;
      }
      else if (sid.background) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, sid.background);
      }
      else {
        return;
      }
    }
    if (lua_pcall(L, inputsCount, sio ? sio->outputsCount : 0, 0) == 0) {
      if (sio) {
        for (int j=sio->outputsCount-1; j>=0; j--) {
          if (!lua_isnumber(L, -1)) {
            sid.state = (instructionsPercent > 100 ? SCRIPT_KILLED : SCRIPT_SYNTAX_ERROR);
            TRACE("Script %8s disabled", filename);
            break;
          }
          sio->outputs[j].value = lua_tointeger(L, -1);
          lua_pop(L, 1);
        }
      }
    }
    else {
      if (instructionsPercent > 100) {
        TRACE("Script %8s killed", filename);
        sid.state = SCRIPT_KILLED;
      }
      else {
        TRACE("Script %8s error: %s", filename, lua_tostring(L, -1));
        sid.state = SCRIPT_SYNTAX_ERROR;
      }
    }

    if (sid.state != SCRIPT_OK) {
      luaFree(sid);
    }
    else {
      if (instructionsPercent > sid.instructions) {
        sid.instructions = instructionsPercent;
      }
    }
  }
}

void luaDoGc()
{
  if (L) {
    PROTECT_LUA() {
      lua_gc(L, LUA_GCCOLLECT, 0);
#if defined(SIMU) || defined(DEBUG)
      static int lastgc = 0;
      int gc = luaGetMemUsed();
      if (gc != lastgc) {
        lastgc = gc;
        TRACE("GC Use: %dbytes", gc);
      }
#endif
    }
    else {
      // we disable Lua for the rest of the session
      luaDisable();
    }
    UNPROTECT_LUA();
  }
}

void luaTask(uint8_t evt)
{
  lcd_locked = false;

  uint32_t t0 = get_tmr10ms();
  static uint32_t lastLuaTime = 0;
  uint16_t interval = (lastLuaTime == 0 ? 0 : (t0 - lastLuaTime));
  lastLuaTime = t0;
  if (interval > maxLuaInterval) {
    maxLuaInterval = interval;
  }

  if (luaState == INTERPRETER_PANIC) {
	return;
  }

  if (luaState & INTERPRETER_RUNNING_STANDALONE_SCRIPT) {
    PROTECT_LUA() {
      luaDoOneRunStandalone(evt);
    }
    else {
      luaDisable();
      return;
    }
    UNPROTECT_LUA();
  }
  else {
    // model scripts
    if (luaState & INTERPRETER_RELOAD_PERMANENT_SCRIPTS) {
      luaState = 0;
      luaInit();
      if (luaState == INTERPRETER_PANIC) return;
      luaLoadPermanentScripts();
      if (luaState == INTERPRETER_PANIC) return;
    }

    for (int i=0; i<luaScriptsCount; i++) {
      PROTECT_LUA() {
        luaDoOneRunPermanentScript(evt, i);
      }
      else {
        luaDisable();
        return;
      }
      UNPROTECT_LUA();
    }
  }

  luaDoGc();

  t0 = get_tmr10ms() - t0;
  if (t0 > maxLuaDuration) {
    maxLuaDuration = t0;
  }
}

int luaGetMemUsed()
{
  return (lua_gc(L, LUA_GCCOUNT, 0) << 10) + lua_gc(L, LUA_GCCOUNTB, 0);
}
