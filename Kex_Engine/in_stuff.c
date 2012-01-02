// Emacs style mode select	 -*- C++ -*-
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
// DESCRIPTION: Intermission screen functions
//
//-----------------------------------------------------------------------------
#ifdef RCSID
static const char rcsid[] = "$Id$";
#endif

#include "i_system.h"
#include "d_englsh.h"
#include "doomstat.h"
#include "st_stuff.h"
#include "f_finale.h"
#include "m_misc.h"
#include "r_local.h"
#include "r_wipe.h"
#include "p_setup.h"
#include "s_sound.h"

static int              f_alpha = 0;
static int              fInterFadeOut = false;
static char             fInterString[16][32];
static dboolean         fInterDone = false;
static int              fInterAlpha = 0;
static int              fInterSlot = 0;
static int              fTextOffset = 0;
static clusterdef_t*    fcluster = NULL;
static dboolean         fstopmusic = false;

//
// IN_Start
//

void IN_Start(void)
{
    int i = 0;
    int j = 0;
    int k = 0;

    // initialize variables
    automapactive   = false;
    fInterDone      = false;
    fstopmusic      = true;
    fInterAlpha     = 0;
    fInterSlot      = 0;
    fInterFadeOut   = false;
    fTextOffset     = 0;
    f_alpha         = 0;
    fcluster        = NULL;

    dmemset(fInterString, 0, 16*32);

    fcluster = P_GetCluster(nextmap);

    if(!fcluster)
        fcluster = P_GetCluster(gamemap);

    // try to bail out if no cluster is found at all
    if(!fcluster)
    {
        gameaction = ga_loadlevel;
        return;
    }

    i = 0;

    // setup intermission text
    while(k < dstrlen(fcluster->text))
    {
        char c = fcluster->text[k++];

        if(c == '\n')
        {
            j = 0;
            if(fInterString[i][0] == '\0')
                fInterString[i][0] = ' ';

            i++;
            continue;
        }
        else
            fInterString[i][j++] = c;
    }

    gameaction  = ga_nothing;
    
    S_StartMusic(fcluster->music);
}

//
// IN_Stop
//

void IN_Stop(void)
{
    if(fstopmusic)
        S_StopMusic();

    WIPE_FadeScreen(6);
}

//
// IN_Drawer
//

void IN_Drawer(void)
{
    int i = 0;
    byte alpha = 0;
    int y = 0;
    rcolor color;

    R_GLClearFrame(0xFF000000);

    if(fcluster->scrolltextend)
        color = D_RGBA(255, 255, 255, f_alpha);
    else
        color = 0xFFFFFFFF;
    
    // Draw background
    R_DrawGfx(fcluster->pic_x, fcluster->pic_y, fcluster->pic, color, false);

    if(!fInterFadeOut)
    {
        // don't draw anything else until background is fully opaque
        if(f_alpha < 0xff)
            return;
    }

    i = 0;
    while(fInterString[i][0] != '\0')
        i++;

    y = (SCREENHEIGHT / 2) - ((i * 14) / 2);

    // draw strings
    for(i = 0;; i++)
    {
        if(i == fInterSlot)
            alpha = (byte)fInterAlpha;
        else
            alpha = 0xff;
        
        M_DrawSmbText(-1, y - fTextOffset, D_RGBA(255, 255, 255, alpha), fInterString[i]);
        y += 14;
        
        if(i == fInterSlot || !fInterString[i][0])
            return;
    }
}

//
// IN_Finish
//

static void IN_Finish(void)
{
    if(fInterFadeOut)
        return;
    
    if(fcluster->scrolltextend)
        fInterFadeOut = true;
    else
        gameaction = ga_loadlevel;

    fInterDone = true;
}

//
// IN_Ticker
//

int IN_Ticker(void)
{
    int   i;
    player_t  *player;

    // Fade out for finale after all of text has scrolled up towards the screen
    if(fInterFadeOut)
    {
        // text hasn't scrolled off screen yet
        if(fTextOffset++ < SCREENHEIGHT)
            return 0;

        if(!fcluster->enteronly)
        {
            fstopmusic = false;
            return ga_finale;
        }

        return 1;
    }
    else
    {
        // fade in for finale
        f_alpha = MIN(f_alpha + 8, 0xff);

        // wait until fully opaque
        if(f_alpha < 0xff)
            return 0;
    }
    
    if(fInterDone || devparm)
    {
        // check for button presses to skip delays
        for(i = 0, player = players; i < MAXPLAYERS; i++, player++)
        {
            if(playeringame[i])
            {
                if(player->cmd.buttons & BT_ATTACK)
                {
                    if(!player->attackdown)
                        IN_Finish();
                    player->attackdown = true;
                }
                else
                    player->attackdown = false;
            
                if(player->cmd.buttons & BT_USE)
                {
                    if(!player->usedown)
                        IN_Finish();
                    player->usedown = true;
                }
                else
                    player->usedown = false;
            }
        }
    }
    
    if(fInterDone)
        return 0;
    
    // fade in each line of text
    fInterAlpha += 6;
    
    if(fInterAlpha >= 0xff)
    {
        fInterAlpha = 0xff;
        if(!fInterString[++fInterSlot][0])
        {
            fInterSlot = fInterSlot - 1;
            fInterDone = true;
            return 0;
        }

        fInterAlpha = 0;
    }

    return 0;
}



