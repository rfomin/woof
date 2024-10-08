//
//  Copyright (C) 1999 by
//  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
//  Copyright (C) 2023 Fabian Greffrath
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
// DESCRIPTION:  Heads-up displays
//
//-----------------------------------------------------------------------------

// killough 5/3/98: remove unnecessary headers

#include <stdlib.h>
#include <string.h>

#include "d_deh.h" /* Ty 03/27/98 - externalization of mapnamesx arrays */
#include "d_event.h"
#include "d_items.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "dstrings.h"
#include "hu_coordinates.h"
#include "hu_crosshair.h"
#include "hu_lib.h"
#include "hu_obituary.h"
#include "hu_stuff.h"
#include "i_timer.h" // time_scale
#include "i_video.h" // fps
#include "m_config.h"
#include "m_input.h"
#include "m_misc.h"
#include "m_swap.h"
#include "p_mobj.h"
#include "r_main.h"
#include "r_state.h"
#include "r_voxel.h"
#include "s_sound.h"
#include "sounds.h"
#include "st_stuff.h" /* jff 2/16/98 need loc of status bar */
#include "u_mapinfo.h"
#include "u_scanner.h"
#include "v_fmt.h"
#include "v_video.h"

// global heads up display controls

int hud_active;       //jff 2/17/98 controls heads-up display mode 
boolean hud_displayed;    //jff 2/23/98 turns heads-up display on/off
boolean hud_secret_message; // "A secret is revealed!" message
static int hud_widget_font;
static boolean hud_widget_layout;

int hud_type; // Crispy HUD or Boom variants
boolean draw_crispy_hud;

//
// Locally used constants, shortcuts.
//
// Ty 03/28/98 -
// These four shortcuts modifed to reflect char ** of mapnamesx[]
#define HU_TITLE  (*mapnames[(gameepisode-1)*9+gamemap-1])
#define HU_TITLE2 (*mapnames2[gamemap-1])
#define HU_TITLEP (*mapnamesp[gamemap-1])
#define HU_TITLET (*mapnamest[gamemap-1])

static const char *chat_macros[] =    // Ty 03/27/98 - *not* externalized
{
  HUSTR_CHATMACRO0,
  HUSTR_CHATMACRO1,
  HUSTR_CHATMACRO2,
  HUSTR_CHATMACRO3,
  HUSTR_CHATMACRO4,
  HUSTR_CHATMACRO5,
  HUSTR_CHATMACRO6,
  HUSTR_CHATMACRO7,
  HUSTR_CHATMACRO8,
  HUSTR_CHATMACRO9
};

char **player_names[] =
{
  &s_HUSTR_PLRGREEN,
  &s_HUSTR_PLRINDIGO,
  &s_HUSTR_PLRBROWN,
  &s_HUSTR_PLRRED
};

//jff 3/17/98 translate player colmap to text color ranges
int plyrcoltran[MAXPLAYERS]={CR_GREEN,CR_GRAY,CR_BROWN,CR_RED};

static player_t *plr;

// font sets
static hu_font_t big_font = {.space_width = 4, .tab_width = 15, .tab_mask = ~15},
                 sml_font = {.space_width = 5, .tab_width =  7, .tab_mask =  ~7};
static hu_font_t *doom_font = &big_font, *boom_font = &sml_font;
static hu_font_t *monospaced_font = &sml_font;
patch_t **hu_font = big_font.patches;

static int CR_BLUE = CR_BLUE1;

// widgets

static char hud_stringbuffer[HU_MAXLINELENGTH];

static inline void InitStringBuffer(const char *const s)
{
  strncpy(hud_stringbuffer, s, sizeof(hud_stringbuffer));
}

// [FG] Vanilla widgets point to a boolean variable (*on) to determine
//      if they are enabled, always big_font, mostly left-aligned
static hu_multiline_t w_title;
static hu_multiline_t w_message;
static hu_multiline_t w_chat;
static hu_multiline_t w_secret; // [crispy] secret message widget

// [FG] special pony, per-player chat input buffer
static hu_line_t w_inputbuffer[MAXPLAYERS];

// [FG] Boom widgets are built using builder() functions
static hu_multiline_t w_ammo;   //jff 2/16/98 new ammo widget for hud
static hu_multiline_t w_armor;  //jff 2/16/98 new armor widget for hud
static hu_multiline_t w_health; //jff 2/16/98 new health widget for hud
static hu_multiline_t w_keys;   //jff 2/16/98 new keys widget for hud
static hu_multiline_t w_weapon; //jff 2/16/98 new weapon widget for hud

static hu_multiline_t w_compact;

// [FG] extra Boom widgets, that need to be explicitly enabled
static hu_multiline_t w_monsec; //jff 2/16/98 new kill/secret widget for hud
static hu_multiline_t w_sttime; // time above status bar
static hu_multiline_t w_coord;
static hu_multiline_t w_fps;
static hu_multiline_t w_rate;
static hu_multiline_t w_cmd;
static hu_multiline_t w_speed;

#define MAX_HUDS 3
#define MAX_WIDGETS 20

static hu_widget_t widgets[MAX_HUDS][MAX_WIDGETS];

static void HU_ParseHUD (void);

static char       chat_dest[MAXPLAYERS];
boolean           chat_on;
static boolean    message_on;
static boolean    has_message;       // killough 12/98
boolean           message_dontfuckwithme;
static boolean    message_nottobefuckedwith;
static int        message_counter;
static int        message_count;     // killough 11/98
static int        chat_count;        // killough 11/98
static boolean    secret_on;
static int        secret_counter;

static boolean    message_centered;
static boolean    message_colorized;

boolean           show_messages;
boolean           show_toggle_messages;
boolean           show_pickup_messages;

static boolean    hud_map_announce;
static boolean    title_on;
static int        title_counter;

static boolean    headsupactive = false;

//jff 2/16/98 hud supported automap colors added
int hudcolor_titl;  // color range of automap level title
int hudcolor_xyco;  // color range of new coords on automap
//jff 2/16/98 hud text colors, controls added
static int hudcolor_mesg;  // color range of scrolling messages
static int hudcolor_chat;  // color range of chat lines
static int hud_msg_lines;  // number of message lines in window
static boolean message_list;      // killough 11/98: made global

static int message_timer  = HU_MSGTIMEOUT * (1000/TICRATE);     // killough 11/98
static int chat_msg_timer = HU_MSGTIMEOUT * (1000/TICRATE);     // killough 11/98

//
// Builtin map names.
// The actual names can be found in DStrings.h.
//
// Ty 03/27/98 - externalized map name arrays - now in d_deh.c
// and converted to arrays of pointers to char *
// See modified HUTITLEx macros

// key tables
// jff 5/10/98 french support removed, 
// as it was not being used and couldn't be easily tested
//
const char shiftxform[] =
{
  0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
  11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
  21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
  31,
  ' ', '!', '"', '#', '$', '%', '&',
  '"', // shift-'
  '(', ')', '*', '+',
  '<', // shift-,
  '_', // shift--
  '>', // shift-.
  '?', // shift-/
  ')', // shift-0
  '!', // shift-1
  '@', // shift-2
  '#', // shift-3
  '$', // shift-4
  '%', // shift-5
  '^', // shift-6
  '&', // shift-7
  '*', // shift-8
  '(', // shift-9
  ':',
  ':', // shift-;
  '<',
  '+', // shift-=
  '>', '?', '@',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
  'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
  '[', // shift-[
  '!', // shift-backslash - OH MY GOD DOES WATCOM SUCK
  ']', // shift-]
  '"', '_',
  '\'', // shift-`
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
  'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
  '{', '|', '}', '~', 127
};

static boolean VANILLAMAP(int e, int m)
{
  if (gamemode == commercial)
    return (e == 1 && m > 0 && m <=32);
  else
    return (e > 0 && e <= 4 && m > 0 && m <= 9);
}

struct {
  char **str;
  const int cr;
  const char *col;
} static const colorize_strings[] = {
  // [Woof!] colorize keycard and skull key messages
  {&s_GOTBLUECARD, CR_BLUE2, " blue "},
  {&s_GOTBLUESKUL, CR_BLUE2, " blue "},
  {&s_GOTREDCARD,  CR_RED,  " red "},
  {&s_GOTREDSKULL, CR_RED,  " red "},
  {&s_GOTYELWCARD, CR_GOLD, " yellow "},
  {&s_GOTYELWSKUL, CR_GOLD, " yellow "},
  {&s_PD_BLUEC,    CR_BLUE2, " blue "},
  {&s_PD_BLUEK,    CR_BLUE2, " blue "},
  {&s_PD_BLUEO,    CR_BLUE2, " blue "},
  {&s_PD_BLUES,    CR_BLUE2, " blue "},
  {&s_PD_REDC,     CR_RED,  " red "},
  {&s_PD_REDK,     CR_RED,  " red "},
  {&s_PD_REDO,     CR_RED,  " red "},
  {&s_PD_REDS,     CR_RED,  " red "},
  {&s_PD_YELLOWC,  CR_GOLD, " yellow "},
  {&s_PD_YELLOWK,  CR_GOLD, " yellow "},
  {&s_PD_YELLOWO,  CR_GOLD, " yellow "},
  {&s_PD_YELLOWS,  CR_GOLD, " yellow "},

  // [Woof!] colorize multi-player messages
  {&s_HUSTR_PLRGREEN,  CR_GREEN, "Green: "},
  {&s_HUSTR_PLRINDIGO, CR_GRAY,  "Indigo: "},
  {&s_HUSTR_PLRBROWN,  CR_BROWN, "Brown: "},
  {&s_HUSTR_PLRRED,    CR_RED,   "Red: "},
};

