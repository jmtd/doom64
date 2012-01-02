// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Author$
// $Revision$
// $Date$
//
//
// DESCRIPTION:
//	DOOM main program (D_DoomMain) and game loop (D_DoomLoop),
//	plus functions to determine game mode (shareware, registered),
//	parse command line parameters, configure game parameters (turbo),
//	and call the startup functions.
//
//-----------------------------------------------------------------------------
#ifdef RCSID

static const char rcsid[] = "$Id$";
#endif

#ifdef _WIN32
#include <io.h>
#endif

#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "v_sdl.h"
#include "d_englsh.h"
#include "sounds.h"
#include "m_shift.h"
#include "z_zone.h"
#include "w_wad.h"
#include "s_sound.h"
#include "f_finale.h"
#include "m_misc.h"
#include "m_menu.h"
#include "i_system.h"
#include "g_game.h"
#include "wi_stuff.h"
#include "st_stuff.h"
#include "am_map.h"
#include "p_setup.h"
#include "d_main.h"
#include "con_console.h"
#include "d_devstat.h"
#include "r_local.h"
#include "r_wipe.h"
#include "g_controls.h"

#include "Ext/ChocolateDoom/net_client.h"

//
// D_DoomLoop()
// Not a globally visible function,
//  just included for source reference,
//  called by D_DoomMain, never exits.
// Manages timing and IO,
//  calls all ?_Responder, ?_Ticker, and ?_Drawer,
//  calls I_GetTime, and I_StartTic
//
void D_DoomLoop(void);

static int      pagetic;
static int      screenalpha;
static int      screenalphatext;
static int      creditstage;
static int      creditscreenstage;


int             video_width;
int             video_height;
dboolean        InWindow;
dboolean        setWindow = true;
int             validcount=1;
dboolean        windowpause = false;
dboolean        devparm=false;	    // started game with -devparm
dboolean        nomonsters=false;	// checkparm of -nomonsters
dboolean        respawnparm=false;	// checkparm of -respawn
dboolean        respawnitem=false;	// checkparm of -respawnitem
dboolean        fastparm=false;	    // checkparm of -fast
dboolean        BusyDisk=false;
dboolean        nolights = false;
skill_t         startskill;
int             startmap;
dboolean        autostart=false;
FILE*           debugfile=NULL;
dboolean        advancedemo=false;
//char			wadfile[1024];		// primary wad file
char            mapdir[1024];		// directory of development maps
char            basedefault[1024];	// default file
dboolean        rundemo4 = false;   // run demo lump #4?


void D_CheckNetGame(void);
void D_ProcessEvents(void);
void G_BuildTiccmd(ticcmd_t* cmd);
void D_DoAdvanceDemo(void);

#define STRPAUSED	"Paused"


//
// EVENT HANDLING
//
// Events are asynchronous inputs generally generated by the game user.
// Events can be discarded if no responder claims them
//
event_t			events[MAXEVENTS];
int				eventhead=0;
int				eventtail=0;


//
// D_PostEvent
// Called by the I/O functions when input is detected
//

void D_PostEvent(event_t* ev)
{
    events[eventhead] = *ev;
    eventhead = (++eventhead)&(MAXEVENTS-1);
}


//
// D_ProcessEvents
// Send all the events of the given timestamp down the responder chain
//

void D_ProcessEvents(void)
{
    event_t* ev;
    
    for(; eventtail != eventhead; eventtail = (++eventtail)&(MAXEVENTS-1))
    {
        ev = &events[eventtail];

        if(CON_Responder(ev))
            continue;               // console ate the event

        if(devparm && !netgame && usergame)
        {
            if(D_DevKeyResponder(ev))
                continue;           // dev keys ate the event
        }

        if(M_Responder(ev))
            continue;               // menu ate the event

        G_Responder(ev);
    }
}


//
// D_IncValidCount
//

void D_IncValidCount(void)
{
    validcount++;
}

//
// D_MiniLoop
//

extern dboolean renderinframe;
extern int      gametime;
extern int      skiptics;

