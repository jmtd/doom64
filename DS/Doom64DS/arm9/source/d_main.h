#ifndef __D_MAIN__
#define __D_MAIN__

#include "doomtype.h"
#include "doomdef.h"
#include "m_fixed.h"
#include "d_player.h"

typedef enum
{
    ga_nothing,
    ga_loadlevel,
    ga_newgame,
    ga_loadgame,
    ga_exitdemo,
    ga_completed,
    ga_victory,
    ga_finale,
    ga_screenshot,
    ga_warplevel,
    ga_warpquick,
    ga_title
} gameaction_t;

//
// EVENT HANDLING
//

// Input event types.
typedef enum
{
    ev_btndown,
    ev_btnup,
    ev_btnheld
} evtype_t;

// Event structure.
typedef struct
{
    evtype_t    type;
    int         data;
} event_t;

//
// Button/action code definitions.
//
typedef enum
{
    // Press "Fire".
    BT_ATTACK		= 1,
    // Use button, to open doors, activate switches.
    BT_USE			= 2,
        
    // Flag: game events, not really buttons.
    BT_SPECIAL		= 0x80,
    BT_SPECIALMASK	= 3,
        
    // Flag, weapon change pending.
    // If true, the next 3 bits hold weapon num.
    BT_CHANGE		= 4,
    // The 3bit weapon mask and shift, convenience.
    BT_WEAPONMASK	= 0x78,
    BT_WEAPONSHIFT	= 3,

    // Pause the game.
    BTS_PAUSE		= 1
} buttoncode_t;

#define MAXEVENTS   64

extern  event_t events[MAXEVENTS];
extern  int     eventhead;
extern	int     eventtail;

void D_PostEvent(event_t* ev);

// Defaults for menu, methinks.
extern  skill_t     startskill;
extern	int         startmap;
extern gameaction_t gameaction;
extern gamestate_t  gamestate;
extern skill_t      gameskill;
extern int          gamemap;
extern int          nextmap;
extern int          gametic;
extern int          validcount;
extern int          totalkills;
extern int          totalitems;
extern int          totalsecret;
extern dboolean     nomonsters;
extern dboolean     respawnmonsters;
extern dboolean     respawnspecials;
extern dboolean     fastparm;
extern dboolean     demoplayback;
extern dboolean     respawnparm;    // checkparm of -respawn
extern dboolean     respawnitem;
extern dboolean     paused;

extern player_t players[MAXPLAYERS];
extern dboolean playeringame[MAXPLAYERS];
// Player spawn spots.
extern  mapthing_t  playerstarts[MAXPLAYERS];

// 1'st Player taking events
extern int consoleplayer;

// tics in game play for par
extern int leveltime;

// only true if packets are broadcast
extern dboolean netgame;

extern	int         rndindex;

extern	int         maketic;
extern ticcmd_t     netcmds[MAXPLAYERS][BACKUPTICS];
extern	int         ticdup;
extern	int         extratics;

void D_Printf(const char *s, ...);
void D_IncValidCount(void);
void D_DoomMain(void);
void D_UpdateTiccmd(void);
int D_MiniLoop(void(*start)(void), void(*stop)(void),
               void (*draw)(void), dboolean(*tick)(void));

int datoi(const char *str);
int dhtoi(char* str);
float datof(char *str);
dboolean dfcmp(float f1, float f2);

static inline int D_abs(x)
{
    fixed_t _t = (x),_s;
    _s = _t >> (8*sizeof _t-1);
    return (_t^_s)-_s;
}

static inline float D_fabs(float x)
{
    return x < 0 ? -x : x;
}

#endif // __D_MAIN__