static char* PrepareColor(const char *str, const char *col)
{
    char *str_replace, col_replace[16];

    M_snprintf(col_replace, sizeof(col_replace),
               "\x1b%c%s\x1b%c", '0'+CR_ORIG, col, '0'+CR_ORIG);
    str_replace = M_StringReplace(str, col, col_replace);

    return str_replace;
}

static void UpdateColor(char *str, int cr)
{
    int i;
    int len = strlen(str);

    if (!message_colorized)
    {
        cr = CR_ORIG;
    }

    for (i = 0; i < len; ++i)
    {
        if (str[i] == '\x1b' && i + 1 < len)
        {
          str[i + 1] = '0'+cr;
          break;
        }
    }
}

void HU_ResetMessageColors(void)
{
    int i;

    for (i = 0; i < arrlen(colorize_strings); i++)
    {
        UpdateColor(*colorize_strings[i].str, colorize_strings[i].cr);
    }
}

static crange_idx_e CRByHealth(int health, int maxhealth, boolean invul)
{
  if (invul)
    return CR_GRAY;

  health = 100 * health / maxhealth;

  if (health < health_red)
    return CR_RED;
  else if (health < health_yellow)
    return CR_GOLD;
  else if (health <= health_green)
    return CR_GREEN;
  else
    return CR_BLUE;
}

byte* HU_ColorByHealth(int health, int maxhealth, boolean invul)
{
  const crange_idx_e cr = CRByHealth(health, maxhealth, invul);

  return colrngs[cr];
}

// [FG] support centered player messages

static void HU_set_centered_message(void)
{
  int i, j;

  for (i = 0; i < MAX_HUDS; i++)
  {
    hu_widget_t *const w = widgets[i];

    for (j = 0; w[j].multiline; j++)
    {
      if (w[j].multiline == &w_message)
      {
        w[j].h_align = message_centered ? align_center : w[j].h_align_orig;
      }
    }
  }
}

//
// HU_Init()
//
// Initialize the heads-up display, text that overwrites the primary display
//
// Passed nothing, returns nothing
//
void HU_Init(void)
{
  int i, j;
  char buffer[9];

  // load the heads-up font
  for (i = 0, j = HU_FONTSTART; i < HU_FONTSIZE; i++, j++)
  {
    M_snprintf(buffer, sizeof(buffer), "STCFN%.3d", j);
    if (W_CheckNumForName(buffer) != -1)
      big_font.patches[i] = V_CachePatchName(buffer, PU_STATIC);

    if ('0' <= j && j <= '9')
    {
      M_snprintf(buffer, sizeof(buffer), "DIG%.1d", j - 48);
      sml_font.patches[i] = V_CachePatchName(buffer, PU_STATIC);
    }
    else if ('A' <= j && j <= 'Z')
    {
      M_snprintf(buffer, sizeof(buffer), "DIG%c", j);
      sml_font.patches[i] = V_CachePatchName(buffer, PU_STATIC);
    }
    else if (j > 122)
    {
      M_snprintf(buffer, sizeof(buffer), "STBR%.3d", j);
      sml_font.patches[i] = V_CachePatchName(buffer, PU_STATIC);
    }
    else
    {
      M_snprintf(buffer, sizeof(buffer), "DIG%.2d", j);
      if (W_CheckNumForName(buffer) != -1)
        sml_font.patches[i] = V_CachePatchName(buffer, PU_STATIC);
    }

    // [FG] small font available, big font unavailable
    if (big_font.patches[i] == NULL && sml_font.patches[i] != NULL)
    {
      big_font.patches[i] = sml_font.patches[i];
    }
    // [FG] big font available, small font unavailable
    else if (big_font.patches[i] != NULL && sml_font.patches[i] == NULL)
    {
      sml_font.patches[i] = big_font.patches[i];
    }
    // [FG] both fonts unavailable, fall back to '!'
    else if (big_font.patches[i] == NULL && sml_font.patches[i] == NULL)
    {
      sml_font.patches[i] =
      big_font.patches[i] = big_font.patches[0];
    }
  }

  //jff 2/26/98 load patches for keys and double keys
  for (i = HU_FONTSIZE, j = 0; j < 6; i++, j++)
  {
    M_snprintf(buffer, sizeof(buffer), "STKEYS%.1d", j);
    sml_font.patches[i] =
    big_font.patches[i] = V_CachePatchName(buffer, PU_STATIC);
  }

  // [FG] calculate font height once right here
  sml_font.line_height = SHORT(sml_font.patches['A'-HU_FONTSTART]->height) + 1;
  big_font.line_height = SHORT(big_font.patches['A'-HU_FONTSTART]->height) + 1;

  // [FG] support crosshair patches from extras.wad
  HU_InitCrosshair();

  HU_InitCommandHistory();

  HU_InitObituaries();

  HU_ParseHUD();
  HU_set_centered_message();

  // [Woof!] prepare player messages for colorization
  for (i = 0; i < arrlen(colorize_strings); i++)
  {
    *colorize_strings[i].str = PrepareColor(*colorize_strings[i].str, colorize_strings[i].col);
  }

  HU_ResetMessageColors();
}

static inline void HU_cond_build_widget (hu_multiline_t *const multiline, boolean cond)
{
  if (cond && multiline->built == false)
  {
    multiline->builder();
    multiline->built = true;
  }
}

static boolean hud_pending;

void HU_disable_all_widgets (void)
{
  hu_widget_t *w = widgets[hud_active];

  while (w->multiline)
  {
    w->multiline->built = false;
    w++;
  }

  hud_pending = true;
}

//
// HU_Stop()
//
// Make the heads-up displays inactive
//
// Passed nothing, returns nothing
//
void HU_Stop(void)
{
  headsupactive = false;
}

//
// HU_Start(void)
//
// Create and initialize the heads-up widgets, software machines to
// maintain, update, and display information over the primary display
//
// This routine must be called after any change to the heads up configuration
// in order for the changes to take effect in the actual displays
//
// Passed nothing, returns nothing
//

static void HU_widget_build_ammo (void);
static void HU_widget_build_armor (void);
static void HU_widget_build_coord (void);
static void HU_widget_build_fps (void);
static void HU_widget_build_rate (void);
static void HU_widget_build_cmd(void);
static void HU_widget_build_health (void);
static void HU_widget_build_keys (void);
static void HU_widget_build_frag (void);
static void HU_widget_build_monsec(void);
static void HU_widget_build_sttime(void);
static void HU_widget_build_title (void);
static void HU_widget_build_weapon (void);
static void HU_widget_build_compact (void);
static void HU_widget_build_speed(void);

static hu_multiline_t *w_stats;

void HU_Start(void)
{
  int i;

  if (headsupactive)                    // stop before starting
    HU_Stop();

  plr = &players[displayplayer];        // killough 3/7/98
  message_on = false;
  message_dontfuckwithme = false;
  message_nottobefuckedwith = false;
  chat_on = false;
  secret_on = false;

  // killough 11/98:
  message_counter = 0;
  message_count = (message_timer  * TICRATE) / 1000 + 1;
  chat_count    = (chat_msg_timer * TICRATE) / 1000 + 1;

  // create the message widget
  HUlib_init_multiline(&w_message, message_list ? hud_msg_lines : 1,
                       &doom_font, colrngs[hudcolor_mesg],
                       &message_on, NULL);

  // create the secret message widget
  HUlib_init_multiline(&w_secret, 1,
                       &doom_font, colrngs[CR_GOLD],
                       &secret_on, NULL);

  // create the chat widget
  HUlib_init_multiline(&w_chat, 1,
                       &doom_font, colrngs[hudcolor_chat],
                       &chat_on, NULL);
  // [FG] only the chat widget draws a cursor
  w_chat.drawcursor = true;

  // create the inputbuffer widgets, one per player
  for (i = 0; i < MAXPLAYERS; i++)
  {
    HUlib_clear_line(&w_inputbuffer[i]);
  }

  //jff 2/16/98 added some HUD widgets
  // create the map title widget
  HUlib_init_multiline(&w_title, 1,
                       &doom_font, colrngs[hudcolor_titl],
                       &title_on, HU_widget_build_title);
  // [FG] built only once right here
  w_title.builder();

  // create the hud health widget
  HUlib_init_multiline(&w_health, 1,
                       &boom_font, colrngs[CR_GREEN],
                       NULL, HU_widget_build_health);

  // create the hud armor widget
  HUlib_init_multiline(&w_armor, 1,
                       &boom_font, colrngs[CR_GREEN],
                       NULL, HU_widget_build_armor);

  // create the hud ammo widget
  HUlib_init_multiline(&w_ammo, 1,
                       &boom_font, colrngs[CR_GOLD],
                       NULL, HU_widget_build_ammo);

  // create the hud weapons widget
  HUlib_init_multiline(&w_weapon, 1,
                       &boom_font, colrngs[CR_GRAY],
                       NULL, HU_widget_build_weapon);

  // create the hud keys widget
  HUlib_init_multiline(&w_keys, 1,
                       &boom_font, colrngs[CR_GRAY],
                       NULL, deathmatch ? HU_widget_build_frag : HU_widget_build_keys);

  HUlib_init_multiline(&w_compact, hud_widget_layout ? 3 : 1,
                       &boom_font, colrngs[CR_GRAY],
                       NULL, HU_widget_build_compact);

  // create the hud monster/secret widget
  HUlib_init_multiline(&w_monsec, hud_widget_layout ? 3 : 1,
                       &boom_font, colrngs[CR_GRAY],
                       NULL, HU_widget_build_monsec);
  // [FG] in deathmatch: w_keys.builder = HU_widget_build_frag()
  w_stats = deathmatch ? &w_keys : &w_monsec;

  HUlib_init_multiline(&w_sttime, 1,
                       &boom_font, colrngs[CR_GRAY],
                       NULL, HU_widget_build_sttime);

  // create the automaps coordinate widget
  if (hud_player_coords == HUD_WIDGET_ADVANCED)
  {
    HUlib_init_multiline(&w_coord, 12,
                         &monospaced_font, colrngs[CR_GRAY],
                         NULL, HU_widget_build_coord);
  }
  else
  {
    HUlib_init_multiline(&w_coord, hud_widget_layout ? 3 : 1,
                         &boom_font, colrngs[hudcolor_xyco],
                         NULL, HU_widget_build_coord);
  }

  HUlib_init_multiline(&w_fps, 1,
                       &boom_font, colrngs[hudcolor_xyco],
                       NULL, HU_widget_build_fps);

  HUlib_init_multiline(&w_rate, (voxels_rendering ? 2 : 1),
                       &boom_font, colrngs[hudcolor_xyco],
                       NULL, HU_widget_build_rate);
  // [FG] draw the IDRATE widget exclusively
  w_rate.exclusive = true;

  HUlib_init_multiline(&w_cmd, hud_command_history_size,
                       &monospaced_font, colrngs[hudcolor_xyco],
                       NULL, HU_widget_build_cmd);
  // Draw command history bottom up.
  w_cmd.bottomup = true;

  HUlib_init_multiline(&w_speed, 1,
                       &boom_font, colrngs[hudcolor_xyco],
                       NULL, HU_widget_build_speed);

  HU_set_centered_message();

  HU_disable_all_widgets();
  HUlib_set_margins();

  // init crosshair
  if (hud_crosshair)
    HU_StartCrosshair();

  // now allow the heads-up display to run
  headsupactive = true;
}