int             frameon = 0;
int             frametics[4];
int             frameskip[4];
int             oldnettics = 0;

int GetLowTic(void);
dboolean PlayersInGame(void);

static void D_DrawInterface(void)
{
    if(menuactive)
        M_Drawer();
    
    CON_Draw();

    if(devparm)
        D_DeveloperDisplay();
    
    
    BusyDisk = false;
    
    // draw pause pic
    if(paused)
        M_DrawSmbText(-1, 64, WHITE, STRPAUSED);
}

static void D_FinishDraw(void)
{
    // send out any new accumulation
    NetUpdate();

    // normal update
    I_FinishUpdate();

    if(i_interpolateframes.value)
        I_EndDisplay();
}

int D_MiniLoop(void (*start)(void), void (*stop)(void),
                void (*draw)(void), dboolean (*tick)(void))
{
    int action = gameaction = ga_nothing;

    if(start) start();

    while(!action)
    {
        int i = 0;
        int lowtic = 0;
        int entertic = 0;
        int oldentertics = 0;
        int realtics = 0;
        int availabletics = 0;
        int counts = 0;
        
        windowpause = (menuactive ? true : false);

        // process one or more tics

        // get real tics
        entertic = I_GetTime ()/ticdup;
        realtics = entertic - oldentertics;
        oldentertics = entertic;

        if(i_interpolateframes.value)
        {
            renderinframe = true;

            if(I_StartDisplay())
            {
                if(draw && !action) draw();
                D_DrawInterface();
                D_FinishDraw();
            }

            renderinframe = false;
        }

        // get available ticks

        NetUpdate();
        lowtic = GetLowTic();

        availabletics = lowtic - gametic/ticdup;

        // decide how many tics to run

        if(net_cl_new_sync)
            counts = availabletics;
        else
        {
            if(realtics < availabletics-1)
                counts = realtics+1;
            else if (realtics < availabletics)
                counts = realtics;
            else
                counts = availabletics;

            if(counts < 1)
                counts = 1;
		
            frameon++;

            if(!demoplayback)
            {
                int keyplayer = -1;

                // ideally maketic should be 1 - 3 tics above lowtic
                // if we are consistantly slower, speed up time

                for(i = 0 ; i < MAXPLAYERS; i++)
                {
                    if(playeringame[i])
                    {
                        keyplayer = i;
                        break;
                    }
                }

                if(keyplayer < 0) // If there are no players, we can never advance anyway
                    goto drawframe;

                if(consoleplayer == keyplayer)
                {
                    // the key player does not adapt
                }
                else
                {
                    if(maketic <= nettics[keyplayer])
                    {
                        gametime--;
                        // I_Printf ("-");
                    }

                    frameskip[frameon & 3] = (oldnettics > nettics[keyplayer]);
                    oldnettics = maketic;

                    if (frameskip[0] && frameskip[1] && frameskip[2] && frameskip[3])
                    {
                        skiptics = 1;
                        // I_Printf ("+");
                    }
                }
            }
        }

        if(counts < 1) counts = 1;

        // wait for new tics if needed

        while(!PlayersInGame() || lowtic < gametic/ticdup + counts)	
        {
            NetUpdate();
            lowtic = GetLowTic();
	
            if(lowtic < gametic/ticdup)
                I_Error("D_MiniLoop: lowtic < gametic");

            if(i_interpolateframes.value)
            {
                renderinframe = true;

                if(I_StartDisplay())
                {
                    if(draw && !action) draw();
                    D_DrawInterface();
                    D_FinishDraw();
                }

                renderinframe = false;
            }

            // Don't stay in this loop forever.  The menu is still running,
            // so return to update the screen

            if(I_GetTime() / ticdup - entertic > 0)
                goto drawframe;

            I_Sleep(1);
        }

        // run the count * ticdup dics
        while(counts--)
        {
            for(i = 0; i < ticdup; i++)
            {
                // check that there are players in the game.  if not, we cannot
                // run a tic.
        
                if(!PlayersInGame())
                    break;
    
                if(gametic/ticdup > lowtic)
                    I_Error ("gametic>lowtic");

                if(i_interpolateframes.value)
                    I_GetTime_SaveMS();

                G_Ticker();

                if(tick)
                    action = tick();

                if(gameaction != ga_nothing)
                    action = gameaction;

                gametic++;
	    
                // modify command for duplicated tics
                if(i != ticdup-1)
                {
                   ticcmd_t *cmd;
                   int buf;
                   int j;
				
                   buf = (gametic/ticdup)%BACKUPTICS; 
                   for(j = 0; j < MAXPLAYERS; j++)
                   {
                       cmd = &netcmds[j][buf];
                       cmd->chatchar = 0;
                       if(cmd->buttons & BT_SPECIAL)
                           cmd->buttons = 0;
                   }
                }
            }

            NetUpdate();   // check for new console commands
        }

drawframe:

        S_UpdateSounds();
        
        // Update display, next frame, with current state.
        if(i_interpolateframes.value)
        {
            if(!I_StartDisplay())
                goto freealloc;
        }

        if(draw && !action) draw();
        D_DrawInterface();
        D_FinishDraw();

freealloc:

        // force garbage collection
        Z_FreeAlloca();
    }

    gamestate = GS_NONE;

    if(stop) stop();

    return action;
}


