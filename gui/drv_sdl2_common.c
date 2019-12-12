/*
 * Copyright (c) 2009-2015 Hypertriton, Inc. <http://hypertriton.com/>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Common code for SDL 1.2 drivers.
 */

#include <agar/core/core.h>
#include <agar/gui/gui.h>
#include <agar/gui/window.h>
#include <agar/gui/packedpixel.h>
#include <agar/gui/cursors.h>
#include <agar/gui/sdl2.h>

#define AG_SDL_CLIPPED_PIXEL(s, ax, ay)			\
	((ax) < (s)->clip_rect.x ||			\
	 (ax) >= (s)->clip_rect.x+(s)->clip_rect.w ||	\
	 (ay) < (s)->clip_rect.y ||			\
	 (ay) >= (s)->clip_rect.y+(s)->clip_rect.h)

/*
 * Initialize Agar with an existing SDL display. If the display surface has
 * flags SDL_OPENGL / SDL_OPENGLBLIT set, the "sdlgl" driver is selected;
 * otherwise, the "sdlfb" driver is used.
 */
int
AG_InitVideoSDL2(void *pDisplay, Uint flags)
{
	SDL_Window *display = pDisplay;
	AG_Driver *drv = NULL;
	AG_DriverClass *dc = NULL;
	int useGL = 0;
	int i;

	if (AG_InitGUIGlobals() == -1)
		return (-1);
	
	/* Enable OpenGL mode if the surface has SDL_OPENGL set. */
	if ((SDL_GetWindowFlags(display) & SDL_WINDOW_OPENGL)) {
		if (flags & AG_VIDEO_SDL) {
			AG_SetError("AG_VIDEO_SDL flag requested, but "
			            "window has SDL_WINDOW_OPENGL set");
			goto fail;
		}
		useGL = 1;
	} else {
		if (flags & AG_VIDEO_OPENGL) {
			AG_SetError("AG_VIDEO_OPENGL flag requested, but "
			            "window is missing SDL_WINDOW_OPENGL");
			goto fail;
		}
	}
	for (i = 0; i < agDriverListSize; i++) {
		dc = agDriverList[i];
		if ((dc->wm == AG_WM_SINGLE) &&
		    (dc->flags & AG_DRIVER_SDL)) {
			if (useGL) {
				if (!(dc->flags & AG_DRIVER_OPENGL))
					continue;
			} else {
				if (dc->flags & AG_DRIVER_OPENGL)
					continue;
			}
			break;
		}
	}
	if (i == agDriverListSize) {
		AG_SetError("No compatible %s driver is available",
		    useGL ? "SDL/OpenGL" : "SDL");
		goto fail;
	}
	dc = agDriverList[i];
	if ((drv = AG_DriverOpen(dc)) == NULL) {
		goto fail;
	}

	/* Open a video display. */
	// if (AGDRIVER_SW_CLASS(drv)->openVideoContext(drv, (void *)display,
	//     flags) == -1) {
	// 	AG_DriverClose(drv);
	// 	goto fail;
	// }
	if (drv->videoFmt == NULL)
		AG_FatalError("Driver did not set video format");

	/* Generic Agar-GUI initialization. */
	if (AG_InitGUI(0) == -1) {
		AG_DriverClose(drv);
		goto fail;
	}

	agDriverOps = dc;
	agDriverSw = AGDRIVER_SW(drv);
	return (0);
fail:
	AG_DestroyGUIGlobals();
	return (-1);
}