static void HU_widget_build_title (void)
{
  InitStringBuffer("");

  char *s, *n;

  if (gamemapinfo && gamemapinfo->levelname)
  {
    if (gamemapinfo->label)
      s = gamemapinfo->label;
    else
      s = gamemapinfo->mapname;

    if (s == gamemapinfo->mapname || U_CheckField(s))
    {
      M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer), "%s: ", s);
    }
    s = gamemapinfo->levelname;
  }
  else if (gamestate == GS_LEVEL)
  {
    if (VANILLAMAP(gameepisode, gamemap))
    {
      s = (gamemode != commercial) ? HU_TITLE :
          (gamemission == pack_tnt) ? HU_TITLET :
          (gamemission == pack_plut) ? HU_TITLEP :
          HU_TITLE2;
    }
    // WADs like pl2.wad have a MAP33, and rely on the layout in the
    // Vanilla executable, where it is possible to overflow the end of one
    // array into the next.
    else if (gamemode == commercial && gamemap >= 33 && gamemap <= 35)
    {
      s = (gamemission == doom2) ? (*mapnamesp[gamemap-33]) :
          (gamemission == pack_plut) ? (*mapnamest[gamemap-33]) : "";
    }
    else
    {
      // initialize the map title widget with the generic map lump name
      s = MAPNAME(gameepisode, gamemap);
    }
  }
  else
  {
    s = "";
  }

  // [FG] cap at line break
  if ((n = strchr(s, '\n')))
  {
    *n = '\0';
  }

  if (hud_map_announce && leveltime == 0)
  {
    title_counter = HU_MSGTIMEOUT2;
  }

  M_StringConcat(hud_stringbuffer, s, sizeof(hud_stringbuffer));

  HUlib_add_string_to_cur_line(&w_title, hud_stringbuffer);
}

// do the hud ammo display
static crange_idx_e CRByAmmo(const int ammo, const int fullammo, int ammopct)
{
  // backpack changes thresholds (ammo widget)
  if (plr->backpack && !hud_backpack_thresholds && fullammo)
    ammopct = (100 * ammo) / (fullammo / 2);

  // set the display color from the percentage of total ammo held
  if (ammopct < ammo_red)
    return CR_RED;
  else if (ammopct < ammo_yellow)
    return CR_GOLD;
  else if (ammopct > 100) // more than max threshold w/o backpack
    return CR_BLUE;
  else
    return CR_GREEN;
}

static void HU_widget_build_ammo (void)
{
  InitStringBuffer("AMM ");

  int fullammo = plr->maxammo[weaponinfo[plr->readyweapon].ammo];
  int i = 4;

  // special case for weapon with no ammo selected - blank bargraph + N/A
  if (weaponinfo[plr->readyweapon].ammo == am_noammo || fullammo == 0)
  {
    if (hud_type == HUD_TYPE_BOOM)
    {
      strcat(hud_stringbuffer, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f");
    }
    strcat(hud_stringbuffer, "N/A");
    w_ammo.cr = colrngs[CR_GRAY];
  }
  else
  {
    int ammo = plr->ammo[weaponinfo[plr->readyweapon].ammo];
    int ammopct = (100 * ammo) / fullammo;
    int ammobars = ammopct / 4;

    // build the bargraph string
    if (hud_type == HUD_TYPE_BOOM)
    {
      // full bargraph chars
      for (i = 4; i < 4 + ammobars / 4;)
        hud_stringbuffer[i++] = 123;

      // plus one last character with 0, 1, 2, 3 bars
      switch (ammobars % 4)
      {
        case 0:
          break;
        case 1:
          hud_stringbuffer[i++] = 126;
          break;
        case 2:
          hud_stringbuffer[i++] = 125;
          break;
        case 3:
          hud_stringbuffer[i++] = 124;
          break;
      }

      // pad string with blank bar characters
      while (i < 4 + 7)
        hud_stringbuffer[i++] = 127;
      hud_stringbuffer[i] = '\0';
    }

    // build the numeric amount init string
    M_snprintf(hud_stringbuffer + i, sizeof(hud_stringbuffer) - i,
               "%3d/%3d", ammo, fullammo);

    const crange_idx_e cr = CRByAmmo(ammo, fullammo, ammopct);
    w_ammo.cr = colrngs[cr];
  }

  // transfer the init string to the widget
  HUlib_add_string_to_cur_line(&w_ammo, hud_stringbuffer);
}

// do the hud health display
static void HU_widget_build_health (void)
{
  InitStringBuffer("HEL ");

  int i = 4;
  int healthbars = (st_health > 100) ? 25 : (st_health / 4);

  // build the bargraph string
  if (hud_type == HUD_TYPE_BOOM)
  {
    // full bargraph chars
    for (i = 4; i < 4 + healthbars / 4;)
      hud_stringbuffer[i++] = 123;

    // plus one last character with 0, 1, 2, 3 bars
    switch (healthbars % 4)
    {
      case 0:
        break;
      case 1:
        hud_stringbuffer[i++] = 126;
        break;
      case 2:
        hud_stringbuffer[i++] = 125;
        break;
      case 3:
        hud_stringbuffer[i++] = 124;
        break;
    }

    // pad string with blank bar characters
    while (i < 4 + 7)
      hud_stringbuffer[i++] = 127;
    hud_stringbuffer[i] = '\0';
  }

  // build the numeric amount init string
  M_snprintf(hud_stringbuffer + i, sizeof(hud_stringbuffer) - i,
             "%3d", st_health);

  // set the display color from the amount of health posessed
  w_health.cr = HU_ColorByHealth(plr->health, 100, st_invul);

  // transfer the init string to the widget
  HUlib_add_string_to_cur_line(&w_health, hud_stringbuffer);
}

// do the hud armor display
static crange_idx_e CRByArmor(void)
{
  // color of armor depends on type
  if (hud_armor_type)
  {
    return
      st_invul ? CR_GRAY :
      (plr->armortype == 0) ? CR_RED :
      (plr->armortype == 1) ? CR_GREEN :
      CR_BLUE;
  }
  else
  {
    const int armor = plr->armorpoints;

    // set the display color from the amount of armor posessed
    return
      st_invul ? CR_GRAY :
      (armor < armor_red) ? CR_RED :
      (armor < armor_yellow) ? CR_GOLD :
      (armor <= armor_green) ? CR_GREEN :
      CR_BLUE;
  }
}

static void HU_widget_build_armor (void)
{
  InitStringBuffer("ARM ");

  int i = 4;
  int armorbars = (st_armor > 100) ? 25 : (st_armor / 4);

  // build the bargraph string
  if (hud_type == HUD_TYPE_BOOM)
  {
    // full bargraph chars
    for (i = 4; i < 4 + armorbars / 4;)
      hud_stringbuffer[i++] = 123;

    // plus one last character with 0, 1, 2, 3 bars
    switch (armorbars % 4)
    {
      case 0:
        break;
      case 1:
        hud_stringbuffer[i++] = 126;
        break;
      case 2:
        hud_stringbuffer[i++] = 125;
        break;
      case 3:
        hud_stringbuffer[i++] = 124;
        break;
    }

    // pad string with blank bar characters
    while (i < 4 + 7)
      hud_stringbuffer[i++] = 127;
    hud_stringbuffer[i] = '\0';
  }

  // build the numeric amount init string
  M_snprintf(hud_stringbuffer + i, sizeof(hud_stringbuffer) - i, "%3d", st_armor);

  const crange_idx_e cr = CRByArmor();
  w_armor.cr = colrngs[cr];

  // transfer the init string to the widget
  HUlib_add_string_to_cur_line(&w_armor, hud_stringbuffer);
}

static void HU_widget_build_compact (void)
{
  const crange_idx_e cr_health = CRByHealth(plr->health, 100, st_invul);
  const crange_idx_e cr_armor = CRByArmor();

  const ammotype_t ammotype = weaponinfo[plr->readyweapon].ammo;
  const int ammo = plr->ammo[ammotype];
  const int fullammo = plr->maxammo[ammotype];
  const boolean noammo = (ammotype == am_noammo || fullammo == 0);

  if (hud_widget_layout)
  {
    M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer),
    "\x1b%cHEL \x1b%c%3d", '0'+CR_GRAY, '0'+cr_health, st_health);
    HUlib_add_string_to_cur_line(&w_compact, hud_stringbuffer);

    M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer),
    "\x1b%cARM \x1b%c%3d", '0'+CR_GRAY, '0'+cr_armor, st_armor);
    HUlib_add_string_to_cur_line(&w_compact, hud_stringbuffer);

    if (noammo)
    {
      M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer),
      "\x1b%cAMM N/A", '0'+CR_GRAY);
    }
    else
    {
      const int ammopct = (100 * ammo) / fullammo;
      const crange_idx_e cr_ammo = CRByAmmo(ammo, fullammo, ammopct);

      M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer),
      "\x1b%cAMM \x1b%c%3d/%3d", '0'+CR_GRAY, '0'+cr_ammo, ammo, fullammo);
    }
    HUlib_add_string_to_cur_line(&w_compact, hud_stringbuffer);
  }
  else
  {
    if (noammo)
    {
      M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer),
      "\x1b%cHEL \x1b%c%3d \x1b%cARM \x1b%c%3d \x1b%cAMM N/A",
      '0'+CR_GRAY, '0'+cr_health, st_health,
      '0'+CR_GRAY, '0'+cr_armor, st_armor,
      '0'+CR_GRAY);
    }
    else
    {
      const int ammopct = (100 * ammo) / fullammo;
      const crange_idx_e cr_ammo = CRByAmmo(ammo, fullammo, ammopct);

      M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer),
      "\x1b%cHEL \x1b%c%3d \x1b%cARM \x1b%c%3d \x1b%cAMM \x1b%c%3d/%3d",
      '0'+CR_GRAY, '0'+cr_health, st_health,
      '0'+CR_GRAY, '0'+cr_armor, st_armor,
      '0'+CR_GRAY, '0'+cr_ammo, ammo, fullammo);
    }
    HUlib_add_string_to_cur_line(&w_compact, hud_stringbuffer);
  }
}