//
// Title_Drawer
//

static void Title_Drawer(void)
{
    R_GLClearFrame(0xFF000000);
    R_DrawGfx(58, 50, "TITLE", WHITEALPHA(0x64), true);
}

//
// Title_Ticker
//

static int Title_Ticker(void)
{
    if(mainmenuactive)
    {
        if((gametic - pagetic) >= (TICRATE * 30))
            return 1;
    }
    else
    {
        if(gametic != pagetic)
            pagetic = gametic;
    }

    return 0;
}

//
// Title_Start
//

static void Title_Start(void)
{
    gameaction = ga_nothing;
    pagetic = gametic;
    allowmenu = true;
    menuactive = true;
    mainmenuactive = true;
    usergame = false;   // no save / end game here
    paused = false;
    allowclearmenu = false;

    S_StartMusic(mus_title);
    M_StartControlPanel();
}

//
// Title_Stop
//

static void Title_Stop(void)
{
    mainmenuactive = false;
    menuactive = false;
    allowmenu = false;
    allowclearmenu = true;

    WIPE_FadeScreen(8);
    S_StopMusic();
}

//
// Legal_Start
//

static char* legalpic = "USLEGAL";
static int legal_x = 32;
static int legal_y = 72;

static void Legal_Start(void)
{
    int pllump;
    int jllump;

    pllump = W_CheckNumForName("PLLEGAL");
    jllump = W_CheckNumForName("JPLEGAL");

    if(pllump == -1 && jllump == -1)
        return;

    if(p_regionmode.value >= 2 && jllump >= 0)
    {
        legalpic = "JPLEGAL";
        legal_x = 35;
        legal_y = 45;
    }
    else if(p_regionmode.value >= 2 && jllump == -1)
        CON_CvarSetValue(p_regionmode.name, 1);

    if(p_regionmode.value == 1 && pllump >= 0)
    {
        legalpic = "PLLEGAL";
        legal_x = 35;
        legal_y = 50;
    }
    else if(p_regionmode.value == 1 && pllump == -1)
        CON_CvarSetValue(p_regionmode.name, 0);
}

//
// Legal_Drawer
//

static void Legal_Drawer(void)
{
    R_GLClearFrame(0xFF000000);
    R_DrawGfx(legal_x, legal_y, legalpic, WHITE, true);
}

//
// Legal_Ticker
//

static int Legal_Ticker(void)
{
    if((gametic - pagetic) >= (TICRATE * 5))
    {
        WIPE_FadeScreen(6);
        return 1;
    }

    return 0;
}

//
// Credits_Drawer
//