// do the hud weapon display
static void HU_widget_build_weapon (void)
{
  InitStringBuffer("WEA ");

  int i = 4, w, ammo, fullammo, ammopct;

  // do each weapon that exists in current gamemode
  for (w = 0; w <= wp_supershotgun; w++) //jff 3/4/98 show fists too, why not?
  {
    int ok = 1;

    //jff avoid executing for weapons that do not exist
    switch (gamemode)
    {
      case shareware:
        if (w >= wp_plasma && w != wp_chainsaw)
          ok = 0;
        break;
      case retail:
      case registered:
        if (w >= wp_supershotgun && !ALLOW_SSG)
          ok = 0;
        break;
      default:
      case commercial:
        break;
    }
    if (!ok)
      continue;

    ammo = plr->ammo[weaponinfo[w].ammo];
    fullammo = plr->maxammo[weaponinfo[w].ammo];

    // skip weapons not currently posessed
    if (!plr->weaponowned[w])
      continue;

    // backpack changes thresholds (weapon widget)
    if (plr->backpack && !hud_backpack_thresholds)
      fullammo /= 2;

    ammopct = fullammo ? (100 * ammo) / fullammo : 100;

    // display each weapon number in a color related to the ammo for it
    hud_stringbuffer[i++] = '\x1b'; //jff 3/26/98 use ESC not '\' for paths
    if (weaponinfo[w].ammo == am_noammo) //jff 3/14/98 show berserk on HUD
      hud_stringbuffer[i++] = (w == wp_fist && !plr->powers[pw_strength]) ? '0'+CR_GRAY : '0'+CR_GREEN;
    else if (ammopct < ammo_red)
      hud_stringbuffer[i++] = '0'+CR_RED;
    else if (ammopct < ammo_yellow)
      hud_stringbuffer[i++] = '0'+CR_GOLD;
    else if (ammopct > 100) // more than max threshold w/o backpack
      hud_stringbuffer[i++] = '0'+CR_BLUE;
    else
      hud_stringbuffer[i++] = '0'+CR_GREEN;

    hud_stringbuffer[i++] = '0'+w+1;
    hud_stringbuffer[i++] = ' ';
    hud_stringbuffer[i] = '\0';
  }

  // transfer the init string to the widget
  HUlib_add_string_to_cur_line(&w_weapon, hud_stringbuffer);
}

static void HU_widget_build_keys (void)
{
  const char hud_keysstr[] = { 'K', 'E', 'Y', '\x1b', '0'+CR_NONE, ' ', '\0' };
  InitStringBuffer(hud_keysstr);

  int i = 6, k;

  // build text string whose characters call out graphic keys
  for (k = 0; k < 6; k++)
  {
    // skip keys not possessed
    if (!plr->cards[k])
      continue;

    hud_stringbuffer[i++] = HU_FONTEND + k + 1; // key number plus HU_FONTEND is char for key
    hud_stringbuffer[i++] = ' ';   // spacing
    hud_stringbuffer[i++] = ' ';
  }

  // [Alaux] Blink missing keys *after* possessed keys
  for (k = 0; k < 6; k++)
  {
    if (plr->cards[k])
      continue;

    switch (ST_BlinkKey(plr, k % 3))
    {
      case KEYBLINK_CARD:
        if (k >= 3)
          continue;
        break;

      case KEYBLINK_SKULL:
        if (k < 3)
          continue;
        break;

      case KEYBLINK_BOTH:
        break;

      default:
        continue;
    }

    hud_stringbuffer[i++] = HU_FONTEND + k + 1;
    hud_stringbuffer[i++] = ' ';
    hud_stringbuffer[i++] = ' ';
  }

  hud_stringbuffer[i] = '\0';

  // transfer the built string (frags or key title) to the widget
  HUlib_add_string_to_cur_line(&w_keys, hud_stringbuffer);
}

static inline int HU_top (int i, const int idx1, const int top1)
{
  if (idx1 > -1)
  {
    char numbuf[32], *s;

    M_snprintf(numbuf, sizeof(numbuf), "%5d", top1);
    // make frag count in player's color via escape code

    hud_stringbuffer[i++] = '\x1b'; //jff 3/26/98 use ESC not '\' for paths
    hud_stringbuffer[i++] = '0' + plyrcoltran[idx1 & 3];
    s = numbuf;
    while (*s)
      hud_stringbuffer[i++] = *s++;
  }
  return i;
}

static void HU_widget_build_frag (void)
{
  const char hud_fragstr[] = { 'F', 'R', 'G', '\x1b', '0'+CR_ORIG, ' ', '\0' };
  InitStringBuffer(hud_fragstr);

  int i = 6, k;

  int top1 = -999, top2 = -999, top3 = -999, top4 = -999;
  int idx1 = -1, idx2 = -1, idx3 = -1, idx4 = -1;
  int fragcount, m;

  // scan thru players
  for (k = 0; k < MAXPLAYERS; k++)
  {
    // skip players not in game
    if (!playeringame[k])
      continue;

    fragcount = 0;

    // compute number of times they've fragged each player
    // minus number of times they've been fragged by them
    for (m = 0; m < MAXPLAYERS; m++)
    {
      if (!playeringame[m])
        continue;
      fragcount += (m != k) ? players[k].frags[m] : -players[k].frags[m];
    }

    // very primitive sort of frags to find top four
    if (fragcount > top1)
    {
      top4 = top3; top3 = top2; top2 = top1; top1 = fragcount;
      idx4 = idx3; idx3 = idx2; idx2 = idx1; idx1 = k;
    }
    else if (fragcount > top2)
    {
      top4 = top3; top3 = top2; top2 = fragcount;
      idx4 = idx3; idx3 = idx2; idx2 = k;
    }
    else if (fragcount > top3)
    {
      top4 = top3; top3 = fragcount;
      idx4 = idx3; idx3 = k;
    }
    else if (fragcount > top4)
    {
      top4 = fragcount;
      idx4 = k;
    }
  }

  // killough 11/98: replaced cut-and-pasted code with function

  // if the biggest number exists,
  // put it in the init string
  i = HU_top(i, idx1, top1);

  // if the second biggest number exists,
  // put it in the init string
  i = HU_top(i, idx2, top2);

  // if the third biggest number exists,
  // put it in the init string
  i = HU_top(i, idx3, top3);

  // if the fourth biggest number exists,
  // put it in the init string
  i = HU_top(i, idx4, top4);

  hud_stringbuffer[i] = '\0';

  // transfer the built string (frags or key title) to the widget
  HUlib_add_string_to_cur_line(&w_keys, hud_stringbuffer);
}

static void HU_widget_build_monsec(void)
{
  int i;
  int fullkillcount, fullitemcount, fullsecretcount;
  int killcolor, itemcolor, secretcolor;
  int kill_percent_count;

  fullkillcount = 0;
  fullitemcount = 0;
  fullsecretcount = 0;
  kill_percent_count = 0;

  for (i = 0; i < MAXPLAYERS; ++i)
  {
    if (playeringame[i])
    {
      fullkillcount += players[i].killcount - players[i].maxkilldiscount;
      fullitemcount += players[i].itemcount;
      fullsecretcount += players[i].secretcount;
      kill_percent_count += players[i].killcount;
    }
  }

  if (respawnmonsters)
  {
    fullkillcount = kill_percent_count;
    max_kill_requirement = totalkills;
  }

  killcolor = (fullkillcount >= max_kill_requirement) ? '0'+CR_BLUE : '0'+CR_GRAY;
  secretcolor = (fullsecretcount >= totalsecret) ? '0'+CR_BLUE : '0'+CR_GRAY;
  itemcolor = (fullitemcount >= totalitems) ? '0'+CR_BLUE : '0'+CR_GRAY;

  if (hud_widget_layout)
  {
    M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer),
      "\x1b%cK\t\x1b%c%d/%d", ('0'+CR_RED), killcolor, fullkillcount, max_kill_requirement);
    HUlib_add_string_to_cur_line(&w_monsec, hud_stringbuffer);

    M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer),
      "\x1b%cI\t\x1b%c%d/%d", ('0'+CR_RED), itemcolor, fullitemcount, totalitems);
    HUlib_add_string_to_cur_line(&w_monsec, hud_stringbuffer);

    M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer),
      "\x1b%cS\t\x1b%c%d/%d", ('0'+CR_RED), secretcolor, fullsecretcount, totalsecret);
    HUlib_add_string_to_cur_line(&w_monsec, hud_stringbuffer);
  }
  else
  {
    M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer),
      "\x1b%cK \x1b%c%d/%d \x1b%cI \x1b%c%d/%d \x1b%cS \x1b%c%d/%d",
      '0'+CR_RED, killcolor, fullkillcount, max_kill_requirement,
      '0'+CR_RED, itemcolor, fullitemcount, totalitems,
      '0'+CR_RED, secretcolor, fullsecretcount, totalsecret);

    HUlib_add_string_to_cur_line(&w_monsec, hud_stringbuffer);
  }
}

static void HU_widget_build_sttime(void)
{
  InitStringBuffer("");

  int offset = 0;

  if ((hud_level_time & HUD_WIDGET_HUD     && !automapactive) ||
      (hud_level_time & HUD_WIDGET_AUTOMAP &&  automapactive))
  {
    if (time_scale != 100)
    {
      offset += M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer), "\x1b%c%d%% ",
                           '0'+CR_BLUE, time_scale);
    }

    if (totalleveltimes)
    {
      const int time = (totalleveltimes + leveltime) / TICRATE;

      offset += M_snprintf(hud_stringbuffer + offset, sizeof(hud_stringbuffer) - offset,
                           "\x1b%c%d:%02d ",
                           '0'+CR_GREEN, time/60, time%60);
    }

    if (!plr->btuse_tics)
    {
      M_snprintf(hud_stringbuffer + offset, sizeof(hud_stringbuffer) - offset,
                 "\x1b%c%d:%05.2f\t",
                 '0'+CR_GRAY, leveltime / TICRATE / 60,
                 (float)(leveltime % (60 * TICRATE)) / TICRATE);
    }
  }

  if (plr->btuse_tics)
  {
    M_snprintf(hud_stringbuffer + offset, sizeof(hud_stringbuffer) - offset,
               "\x1b%cU %d:%05.2f\t",
               '0'+CR_GOLD, plr->btuse / TICRATE / 60, 
               (float)(plr->btuse % (60 * TICRATE)) / TICRATE);
  }

  HUlib_add_string_to_cur_line(&w_sttime, hud_stringbuffer);
}

void HU_widget_rebuild_sttime(void)
{
  HU_widget_build_sttime();
}

static void HU_widget_build_coord (void)
{
  fixed_t x,y,z; // killough 10/98:
  void AM_Coordinates(const mobj_t *, fixed_t *, fixed_t *, fixed_t *);

  if (hud_player_coords == HUD_WIDGET_ADVANCED)
  {
    HU_BuildCoordinatesEx(&w_coord, plr->mo, hud_stringbuffer,
                          sizeof(hud_stringbuffer));
    return;
  }

  // killough 10/98: allow coordinates to display non-following pointer
  AM_Coordinates(plr->mo, &x, &y, &z);

  //jff 2/16/98 output new coord display
  if (hud_widget_layout)
  {
    M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer), "X\t\x1b%c%d", '0'+CR_GRAY, x >> FRACBITS);
    HUlib_add_string_to_cur_line(&w_coord, hud_stringbuffer);

    M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer), "Y\t\x1b%c%d", '0'+CR_GRAY, y >> FRACBITS);
    HUlib_add_string_to_cur_line(&w_coord, hud_stringbuffer);

    M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer), "Z\t\x1b%c%d", '0'+CR_GRAY, z >> FRACBITS);
    HUlib_add_string_to_cur_line(&w_coord, hud_stringbuffer);
  }
  else
  {
    M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer), "X \x1b%c%d \x1b%cY \x1b%c%d \x1b%cZ \x1b%c%d",
            '0'+CR_GRAY, x >> FRACBITS, '0'+hudcolor_xyco,
            '0'+CR_GRAY, y >> FRACBITS, '0'+hudcolor_xyco,
            '0'+CR_GRAY, z >> FRACBITS);

    HUlib_add_string_to_cur_line(&w_coord, hud_stringbuffer);
  }
}

static void HU_widget_build_fps (void)
{
  M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer), "\x1b%c%d \x1b%cFPS",
             '0'+CR_GRAY, fps, '0'+CR_ORIG);
  HUlib_add_string_to_cur_line(&w_fps, hud_stringbuffer);
}

static void HU_widget_build_rate (void)
{
  M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer),
             "Sprites %4d Segs %4d Visplanes %4d   \x1b%cFPS %3d %dx%d\x1b%c",
             rendered_vissprites, rendered_segs, rendered_visplanes,
             '0'+CR_GRAY, fps, video.width, video.height, '0'+CR_ORIG);
  HUlib_add_string_to_cur_line(&w_rate, hud_stringbuffer);

  if (voxels_rendering)
  {
    M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer), " Voxels %4d", rendered_voxels);
    HUlib_add_string_to_cur_line(&w_rate, hud_stringbuffer);
  }
}

static void HU_widget_build_cmd(void)
{
  HU_BuildCommandHistory(&w_cmd);
}

int speedometer;

static void HU_widget_build_speed(void)
{
  static const double factor[] = {TICRATE, 2.4003, 525.0 / 352.0};
  static const char *units[] = {"ups", "km/h", "mph"};
  const int type = speedometer - 1;
  const double dx = FIXED2DOUBLE(plr->mo->x - plr->mo->oldx);
  const double dy = FIXED2DOUBLE(plr->mo->y - plr->mo->oldy);
  const double dz = FIXED2DOUBLE(plr->mo->z - plr->mo->oldz);
  const double speed = sqrt(dx*dx + dy*dy + dz*dz) * factor[type];

  M_snprintf(hud_stringbuffer, sizeof(hud_stringbuffer), "\x1b%c%.*f \x1b%c%s",
             '0' + CR_GRAY, type && speed ? 1 : 0, speed,
             '0' + CR_ORIG, units[type]);
  HUlib_add_string_to_cur_line(&w_speed, hud_stringbuffer);
}

// [crispy] print a bar indicating demo progress at the bottom of the screen
boolean HU_DemoProgressBar(boolean force)
{
  const int progress = video.unscaledw * playback_tic / playback_totaltics;
  static int old_progress = 0;

  if (old_progress < progress)
  {
    old_progress = progress;
  }
  else if (!force)
  {
    return false;
  }

  V_FillRect(0, SCREENHEIGHT - 2, progress, 1, v_darkest_color);
  V_FillRect(0, SCREENHEIGHT - 1, progress, 1, v_lightest_color);

  return true;
}

// [FG] level stats and level time widgets
int hud_player_coords, hud_level_stats, hud_level_time;

boolean hud_time_use;

//
// HU_Drawer()
//
// Draw all the pieces of the heads-up display
//
// Passed nothing, returns nothing
//
void HU_Drawer(void)
{
  hu_widget_t *w = widgets[hud_active];

  if (hud_pending)
    return;

  HUlib_reset_align_offsets();

  while (w->multiline)
  {
    if ((w->multiline->on && *w->multiline->on) || w->multiline->built)
    {
      HUlib_draw_widget(w);
    }
    w++;
  }

  if (draw_crispy_hud)
  {
    ST_Drawer (false, true);
  }
}

// [FG] draw Time widget on intermission screen
void WI_DrawWidgets(void)
{
  HUlib_reset_align_offsets();

  if (hud_level_time & HUD_WIDGET_HUD)
  {
    const hu_widget_t w = {&w_sttime, align_left, align_top};
    // leveltime is already added to totalleveltimes before WI_Start()
    //HU_widget_build_sttime();
    HUlib_draw_widget(&w);
  }

  if (STRICTMODE(hud_command_history))
  {
    hu_widget_t *w = widgets[hud_active];

    while (w->multiline)
    {
      if (w->multiline == &w_cmd
          && ((w->multiline->on && *w->multiline->on) || w->multiline->built))
      {
        w_cmd.built = false;
        HU_cond_build_widget(&w_cmd, true);
        HUlib_draw_widget(w);
        break;
      }
      w++;
    }
  }
}

//
// HU_Erase()
//
// Erase hud display lines that can be trashed by small screen display
//
// Passed nothing, returns nothing
//