static void Credits_Drawer(void)
{
    R_GLClearFrame(0xFF000000);

    switch(creditscreenstage)
    {
    case 0:
        R_DrawGfx(72, 24, "IDCRED1",
            D_RGBA(255, 255, 255, (byte)screenalpha), true);

        R_DrawGfx(40, 40, "IDCRED2",
            D_RGBA(255, 255, 255, (byte)screenalphatext), true);
        break;

    case 1:
        R_DrawGfx(16, 80, "WMSCRED1",
            D_RGBA(255, 255, 255, (byte)screenalpha), true);

        R_DrawGfx(32, 24, "WMSCRED2",
            D_RGBA(255, 255, 255, (byte)screenalphatext), true);
        break;

    case 2:
        R_DrawGfx(64, 30, "EVIL",
            D_RGBA(255, 255, 255, (byte)screenalpha), true);

        R_DrawGfx(40, 52, "FANCRED",
            D_RGBA(255, 255, 255, (byte)screenalphatext), true);
        break;

    }
}

//
// Credits_Ticker
//

static int Credits_Ticker(void)
{
    switch(creditstage)
    {
    case 0:
        if(screenalpha < 0xff)
            screenalpha = MIN(screenalpha + 8, 0xff);
        else
            creditstage = 1;
        break;

    case 1:
        if(screenalphatext < 0xff)
            screenalphatext = MIN(screenalphatext + 8, 0xff);
        else
            creditstage = 2;
        break;

    case 2:
        if((gametic - pagetic) >= (TICRATE * 6))
        {
            screenalpha = MAX(screenalpha - 8, 0);
            screenalphatext = MAX(screenalphatext - 8, 0);

            if(screenalpha <= 0)
            {
                creditstage = 3;
                creditscreenstage++;
            }
        }
        break;

    case 3:
        if(creditscreenstage >= 3)
            return 1;

        screenalpha = 0;
        screenalphatext = 0;
        creditstage = 0;
        pagetic = gametic;

        break;
    }

    return 0;
}

//
// Credits_Start
//

static void Credits_Start(void)
{
    screenalpha = 0;
    screenalphatext = 0;
    creditstage = 0;
    creditscreenstage = 0;
    pagetic = gametic;
    allowmenu = false;
    menuactive = false;
    usergame = false;   // no save / end game here
    paused = false;
    gamestate = GS_SKIPPABLE;
}

//
// D_SplashScreen
//

static void D_SplashScreen(void)
{
    int skip = 0;

    if(gameaction || netgame)
        return;

    screenalpha = 0xff;
    allowmenu = false;
    menuactive = false;

    gamestate = GS_SKIPPABLE;
    pagetic = gametic;
    gameaction = ga_nothing;

    skip = D_MiniLoop(Legal_Start, NULL, Legal_Drawer, Legal_Ticker);

    if(skip != ga_title)
    {
        G_RunTitleMap();
        gameaction = ga_title;
    }
}

//
// D_DoomLoop
// Main game loop
//

void D_DoomLoop(void)
{
    int exit;

    if(netgame)
        gameaction = ga_newgame;

    exit = gameaction;
    
    while(1)
    {
        exit = D_MiniLoop(Title_Start, Title_Stop, Title_Drawer, Title_Ticker);

        if(exit == ga_newgame || exit == ga_loadgame)
            G_RunGame();
        else
        {
            D_MiniLoop(Credits_Start, NULL, Credits_Drawer, Credits_Ticker);

            if(gameaction == ga_title)
                continue;

            G_PlayDemo("DEMO1LMP");
            if(gameaction != ga_exitdemo)
                continue;

            G_PlayDemo("DEMO2LMP");
            if(gameaction != ga_exitdemo)
                continue;

            G_PlayDemo("DEMO3LMP");
            if(gameaction != ga_exitdemo)
                continue;

            if(rundemo4)
            {
                G_PlayDemo("DEMO4LMP");
                if(gameaction != ga_exitdemo)
                    continue;
            }

            G_RunTitleMap();
            continue;
        }
    }
}


//      print title for every printed line
char title[128];

//
// Find a Response File
//

#define MAXARGVS 100