void HU_Erase(void)
{
  hu_widget_t *w = widgets[hud_active];

  if (automapactive || !scaledviewx)
    return;

  HUlib_reset_align_offsets();

  while (w->multiline)
  {
    if (w->multiline->on || w->multiline->built)
    {
      HUlib_erase_widget(w);
    }
    w++;
  }
}

//
// HU_Ticker()
//
// Update the hud displays once per frame
//
// Passed nothing, returns nothing
//

static boolean bsdown; // Is backspace down?
static int bscounter;

void HU_Ticker(void)
{
  plr = &players[displayplayer];         // killough 3/7/98

  HU_disable_all_widgets();
  draw_crispy_hud = false;

  if ((automapactive && hud_widget_font == 1) || 
      (!automapactive && hud_widget_font == 2) ||
      hud_widget_font == 3)
  {
    boom_font = &big_font;
    CR_BLUE = CR_BLUE2;
  }
  else
  {
    boom_font = &sml_font;
    CR_BLUE = CR_BLUE1;
  }

  // wait a few tics before sending a backspace character
  if (bsdown && bscounter++ > 9)
  {
    HUlib_add_key_to_cur_line(&w_chat, KEY_BACKSPACE);
    bscounter = 8;
  }

  // tick down message counter if message is up
  if (message_counter && !--message_counter)
    message_on = message_nottobefuckedwith = false;

  if (secret_counter && !--secret_counter)
    secret_on = false;

  // [Woof!] "A secret is revealed!" message
  if (plr->secretmessage)
  {
    HUlib_add_string_to_cur_line(&w_secret, plr->secretmessage);
    plr->secretmessage = NULL;
    secret_on = true;
    secret_counter = HU_MSGTIMEOUT2;
  }

  // if messages on, or "Messages Off" is being displayed
  // this allows the notification of turning messages off to be seen
  // display message if necessary

  if ((show_messages || message_dontfuckwithme) && plr->message &&
      (!message_nottobefuckedwith || message_dontfuckwithme))
  {
    //post the message to the message widget
    HUlib_add_string_to_cur_line(&w_message, plr->message);

    // [FG] empty messages clear the whole widget
    if (plr->message[0] == '\0')
      HUlib_clear_all_lines(&w_message);

    // clear the message to avoid posting multiple times
    plr->message = 0;

    message_on = true;       // note a message is displayed
    // start the message persistence counter	      
    message_counter = message_count;

    has_message = true;        // killough 12/98

    // transfer "Messages Off" exception to the "being displayed" variable
    message_nottobefuckedwith = message_dontfuckwithme;

    // clear the flag that "Messages Off" is being posted
    message_dontfuckwithme = 0;
  }

  // check for incoming chat characters
  if (netgame)
  {
    int i, rc;
    char c;

    for (i = 0; i < MAXPLAYERS; i++)
    {
      if (!playeringame[i])
        continue;

      if (i != consoleplayer &&
          (c = players[i].cmd.chatchar))
      {
        if (c <= HU_BROADCAST)
          chat_dest[i] = c;
        else
        {
          if (c >= 'a' && c <= 'z')
            c = (char) shiftxform[(unsigned char) c];

          rc = HUlib_add_key_to_line(&w_inputbuffer[i], c);
          if (rc && c == KEY_ENTER)
          {
            if (w_inputbuffer[i].len &&
                (chat_dest[i] == consoleplayer + 1 ||
                chat_dest[i] == HU_BROADCAST))
            {
              HUlib_add_strings_to_cur_line(&w_message,
                                            *player_names[i],
                                            w_inputbuffer[i].line);

              has_message = true; // killough 12/98
              message_nottobefuckedwith = true;
              message_on = true;
              message_counter = chat_count; // killough 11/98
              S_StartSoundPitch(0, gamemode == commercial ?
                              sfx_radio : sfx_tink, PITCH_NONE);
            }
            HUlib_clear_line(&w_inputbuffer[i]);
          }
        }
        players[i].cmd.chatchar = 0;
      }
    }
  }

  // draw the automap widgets if automap is displayed

  if (title_counter)
  {
    title_counter--;
  }

  if (automapactive)
  {
    HU_cond_build_widget(w_stats, hud_level_stats & HUD_WIDGET_AUTOMAP);
    HU_cond_build_widget(&w_sttime, hud_level_time & HUD_WIDGET_AUTOMAP || plr->btuse_tics);
    HU_cond_build_widget(&w_coord, STRICTMODE(hud_player_coords == HUD_WIDGET_AUTOMAP
                                              || hud_player_coords >= HUD_WIDGET_ALWAYS));

    title_on = true;
  }
  else
  {
    HU_cond_build_widget(w_stats, hud_level_stats & HUD_WIDGET_HUD);
    HU_cond_build_widget(&w_sttime, hud_level_time & HUD_WIDGET_HUD || plr->btuse_tics);
    HU_cond_build_widget(&w_coord, STRICTMODE(hud_player_coords >= HUD_WIDGET_HUD));

    title_on = (title_counter > 0);
  }

  HU_cond_build_widget(&w_fps, plr->cheats & CF_SHOWFPS);
  HU_cond_build_widget(&w_rate, plr->cheats & CF_RENDERSTATS);
  HU_cond_build_widget(&w_cmd, STRICTMODE(hud_command_history));
  HU_cond_build_widget(&w_speed, speedometer > 0);

  if (hud_displayed &&
      scaledviewheight == SCREENHEIGHT &&
      automap_off)
  {
    if (hud_type == HUD_TYPE_CRISPY)
    {
      if (hud_active > 0)
      {
        draw_crispy_hud = true;
      }
    }
    else
    {
      HU_cond_build_widget(&w_weapon, true);
      HU_cond_build_widget(&w_armor, true);
      HU_cond_build_widget(&w_health, true);
      HU_cond_build_widget(&w_ammo, true);
      HU_cond_build_widget(&w_keys, true);

      HU_cond_build_widget(&w_compact, true);
    }
  }

  if (plr->btuse_tics)
    plr->btuse_tics--;

  // update crosshair properties
  if (hud_crosshair)
    HU_UpdateCrosshair();

  hud_pending = false;
}

#define QUEUESIZE   128

static char chatchars[QUEUESIZE];
static int  head = 0;
static int  tail = 0;

//
// HU_queueChatChar()
//
// Add an incoming character to the circular chat queue
//
// Passed the character to queue, returns nothing
//
void HU_queueChatChar(char c)
{
  if (((head + 1) & (QUEUESIZE-1)) == tail)
    displaymsg("%s", HUSTR_MSGU);
  else
    {
      chatchars[head++] = c;
      head &= QUEUESIZE-1;
    }
}

//
// HU_dequeueChatChar()
//
// Remove the earliest added character from the circular chat queue
//
// Passed nothing, returns the character dequeued
//
char HU_dequeueChatChar(void)
{
  char c;

  if (head != tail)
    {
      c = chatchars[tail++];
      tail &= QUEUESIZE-1;
    }
  else
    c = 0;
  return c;
}

//
// HU_Responder()
//
// Responds to input events that affect the heads up displays
//
// Passed the event to respond to, returns true if the event was handled
//

boolean HU_Responder(event_t *ev)
{
  static char   lastmessage[HU_MAXLINELENGTH+1];
  const char    *macromessage;
  boolean   eatkey = false;
  static boolean  shiftdown = false;
  static boolean  altdown = false;
  int     c;
  int     i;
  int     numplayers;

  static int    num_nobrainers = 0;

  c = (ev->type == ev_keydown) ? ev->data1.i : 0;

  numplayers = 0;
  for (i=0 ; i<MAXPLAYERS ; i++)
    numplayers += playeringame[i];

  if (ev->data1.i == KEY_RSHIFT)
    {
      shiftdown = ev->type == ev_keydown;
      return false;
    }

  if (ev->data1.i == KEY_RALT)
    {
      altdown = ev->type == ev_keydown;
      return false;
    }

  if (M_InputActivated(input_chat_backspace))
  {
    bsdown = true;
    bscounter = 0;
    c = KEY_BACKSPACE;
  }
  else if (M_InputDeactivated(input_chat_backspace))
  {
    bsdown = false;
    bscounter = 0;
  }

  if (ev->type == ev_keyup)
    return false;

  if (!chat_on)
    {
      if (M_InputActivated(input_chat_enter))                         // phares
        {
	  //jff 2/26/98 toggle list of messages

	  if (has_message)
	    {
		  message_counter = message_count;
		  message_on = true;
	    }
          eatkey = true;
        }  //jff 2/26/98 no chat if message review is displayed
      else // killough 10/02/98: no chat if demo playback
        if (!demoplayback)
          {
	    if (netgame && M_InputActivated(input_chat))
	      {
		eatkey = chat_on = true;
		HUlib_clear_cur_line(&w_chat);
		HU_queueChatChar(HU_BROADCAST);
	      }//jff 2/26/98
	    else    // killough 11/98: simplify
	      if (netgame && numplayers > 2)
		for (i=0; i<MAXPLAYERS ; i++)
		  if (M_InputActivated(input_chat_dest0 + i))
		  {
		    if (i == consoleplayer)
		      displaymsg("%s", 
			++num_nobrainers <  3 ? HUSTR_TALKTOSELF1 :
	                  num_nobrainers <  6 ? HUSTR_TALKTOSELF2 :
	                  num_nobrainers <  9 ? HUSTR_TALKTOSELF3 :
	                  num_nobrainers < 32 ? HUSTR_TALKTOSELF4 :
                                                HUSTR_TALKTOSELF5 );
                  else
                    if (playeringame[i])
                      {
                        eatkey = chat_on = true;
                        HUlib_clear_cur_line(&w_chat);
                        HU_queueChatChar((char)(i+1));
                        break;
                      }
		  }
          }
    }//jff 2/26/98 no chat functions if message review is displayed
  else
      {
        if (M_InputActivated(input_chat_enter))
        {
          c = KEY_ENTER;
        }

        // send a macro
        if (altdown)
          {
            c = c - '0';
            if (c < 0 || c > 9)
              return false;
            // fprintf(stderr, "got here\n");
            macromessage = chat_macros[c];
      
            // kill last message with a '\n'
            HU_queueChatChar(KEY_ENTER); // DEBUG!!!                // phares
      
            // send the macro message
            while (*macromessage)
              HU_queueChatChar(*macromessage++);
            HU_queueChatChar(KEY_ENTER);                            // phares
      
            // leave chat mode and notify that it was sent
            chat_on = false;
            strcpy(lastmessage, chat_macros[c]);
            displaymsg("%s", lastmessage);
            eatkey = true;
          }
        else
          {
            if (shiftdown || (c >= 'a' && c <= 'z'))
              c = shiftxform[c];
            eatkey = HUlib_add_key_to_cur_line(&w_chat, c);
            if (eatkey)
              HU_queueChatChar(c);

            if (c == KEY_ENTER)                                     // phares
              {
                chat_on = false;
                if (w_chat.lines[0]->len)
                  {
                    strcpy(lastmessage, w_chat.lines[0]->line);
                    displaymsg("%s", lastmessage);
                  }
              }
            else
              if (c == KEY_ESCAPE)                               // phares
                chat_on = false;
          }
      }
  return eatkey;
}

// [FG] dynamic HUD alignment

static const struct {
  const char *name, *altname;
  hu_multiline_t *const multiline;
} multiline_names[] = {
    {"title",   NULL,     &w_title},
    {"message", NULL,     &w_message},
// [FG] TODO due to its variable width and the trailing cursor,
//      the w_chat widget *must* currently remain left-aligned
//  {"chat",    NULL,     &w_chat},
    {"secret",  NULL,     &w_secret},

    {"ammo",    NULL,     &w_ammo},
    {"armor",   NULL,     &w_armor},
    {"health",  NULL,     &w_health},
    {"keys",    NULL,     &w_keys},
    {"weapon", "weapons", &w_weapon},

    {"compact", NULL,     &w_compact},

    {"monsec", "stats",   &w_monsec},
    {"sttime", "time",    &w_sttime},
    {"coord",  "coords",  &w_coord},
    {"fps",     NULL,     &w_fps},
    {"rate",    NULL,     &w_rate},
    {"cmd",    "commands", &w_cmd},
    {"speed",   NULL,     &w_speed},
    {NULL},
};

static boolean HU_ReplaceInWidgets (hu_multiline_t *multiline, int hud, align_t h_align, align_t v_align, int x, int y)
{
  int i;

  if (hud < 0 || hud >= MAX_HUDS)
  {
    return false;
  }

  for (i = 0; i < MAX_WIDGETS - 1; i++)
  {
    if (widgets[hud][i].multiline == NULL)
    {
      break;
    }

    if (widgets[hud][i].multiline == multiline)
    {
      widgets[hud][i].h_align = h_align;
      widgets[hud][i].v_align = v_align;
      widgets[hud][i].x = x;
      widgets[hud][i].y = y;

      // [FG] save original alignment
      widgets[hud][i].h_align_orig = widgets[hud][i].h_align;

      return true;
    }
  }

  return false;
}

static boolean HU_AppendToWidgets (hu_multiline_t *multiline, int hud, align_t h_align, align_t v_align, int x, int y)
{
  int i;

  if (hud < 0 || hud >= MAX_HUDS)
  {
    return false;
  }

  for (i = 0; i < MAX_WIDGETS - 1; i++)
  {
    if (widgets[hud][i].multiline == NULL)
    {
      break;
    }
  }

  if (i + 1 >= MAX_WIDGETS)
  {
    return false;
  }

  widgets[hud][i].multiline = multiline;
  widgets[hud][i].h_align = h_align;
  widgets[hud][i].v_align = v_align;
  widgets[hud][i].x = x;
  widgets[hud][i].y = y;

  // [FG] save original alignment
  widgets[hud][i].h_align_orig = widgets[hud][i].h_align;

  widgets[hud][i + 1].multiline = NULL;

  return true;
}

static boolean HU_AddToWidgets (hu_multiline_t *multiline, int hud, align_t h_align, align_t v_align, int x, int y)
{
  if (HU_ReplaceInWidgets(multiline, hud, h_align, v_align, x, y))
  {
    return true;
  }
  else if (HU_AppendToWidgets(multiline, hud, h_align, v_align, x, y))
  {
    return true;
  }

  return false;
}

static hu_multiline_t *HU_MultilineByName (const char *name)
{
  int i;

  for (i = 0; multiline_names[i].name; i++)
  {
    if (strcasecmp(name, multiline_names[i].name) == 0 ||
       (multiline_names[i].altname && strcasecmp(name, multiline_names[i].altname) == 0))
    {
      return multiline_names[i].multiline;
    }
  }

  return NULL;
}

static boolean HU_AddHUDCoords (char *name, int hud, int x, int y)
{
  hu_multiline_t *multiline = HU_MultilineByName(name);

  if (multiline == NULL)
  {
    return false;
  }

  // [FG] relative alignment to the edges
  if (x < 0)
  {
    x += SCREENWIDTH;
  }
  if (y < 0)
  {
    y += SCREENHEIGHT;
  }

  if (x < 0 || x >= SCREENWIDTH || y < 0 || y >= SCREENHEIGHT)
  {
    return false;
  }

  return HU_AddToWidgets(multiline, hud, align_direct, align_direct, x, y);
}

static boolean HU_AddHUDAlignment (char *name, int hud, char *alignstr)
{
  hu_multiline_t *multiline = HU_MultilineByName(name);

  if (multiline == NULL)
  {
    return false;
  }

  if (!strcasecmp(alignstr, "topleft")          || !strcasecmp(alignstr, "upperleft"))
  {
    return HU_AddToWidgets(multiline, hud, align_left, align_top, 0, 0);
  }
  else if (!strcasecmp(alignstr, "topright")    || !strcasecmp(alignstr, "upperright"))
  {
    return HU_AddToWidgets(multiline, hud, align_right, align_top, 0, 0);
  }
  else if (!strcasecmp(alignstr, "topcenter")   || !strcasecmp(alignstr, "uppercenter"))
  {
    return HU_AddToWidgets(multiline, hud, align_center, align_top, 0, 0);
  }
  else if (!strcasecmp(alignstr, "bottomleft")  || !strcasecmp(alignstr, "lowerleft"))
  {
    return HU_AddToWidgets(multiline, hud, align_left, align_bottom, 0, 0);
  }
  else if (!strcasecmp(alignstr, "bottomright") || !strcasecmp(alignstr, "lowerright"))
  {
    return HU_AddToWidgets(multiline, hud, align_right, align_bottom, 0, 0);
  }
  else if (!strcasecmp(alignstr, "bottomcenter")|| !strcasecmp(alignstr, "lowercenter"))
  {
    return HU_AddToWidgets(multiline, hud, align_center, align_bottom, 0, 0);
  }

  return false;
}

static void HU_ParseHUD (void)
{
  u_scanner_t *s;
  int hud;
  int lumpnum;
  const char *data;
  int length;

  // [FG] initialize HUDs with Vanilla Doom widgets
  for (hud = 0; hud < MAX_HUDS; hud++)
  {
    HU_AddToWidgets(&w_title,   hud, align_direct, align_bottom, 0, 0);
    HU_AddToWidgets(&w_message, hud, align_direct, align_top,    0, 0);
    HU_AddToWidgets(&w_chat,    hud, align_direct, align_top,    0, 0);
    HU_AddToWidgets(&w_secret , hud, align_center, align_secret, 0, 0);
  }

  if ((lumpnum = W_CheckNumForName("WOOFHUD")) == -1)
  {
    return;
  }

  data = W_CacheLumpNum(lumpnum, PU_CACHE);
  length = W_LumpLength(lumpnum);

  s = U_ScanOpen(data, length, "WOOFHUD");

  while (U_HasTokensLeft(s))
  {
    char *name;

    if (!U_CheckToken(s, TK_Identifier))
    {
      U_GetNextToken(s, true);
      continue;
    }

    if (!strcasecmp("HUD", s->string))
    {
      U_MustGetInteger(s);
      hud = s->number;

      if (hud < 0 || hud >= MAX_HUDS)
      {
        U_Error(s, "HUD (%d) must be between 0 and %d", hud, MAX_HUDS - 1);
      }

      continue;
    }

    name = M_StringDuplicate(s->string);

    if (U_CheckToken(s, TK_IntConst))
    {
      int x, y;

      x = s->number;
      U_MustGetInteger(s);
      y = s->number;

      if (!HU_AddHUDCoords(name, hud, x, y))
      {
        U_Error(s, "Cannot set coordinates for widget (%s)", name);
      }
    }
    else
    {
      char *align;

      U_MustGetToken(s, TK_Identifier);
      align = M_StringDuplicate(s->string);

      if (!HU_AddHUDAlignment(name, hud, align))
      {
        U_Error(s, "Cannot set alignment for widget (%s)", name);
      }

      free(align);
    }

    free(name);
  }

  U_ScanClose(s);
}