static void FindResponseFile(void)
{
    int i;
    
    for(i = 1; i < myargc; i++)
    {
        if(myargv[i][0] == '@')
        {
            FILE *  handle;
            int     size;
            int     k;
            int     index;
            int     indexinfile;
            char    *infile;
            char    *file;
            char    *moreargs[20];
            char    *firstargv;
            
            // READ THE RESPONSE FILE INTO MEMORY
            handle = fopen (&myargv[i][1],"rb");
            if(!handle)
            {
                //				I_Warnf (IWARNMINOR, "\nNo such response file!");
                exit(1);
            }
            I_Printf("Found response file %s!\n",&myargv[i][1]);
            fseek (handle,0,SEEK_END);
            size = ftell(handle);
            fseek (handle,0,SEEK_SET);
            file = malloc (size);
            fread (file,size,1,handle);
            fclose (handle);
            
            // KEEP ALL CMDLINE ARGS FOLLOWING @RESPONSEFILE ARG
            for (index = 0,k = i+1; k < myargc; k++)
                moreargs[index++] = myargv[k];
            
            firstargv = myargv[0];
            myargv = malloc(sizeof(char *)*MAXARGVS);
            dmemset(myargv,0,sizeof(char *)*MAXARGVS);
            myargv[0] = firstargv;
            
            infile = file;
            indexinfile = k = 0;
            indexinfile++;  // SKIP PAST ARGV[0] (KEEP IT)
            do
            {
                myargv[indexinfile++] = infile+k;
                while(k < size &&
                    ((*(infile+k)>= ' '+1) && (*(infile+k)<='z')))
                    k++;
                *(infile+k) = 0;
                while(k < size &&
                    ((*(infile+k)<= ' ') || (*(infile+k)>'z')))
                    k++;
            } while(k < size);
            
            for(k = 0;k < index;k++)
                myargv[indexinfile++] = moreargs[k];
            myargc = indexinfile;
            
            // DISPLAY ARGS
            for(k = 1; k < myargc; k++)
                I_Printf("%d command-line args: %s\n", myargc, myargv[k]);
            
            break;
        }
    }
}

//
// DoLooseFiles
//
// Take any file names on the command line before the first switch parm
// and insert the appropriate -file, -deh or -playdemo switch in front
// of them.
//
// Note that more than one -file, etc. entry on the command line won't
// work, so we have to go get all the valid ones if any that show up
// after the loose ones.  This means that boom fred.wad -file wilma
// will still load fred.wad and wilma.wad, in that order.
// The response file code kludges up its own version of myargv[] and
// unfortunately we have to do the same here because that kludge only
// happens if there _is_ a response file.  Truth is, it's more likely
// that there will be a need to do one or the other so it probably
// isn't important.  We'll point off to the original argv[], or the
// area allocated in FindResponseFile, or our own areas from strdups.
//
// CPhipps - OUCH! Writing into *myargv is too dodgy, damn
//
// e6y
// Fixed crash if numbers of wads/lmps/dehs is greater than 100
// Fixed bug when length of argname is smaller than 3
// Refactoring of the code to avoid use the static arrays
// The logic of DoLooseFiles has been rewritten in more optimized style
// MAXARGVS has been removed.