void HU_BindHUDVariables(void)
{
  M_BindBool("hud_displayed", &hud_displayed, NULL, false, ss_none, wad_yes,
             "Display HUD");
  M_BindNum("hud_active", &hud_active, NULL, 2, 0, 2, ss_stat, wad_yes,
            "HUD layout (by default: 0 = Minimal; 1 = Compact; 2 = Distributed)");
  M_BindNum("hud_level_stats", &hud_level_stats, NULL,
            HUD_WIDGET_OFF, HUD_WIDGET_OFF, HUD_WIDGET_ALWAYS,
            ss_stat, wad_no,
            "Show level stats (kills, items, and secrets) widget (1 = On automap; "
            "2 = On HUD; 3 = Always)");
  M_BindNum("hud_level_time", &hud_level_time, NULL,
            HUD_WIDGET_OFF, HUD_WIDGET_OFF, HUD_WIDGET_ALWAYS,
            ss_stat, wad_no,
            "Show level time widget (1 = On automap; 2 = On HUD; 3 = Always)");
  M_BindNum("hud_player_coords", &hud_player_coords, NULL,
            HUD_WIDGET_AUTOMAP, HUD_WIDGET_OFF, HUD_WIDGET_ADVANCED,
            ss_stat, wad_no,
            "Show player coordinates widget (1 = On automap; 2 = On HUD; 3 = Always; 4 = Advanced)");
  M_BindBool("hud_command_history", &hud_command_history, NULL, false, ss_stat,
             wad_no, "Show command history widget");
  BIND_NUM(hud_command_history_size, 10, 1, HU_MAXMESSAGES,
           "Number of commands to display for command history widget");
  BIND_BOOL(hud_hide_empty_commands, true,
            "Hide empty commands from command history widget");
  M_BindBool("hud_time_use", &hud_time_use, NULL, false, ss_stat, wad_no,
             "Show split time when pressing the use-button");
  M_BindNum("hud_type", &hud_type, NULL,
            HUD_TYPE_BOOM, HUD_TYPE_CRISPY, NUM_HUD_TYPES - 1,
            ss_stat, wad_no,
            "Fullscreen HUD type (0 = Crispy; 1 = Boom (No Bars); 2 = Boom)");
  M_BindBool("hud_backpack_thresholds", &hud_backpack_thresholds, NULL,
             true, ss_stat, wad_no, "Backpack changes thresholds");
  M_BindBool("hud_armor_type", &hud_armor_type, NULL, false, ss_stat, wad_no,
             "Armor count is colored based on armor type");
  M_BindBool("hud_widescreen_widgets", &hud_widescreen_widgets, NULL,
             true, ss_stat, wad_no, "Arrange widgets on widescreen edges");
  M_BindNum("hud_widget_font", &hud_widget_font, NULL,
            HUD_WIDGET_OFF, HUD_WIDGET_OFF, HUD_WIDGET_ALWAYS,
            ss_stat, wad_no,
            "Use standard Doom font for widgets (1 = On automap; 2 = On HUD; 3 "
            "= Always)");
  M_BindBool("hud_widget_layout", &hud_widget_layout, NULL,
             false, ss_stat, wad_no, "Widget layout (0 = Horizontal; 1 = Vertical)");
  M_BindNum("hud_crosshair", &hud_crosshair, NULL, 0, 0, 10 - 1, ss_stat, wad_no,
            "Crosshair");
  M_BindBool("hud_crosshair_health", &hud_crosshair_health, NULL,
             false, ss_stat, wad_no, "Change crosshair color based on player health");
  M_BindNum("hud_crosshair_target", &hud_crosshair_target, NULL,
            0, 0, 2, ss_stat, wad_no,
            "Change crosshair color when locking on target (1 = Highlight; 2 = Health)");
  M_BindBool("hud_crosshair_lockon", &hud_crosshair_lockon, NULL,
             false, ss_stat, wad_no, "Lock crosshair on target");
  M_BindNum("hud_crosshair_color", &hud_crosshair_color, NULL,
            CR_GRAY, CR_BRICK, CR_NONE, ss_stat, wad_no,
            "Default crosshair color");
  M_BindNum("hud_crosshair_target_color", &hud_crosshair_target_color, NULL,
            CR_YELLOW, CR_BRICK, CR_NONE, ss_stat, wad_no,
            "Crosshair color when aiming at target");

  M_BindNum("hudcolor_titl", &hudcolor_titl, NULL,
            CR_GOLD, CR_BRICK, CR_NONE, ss_none, wad_yes,
            "Color range used for automap level title");
  M_BindNum("hudcolor_xyco", &hudcolor_xyco, NULL,
            CR_GREEN, CR_BRICK, CR_NONE, ss_none, wad_yes,
            "Color range used for automap coordinates");

  BIND_BOOL(show_messages, true, "Show messages");
  M_BindBool("hud_secret_message", &hud_secret_message, NULL,
            true, ss_stat, wad_no, "Announce revealed secrets");
  M_BindBool("hud_map_announce", &hud_map_announce, NULL,
            false, ss_stat, wad_no, "Announce map titles");
  M_BindBool("show_toggle_messages", &show_toggle_messages, NULL,
            true, ss_stat, wad_no, "Show toggle messages");
  M_BindBool("show_pickup_messages", &show_pickup_messages, NULL,
             true, ss_stat, wad_no, "Show pickup messages");
  M_BindBool("show_obituary_messages", &show_obituary_messages, NULL,
             true, ss_stat, wad_no, "Show obituaries");

  M_BindNum("hudcolor_mesg", &hudcolor_mesg, NULL, CR_NONE, CR_BRICK, CR_NONE,
            ss_none, wad_yes, "Color range used for messages during play");
  M_BindNum("hudcolor_chat", &hudcolor_chat, NULL, CR_GOLD, CR_BRICK, CR_NONE,
            ss_none, wad_yes, "Color range used for chat messages and entry");
  BIND_NUM(hudcolor_obituary, CR_GRAY, CR_BRICK, CR_NONE,
           "Color range used for obituaries");

  BIND_NUM(message_timer, 4000, 0, UL, "Duration of normal Doom messages (ms)");
  BIND_NUM(chat_msg_timer, 4000, 0, UL, "Duration of chat messages (ms)");
  BIND_NUM(hud_msg_lines, 4, 1, HU_MAXMESSAGES, "Number of message lines for message list");
  M_BindBool("message_colorized", &message_colorized, NULL,
             false, ss_stat, wad_no, "Colorize player messages");
  M_BindBool("message_centered", &message_centered, NULL,
             false, ss_stat, wad_no, "Center messages horizontally");
  BIND_BOOL(message_list, false, "Use message list");

#define BIND_CHAT(num)                                                     \
    M_BindStr("chatmacro" #num, &chat_macros[(num)], HUSTR_CHATMACRO##num, \
              wad_yes, "Chat string associated with " #num " key")

  BIND_CHAT(0);
  BIND_CHAT(1);
  BIND_CHAT(2);
  BIND_CHAT(3);
  BIND_CHAT(4);
  BIND_CHAT(5);
  BIND_CHAT(6);
  BIND_CHAT(7);
  BIND_CHAT(8);
  BIND_CHAT(9);
}

//----------------------------------------------------------------------------
//
// $Log: hu_stuff.c,v $
// Revision 1.27  1998/05/10  19:03:41  jim
// formatted/documented hu_stuff
//
// Revision 1.26  1998/05/03  22:25:24  killough
// Provide minimal headers at top; nothing else
//
// Revision 1.25  1998/04/28  15:53:58  jim
// Fix message list bug in small screen mode
//
// Revision 1.24  1998/04/22  12:50:14  jim
// Fix lockout from dynamic message change
//
// Revision 1.23  1998/04/05  10:09:51  jim
// added STCFN096 lump
//
// Revision 1.22  1998/03/28  05:32:12  jim
// Text enabling changes for DEH
//
// Revision 1.19  1998/03/17  20:45:23  jim
// added frags to HUD
//
// Revision 1.18  1998/03/15  14:42:16  jim
// added green fist/chainsaw in HUD when berserk
//
// Revision 1.17  1998/03/10  07:07:15  jim
// Fixed display glitch in HUD cycle
//
// Revision 1.16  1998/03/09  11:01:48  jim
// fixed string overflow for DEH, added graphic keys
//
// Revision 1.15  1998/03/09  07:10:09  killough
// Use displayplayer instead of consoleplayer
//
// Revision 1.14  1998/03/05  00:57:37  jim
// Scattered HUD
//
// Revision 1.13  1998/03/04  11:50:48  jim
// Change automap coord display
//
// Revision 1.12  1998/02/26  22:58:26  jim
// Added message review display to HUD
//
// Revision 1.11  1998/02/23  14:20:51  jim
// Merged HUD stuff, fixed p_plats.c to support elevators again
//
// Revision 1.10  1998/02/23  04:26:07  killough
// really allow new hud stuff to be turned off COMPLETELY
//
// Revision 1.9  1998/02/22  12:51:26  jim
// HUD control on F5, z coord, spacing change
//
// Revision 1.7  1998/02/20  18:46:51  jim
// cleanup of HUD control
//
// Revision 1.6  1998/02/19  16:54:53  jim
// Optimized HUD and made more configurable
//
// Revision 1.5  1998/02/18  11:55:55  jim
// Fixed issues with HUD and reduced screen size
//
// Revision 1.3  1998/02/15  02:47:47  phares
// User-defined keys
//
// Revision 1.2  1998/01/26  19:23:22  phares
// First rev with no ^Ms
//
// Revision 1.1.1.1  1998/01/19  14:02:55  rand
// Lee's Jan 19 sources
//
//----------------------------------------------------------------------------