static void DoLooseFiles(void)
{
    char **wads;  // store the respective loose filenames
    char **lmps;
    int wadcount = 0;      // count the loose filenames
    int lmpcount = 0;
    int i,k,n,p;
    char **tmyargv;  // use these to recreate the argv array
    int tmyargc;
    dboolean *skip; // CPhipps - should these be skipped at the end

    struct
    {
        const char *ext;
        char ***list;
        int *count;
    } looses[] =
    {
        { ".wad", &wads, &wadcount  },
        { ".lmp", &lmps, &lmpcount  },
        // assume wad if no extension or length of the extention is not equal to 3
        // must be last entrie
        { "",     &wads, &wadcount  },
        { 0                         }
    };

    struct
    {
        char *cmdparam;
        char ***list;
        int *count;
    } params[] =
    {
        { "-file"    , &wads, &wadcount },
        { "-playdemo", &lmps, &lmpcount },
        { 0                             }
    };

    wads = malloc(myargc * sizeof(*wads));
    lmps = malloc(myargc * sizeof(*lmps));
    skip = malloc(myargc * sizeof(dboolean));

    for(i = 0; i < myargc; i++)
        skip[i] = false;

    for(i = 1; i < myargc; i++)
    {
        int arglen, extlen;

        if(*myargv[i] == '-')
            break;  // quit at first switch

        // so now we must have a loose file.  Find out what kind and store it.
        arglen = dstrlen(myargv[i]);

        k = 0;

        while (looses[k].ext)
        {
            extlen = strlen(looses[k].ext);
            if(arglen - extlen >= 0 && !dstricmp(&myargv[i][arglen - extlen], looses[k].ext))
            {
                (*(looses[k].list))[(*looses[k].count)++] = strdup(myargv[i]);
                break;
            }

            k++;
        }
        
        skip[i] = true; // nuke that entry so it won't repeat later
    }

    // Now, if we didn't find any loose files, we can just leave.
    if(wadcount + lmpcount != 0)
    {
        n = 0;
        k = 0;

        while(params[k].cmdparam)
        {
            if((p = M_CheckParm(params[k].cmdparam)))
            {
                skip[p] = true;    // nuke the entry
                while (++p != myargc && *myargv[p] != '-')
                {
                    (*(params[k].list))[(*params[k].count)++] = strdup(myargv[p]);
                    skip[p] = true;  // null any we find and save
                }
            }
            else
            {
                if(*(params[k].count) > 0)
                    n++;
            }
            
            k++;
        }

        // Now go back and redo the whole myargv array with our stuff in it.
        // First, create a new myargv array to copy into
        tmyargv = calloc(myargc + n);
        tmyargv[0] = myargv[0]; // invocation
        tmyargc = 1;

        k = 0;

        while(params[k].cmdparam)
        {
            // put our stuff into it
            if(*(params[k].count) > 0)
            {
                tmyargv[tmyargc++] = strdup(params[k].cmdparam); // put the switch in
                for(i = 0; i < *(params[k].count);)
                    tmyargv[tmyargc++] = (*(params[k].list))[i++]; // allocated by strdup above
            }
            
            k++;
        }

        // then copy everything that's there now
        for (i = 1; i < myargc; i++)
        {
            if(!skip[i])  // skip any zapped entries
                tmyargv[tmyargc++] = myargv[i];  // pointers are still valid
        }
        
        // now make the global variables point to our array
        myargv = tmyargv;
        myargc = tmyargc;
    }

    free(wads);
    free(lmps);
    free(skip);
}

//
// D_Init
//

static void D_Init(void)
{
    int     p;
    char    file[256];

    FindResponseFile();
    DoLooseFiles();
    
    nomonsters		= M_CheckParm("-nomonsters");
    respawnparm		= M_CheckParm("-respawn");
    respawnitem		= M_CheckParm("-respawnitem");
    fastparm		= M_CheckParm("-fast");
    devparm			= M_CheckParm("-devparm");

    if(p = M_CheckParm("-setvars"))
    {
        p++;

        while(p != myargc && myargv[p][0] != '-')
        {
            char *name;
            char *value;

            name = myargv[p++];
            value = myargv[p++];

            CON_CvarSet(name, value);
        }
    }
    
    if(M_CheckParm("-altdeath"))
        deathmatch = 2;
    else if(M_CheckParm("-deathmatch"))
        deathmatch = 1;
    
    // turbo option
    p = M_CheckParm ("-turbo");
    if(p)
    {
        int     scale = 200;
        extern int forwardmove[2];
        extern int sidemove[2];
        
        if (p<myargc-1)
            scale = datoi (myargv[p+1]);
        if (scale < 10)
            scale = 10;
        if (scale > 400)
            scale = 400;
        I_Printf ("turbo scale: %i%%\n",scale);
        forwardmove[0] = forwardmove[0]*scale/100;
        forwardmove[1] = forwardmove[1]*scale/100;
        sidemove[0] = sidemove[0]*scale/100;
        sidemove[1] = sidemove[1]*scale/100;
    }
    
    // get skill / episode / map from parms
    startskill = sk_medium;
    startmap = 1;
    autostart = false;
    
    
    p = M_CheckParm ("-skill");
    if(p && p < myargc-1)
    {
        startskill = myargv[p+1][0]-'1';
        autostart = true;
        gameaction = ga_newgame;
    }
    
    p = M_CheckParm ("-timer");
    if(p && p < myargc-1 && deathmatch)
    {
        int     time;
        time = datoi(myargv[p+1]);
        I_Printf("Levels will end after %d minute\n",time);

        if (time>1)
            I_Printf("s");

        I_Printf(".\n");
    }
    
    p = M_CheckParm ("-warp");
    if(p && p < myargc-1)
    {
        autostart = true;
        startmap = datoi (myargv[p+1]);
        gameaction = ga_newgame;
    }

    // set server cvars
    CON_CvarSetValue(sv_skill.name, (float)startskill);
    CON_CvarSetValue(sv_respawn.name, (float)respawnparm);
    CON_CvarSetValue(sv_respawnitems.name, (float)respawnitem);
    CON_CvarSetValue(sv_fastmonsters.name, (float)fastparm);
    CON_CvarSetValue(sv_nomonsters.name, (float)nomonsters);
    
    p = M_CheckParm ("-loadgame");
    if (p && p < myargc-1)
    {
        sprintf(file, SAVEGAMENAME"%c.dsg",myargv[p+1][0]);
        G_LoadGame (file);
    }
    
    if(M_CheckParm("-nogun"))
    {
        ShowGun = false;
    }
}

//
// D_CheckDemo
//

static int D_CheckDemo(void)
{
    int p;

    // start the apropriate game based on parms
    p = M_CheckParm ("-record");
    
    if(p && p < myargc-1)
    {
        G_RecordDemo(myargv[p+1]);
        return 1;
    }

    p = M_CheckParm ("-playdemo");
    if(p && p < myargc-1)
    {
        //singledemo = true;              // quit after one demo
        G_PlayDemo(myargv[p+1]);
        return 1;
    }

    return 0;
}

//
// D_DoomMain
//

void D_DoomMain(void)
{   
    I_Printf("Z_Init: Init Zone Memory Allocator\n");
    Z_Init();

    I_Printf("CON_Init: Init Game Console\n");
    CON_Init();

    // load before initing other systems
    I_Printf("M_LoadDefaults: Loading Game Configuration\n");
    M_LoadDefaults();
    
    // init subsystems

    I_Printf("D_Init: Init DOOM parameters\n");
    D_Init();

    I_Printf("W_Init: Init WADfiles.\n");
    W_Init();
    
    I_Printf("M_Init: Init miscellaneous info.\n");
    M_Init();
    
    I_Printf("I_Init: Setting up machine state.\n");
    I_Init();
    
    I_Printf("R_Init: Init DOOM refresh daemon.\n");
    R_Init();
    
    I_Printf("P_Init: Init Playloop state.\n");
    P_Init();
    
    I_Printf("G_Init: Setting up gamestate\n");
    G_Init();
    
    I_Printf("NET_Init: Init network subsystem.\n");
    NET_Init();
    
    I_Printf("D_CheckNetGame: Checking network game status.\n");
    D_CheckNetGame();
    
    I_Printf("S_Init: Setting up sound.\n");
    S_Init();
    
    I_Printf("ST_Init: Init status bar.\n");
    ST_Init();
    
    I_Printf("V_InitGL: Starting OpenGL\n");
    V_InitGL();

    // garbage collection
    Z_FreeAlloca();

    if(!D_CheckDemo())
    {
        if(!autostart)
        {
            // start legal screen and title map stuff
            D_SplashScreen();
        }
        else
            G_RunGame();
    }

    D_DoomLoop();   // never returns
}
