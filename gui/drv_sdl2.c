/*
 * Copyright (c) 2009-2013 Hypertriton, Inc. <http://hypertriton.com/>
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
 * Driver for framebuffer graphics via the SDL 2.0 library.
 */

#include <agar/core/core.h>
#include <agar/gui/gui.h>
#include <agar/gui/drv.h>
#include <agar/gui/text.h>
#include <agar/gui/window.h>
#include <agar/gui/packedpixel.h>
#include <agar/gui/cursors.h>
#include <agar/gui/pixmap.h>
#include <agar/gui/sdl2.h>
#include <agar/core/types.h>
#include <stdint.h>

#define SDL2_SPECIFIC_STREAM 0x100
#define TEXTUREMAPSIZE 1024
static	Uint		ntexturemaps = 0;
static	Uint		maxtextures = 0;

typedef struct ag_sdl2_driver {
	struct ag_driver_mw _inherit;
	Uint mwflags;
	SDL_Window	*w;
	SDL_Renderer	*r;
	SDL_PixelFormat *pf;
	Uint32		f;
	Uint32		wid;
	AG_ClipRect *clipRects;		/* Clipping rectangle stack */
	Uint        nClipRects;
} AG_DriverSDL2;

AG_EventSink *sdlEventSpinner = NULL;	/* Standard event sink */
AG_EventSink *sdlEventEpilogue = NULL;	/* Standard event epilogue */

AG_DriverMwClass agDriverSDL2;
#define AGDRIVER_IS_SDL2(drv) (AGDRIVER_CLASS(drv) == (AG_DriverClass *)&agDriverSDL2)

static int nDrivers = 0;			/* Opened driver instances */
static int initedSDL = 0;			/* Used SDL_Init() */
static int initedSDLVideo = 0;			/* Used SDL_INIT_VIDEO */

static void SDL2_DrawRectFilled(void *, AG_Rect, AG_Color);
static void SDL2_UpdateRegion(void *, AG_Rect);

static void SDL2_PostResizeCallback(AG_Window *, AG_SizeAlloc *);
static void SDL2_PostMoveCallback(AG_Window *, AG_SizeAlloc *);
static void SDL2_SetTransientFor(AG_Window *, AG_Window *);
void *AG_SDL2_SurfaceExportSDL2(const AG_Surface *);

int AG_SDL2_EventSink(AG_EventSink *, AG_Event *);
int AG_SDL2_EventEpilogue(AG_EventSink *, AG_Event *);
AG_EventSink *sdl2EventSpinner = NULL;	/* For agTimeOps_renderer */

static void
Init(void *obj)
{
	AG_DriverSDL2 *sdl = obj;
	AG_DriverMw *dmw = obj;

	dmw->flags |= AG_DRIVER_MW_ANYPOS_AVAIL;
	sdl->w = NULL;
	sdl->r = NULL;
	sdl->pf = NULL;
	sdl->clipRects = NULL;
	sdl->nClipRects = 0;
	sdl->wid = 0;
	sdl->f = 0;
	sdl->mwflags = 0;
}

static void
Destroy(void *obj)
{
	AG_DriverSDL2 *sdl = obj;

	Free(sdl->clipRects);
}

#if defined(HAVE_CLOCK_GETTIME) && defined(HAVE_PTHREADS)
#pragma message("drv_sdl2: reached here 1")
static int
SDL2_EventSpin(AG_EventSink *es, AG_Event *event)
{
	AG_Delay(1);
	return (0);
}
#endif
/*
 * Generic driver operations
 */

static int
SDL2_Open(void *obj, const char *spec)
{
	AG_Driver *drv = obj;
	AG_DriverSDL2 *sdl = obj;

	/* Initialize SDL's video subsystem. */

	if (!initedSDL) {
  		if (SDL_Init(SDL_INIT_VIDEO) < 0) {
			AG_SetError("SDL_Init() failed: %s", SDL_GetError());
			return (-1);
		}
		initedSDL = 1;
	}
	if (!SDL_WasInit(SDL_INIT_VIDEO)) {
		if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
			AG_SetError("SDL_INIT_VIDEO failed: %s", SDL_GetError());
			return (-1);
		}
		initedSDLVideo = 1;
	}

	/* Initialize the main mouse and keyboard devices. */

	if(drv->mouse == NULL)
	{
		if ((drv->mouse = AG_MouseNew(sdl, "SDL mouse")) == NULL ||
			(drv->kbd = AG_KeyboardNew(sdl, "SDL keyboard")) == NULL) {
			goto fail2;
		}
	}

	/* Initialize the Event Spinners */

	if(nDrivers == 0)
	{
		if ((sdlEventSpinner = AG_AddEventSpinner(AG_SDL2_EventSink, "%p", drv)) == NULL ||
			(sdlEventEpilogue = AG_AddEventEpilogue(AG_SDL2_EventEpilogue, NULL)) == NULL) {
			goto fail1;
		}
	}

// #if defined(HAVE_CLOCK_GETTIME) && defined(HAVE_PTHREADS)
// #pragma message("drv_sdl2: reached here 1")
//  	if (agTimeOps == &agTimeOps_renderer)
//  		sdl2EventSpinner = AG_AddEventSpinner(SDL2_EventSpin, "%p", drv);
// #endif

	nDrivers++;

	return (0);
fail1:
	if (sdlEventSpinner != NULL) { AG_DelEventSink(sdlEventSpinner); sdlEventSpinner = NULL; }
	if (sdlEventEpilogue != NULL) { AG_DelEventEpilogue(sdlEventEpilogue); sdlEventEpilogue = NULL; }
fail2:
	if (drv->kbd != NULL) { AG_ObjectDelete(drv->kbd); drv->kbd = NULL; }
	if (drv->mouse != NULL) { AG_ObjectDelete(drv->mouse); drv->mouse = NULL; }
	return (-1);
}

static void
SDL2_Close(void *obj)
{
	AG_Driver *drv = obj;
	AG_DriverSDL2 *sdl = obj;
	
	if (--nDrivers == 0) {
		if(sdlEventSpinner != NULL)
		{
			AG_DelEventSink(sdlEventSpinner); sdlEventSpinner = NULL;
			AG_DelEventEpilogue(sdlEventEpilogue); sdlEventEpilogue = NULL;
		}
	}

	//AG_FreeCursors(AGDRIVER(sdl));

	if (initedSDLVideo) {
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		initedSDLVideo = 0;
	}

	AG_ObjectDelete(drv->kbd); drv->kbd = NULL;
	AG_ObjectDelete(drv->mouse); drv->mouse = NULL;
}

/* Dump the window to another window to verify contents - debug purposes only*/

static void DumpWindow(AG_DriverSDL2 *sdl)
{
	void *pixels;
	SDL_Window *sw = sdl->w;
	SDL_Renderer *sr = sdl->r;
	SDL_PixelFormat *pf = sdl->pf;
	Uint32 sf;
	int w;
	int h;
	int pitch;
	SDL_Window *dw;
	SDL_Renderer *dr;
	SDL_Surface *ds;
	SDL_Texture *dt;
	char *msg;
	SDL_Rect srect;

	pixels = (void*)malloc(h * pitch);

	SDL_GetWindowSize(sw, &w, &h);
	pitch =	pf->BytesPerPixel * w;

	dw = SDL_CreateWindow("AGAR:Dump", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_RESIZABLE);
	dr = SDL_CreateRenderer(dw, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
	sf = SDL_GetWindowPixelFormat(dw);
	pf = SDL_AllocFormat(sf);

	srect.x = 0;
	srect.y = 0;
	srect.w = w;
	srect.h = h;

	SDL_RenderReadPixels(sr, &srect, 0, pixels, pitch);
	ds = SDL_CreateRGBSurfaceFrom(pixels, w, h, pf->BitsPerPixel, pitch, pf->Rmask, pf->Gmask, pf->Bmask, pf->Amask);
	dt = SDL_CreateTextureFromSurface(dr, ds);
	if (dt == NULL)
	{
		msg = SDL_GetError();
	}
	SDL_RenderCopy(dr, dt, NULL, NULL);
	SDL_RenderPresent(dr);

	SDL_DestroyTexture(dt);
	SDL_FreeSurface(ds);
	free(pixels);

	SDL_Delay(4000);
	SDL_DestroyRenderer(dr);
	SDL_DestroyWindow(dw);
}

/* Standard beginEventProcessing() method for SDL drivers. */
void
AG_SDL2_BeginEventProcessing(void *obj)
{
	/* Nothing to do */
}

/* Standard pendingEvents() method for SDL drivers. */
int
AG_SDL2_PendingEvents(void *obj)
{
	return (SDL_PollEvent(NULL) != 0);
}

/* Return the Agar window corresponding to a X Window ID */
static __inline__ AG_Window *
LookupWindowByID(Uint32 wid)
{
	AG_Window *win;
	AG_DriverSDL2 *sdl;

	AG_LockVFS(&agDrivers);
	AGOBJECT_FOREACH_CHILD(sdl, &agDrivers, ag_sdl2_driver) {
		if (!AGDRIVER_IS_SDL2(sdl)) {
			continue;
		}
		if (sdl->wid == wid) {
			win = AGDRIVER_MW(sdl)->win;
			if (win == NULL || WIDGET(win)->drv == NULL) {	/* Being detached */
				goto fail;
			}
			AG_UnlockVFS(&agDrivers);
			return (win);
		}
	}
fail:
	AG_UnlockVFS(&agDrivers);
	AG_SetError("event from unknown window");
	return (0);
}

int isCappable(SDL_Scancode sc)
{
	int result = (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z);
	return result;
}

Uint32 AG_SDL2_Translate_UniCode(SDL_Keysym ks)
{
	switch(ks.scancode)
	{
		case SDL_SCANCODE_LSHIFT:
		case SDL_SCANCODE_RSHIFT:
		case SDL_SCANCODE_LALT:
		case SDL_SCANCODE_RALT:
		case SDL_SCANCODE_LGUI:
		case SDL_SCANCODE_RGUI:
		case SDL_SCANCODE_NUMLOCKCLEAR:
		case SDL_SCANCODE_CAPSLOCK:
		case SDL_SCANCODE_MODE:
		case SDL_SCANCODE_LCTRL:
		case SDL_SCANCODE_RCTRL:
			return (0);
			
		default:
			if (isCappable(ks.scancode))
			{
				if (ks.mod & KMOD_CTRL)
				{
					return ((Uint32)ks.sym - 0x60);
				}
				else if (ks.mod & KMOD_CAPS)
				{
					if (ks.mod & KMOD_SHIFT)
					{
						return ((Uint32)ks.sym);
					}

					return ((Uint32)ks.sym - 0x20);
				}
				else if (ks.mod & KMOD_SHIFT)
				{
					return ((Uint32)ks.sym - 0x20);
				}

				return ((Uint32)ks.sym);
			}
			else if (ks.mod & KMOD_SHIFT)
			{
				switch (ks.scancode)
				{
					case SDL_SCANCODE_0:
						return (Uint32)ks.sym - 0x07;
						
					case SDL_SCANCODE_1:
					case SDL_SCANCODE_3:
					case SDL_SCANCODE_4:
					case SDL_SCANCODE_5:
						return (Uint32)ks.sym - 0x10;

					case SDL_SCANCODE_6:
						return (Uint32)ks.sym + 0x28;

					case SDL_SCANCODE_7:
					case SDL_SCANCODE_9:
						return (Uint32)ks.sym - 0x11;

					case SDL_SCANCODE_2:
						return (Uint32)ks.sym + 0x0E;

					case SDL_SCANCODE_8:
						return (Uint32)ks.sym - 0x0E;

					case SDL_SCANCODE_GRAVE:
						return (Uint32)ks.sym + 0x1E;

					case SDL_SCANCODE_MINUS:
						return (Uint32)ks.sym + 0x32;

					case SDL_SCANCODE_EQUALS:
						return (Uint32)ks.sym - 0x12;

					case SDL_SCANCODE_SEMICOLON:
						return (Uint32)ks.sym - 0x01;

					case SDL_SCANCODE_LEFTBRACKET:
					case SDL_SCANCODE_RIGHTBRACKET:
					case SDL_SCANCODE_BACKSLASH:
						return (Uint32)ks.sym + 0x20;

					case SDL_SCANCODE_APOSTROPHE:
						return (Uint32)ks.sym - 0x05;

					case SDL_SCANCODE_COMMA:
					case SDL_SCANCODE_PERIOD:
					case SDL_SCANCODE_SLASH:
						return (Uint32)ks.sym + 0x10;

					default:
						return (Uint32)ks.sym;		
				}
			}
			else if (ks.mod & KMOD_CTRL)
			{
				switch (ks.scancode)
				{
					case SDL_SCANCODE_LEFTBRACKET:
					case SDL_SCANCODE_RIGHTBRACKET:
					case SDL_SCANCODE_BACKSLASH:
						return (Uint32)ks.sym - 0x40;
					
					case SDL_SCANCODE_SLASH:
						return (Uint32)ks.sym - 0x10;

					default:
						return (Uint32)ks.sym;
				}
			}
			else if (ks.sym < 256)
			{
				return ((Uint32)ks.sym);
			}

			return (0);	
	}
}

AG_KeySym AG_SDL2_Translate_KeySym(SDL_Keysym ks)
{
	switch(ks.scancode)
	{
		case SDL_SCANCODE_LSHIFT:		return (AG_KEY_LSHIFT);
		case SDL_SCANCODE_RSHIFT:		return (AG_KEY_RSHIFT);
		case SDL_SCANCODE_LALT:			return (AG_KEY_LALT);
		case SDL_SCANCODE_RALT:			return (AG_KEY_RALT);
		case SDL_SCANCODE_LGUI:			return (AG_KEY_LMETA);
		case SDL_SCANCODE_RGUI:			return (AG_KEY_RMETA);
		case SDL_SCANCODE_NUMLOCKCLEAR:	return (AG_KEY_NUMLOCK);
		case SDL_SCANCODE_CAPSLOCK:		return (AG_KEY_CAPSLOCK);
		case SDL_SCANCODE_MODE:			return (AG_KEY_MODE);
		case SDL_SCANCODE_LCTRL:		return (AG_KEY_LCTRL);
		case SDL_SCANCODE_RCTRL:		return (AG_KEY_RCTRL);
		case SDL_SCANCODE_RIGHT:		return (AG_KEY_RIGHT);
		case SDL_SCANCODE_LEFT:			return (AG_KEY_LEFT);
		case SDL_SCANCODE_DOWN:			return (AG_KEY_DOWN);
		case SDL_SCANCODE_UP:			return (AG_KEY_UP);
		case SDL_SCANCODE_PAGEUP:		return (AG_KEY_PAGEUP);
		case SDL_SCANCODE_PAGEDOWN:		return (AG_KEY_PAGEDOWN);
		case SDL_SCANCODE_HOME:			return (AG_KEY_HOME);
		case SDL_SCANCODE_END:			return (AG_KEY_END);
		case SDL_SCANCODE_TAB:			return (AG_KEY_TAB);
		case SDL_SCANCODE_F1:			return (AG_KEY_F1);
		case SDL_SCANCODE_F2:			return (AG_KEY_F2);
		case SDL_SCANCODE_F3:			return (AG_KEY_F3);
		case SDL_SCANCODE_F4:			return (AG_KEY_F4);
		case SDL_SCANCODE_F5:			return (AG_KEY_F5);
		case SDL_SCANCODE_F6:			return (AG_KEY_F6);
		case SDL_SCANCODE_F7:			return (AG_KEY_F7);
		case SDL_SCANCODE_F8:			return (AG_KEY_F8);
		case SDL_SCANCODE_F9:			return (AG_KEY_F9);
		case SDL_SCANCODE_F10:			return (AG_KEY_F10);
		case SDL_SCANCODE_F11:			return (AG_KEY_F11);
		case SDL_SCANCODE_F12:			return (AG_KEY_F12);			
		case SDL_SCANCODE_KP_DIVIDE:	return (AG_KEY_KP_DIVIDE);
		case SDL_SCANCODE_KP_MULTIPLY:	return (AG_KEY_KP_MULTIPLY);
		case SDL_SCANCODE_KP_MINUS:		return (AG_KEY_KP_MINUS);
		case SDL_SCANCODE_KP_PLUS:		return (AG_KEY_KP_PLUS);
		case SDL_SCANCODE_KP_ENTER:		return (AG_KEY_KP_ENTER);
		case SDL_SCANCODE_KP_1:			return (AG_KEY_KP1);
		case SDL_SCANCODE_KP_2:			return (AG_KEY_KP2);
		case SDL_SCANCODE_KP_3:			return (AG_KEY_KP3);
		case SDL_SCANCODE_KP_4:			return (AG_KEY_KP4);
		case SDL_SCANCODE_KP_5:			return (AG_KEY_KP5);
		case SDL_SCANCODE_KP_6:			return (AG_KEY_KP6);
		case SDL_SCANCODE_KP_7:			return (AG_KEY_KP7);
		case SDL_SCANCODE_KP_8:			return (AG_KEY_KP8);
		case SDL_SCANCODE_KP_9:			return (AG_KEY_KP9);
		case SDL_SCANCODE_KP_0:			return (AG_KEY_KP0);
		case SDL_SCANCODE_KP_PERIOD:	return (AG_KEY_KP_PERIOD);
		default:
			if ((Uint32)ks.sym < 256)
			{
				return ((AG_KeySym)ks.sym);
			}

			return ((AG_KeySym)0);
	}
}


/* Translate an SDL_Event to an AG_DriverEvent. */
/*
	The Keyboard Event translations to from SDL to AGAR are diabolical.
	AGAR requires a UNICODE that was provided in SDL1.2 but not in SDL2.0.

	The following method is used to generate a unicode.

	SDL2.0 provides three keyboard events that are received in this order:
		KeyDown
		TextInput
		KeyUp

	KeyDown Event Received
		Clears the persistant ksym variable in preperation for the KeyUp event

		Records the KeyCode in the presistant kc variable in prereration for the TextInput event

		A check is conducted to determine if the KeyCode (sym) 
		is between 13 & 255 (printable/visible characters).

		If NOT PRINTABLE
			Generates a AGAR keysym from the ScanCode via the function AG_SDL2_Translate_KeySym()
			Generates a unicode from the ScanCode using the function AG_SDL2_Translate_UniCode() 
			And sends that information to the AG_KeyboardUpdate function 
			and returns the same data to the event processor via the device event structure.
		
		If PRINTABLE
			it does nothing else and returns awaiting the TextInput event
	
	TextInput Event Received
		(Is only ever received if the key is printable)

		Uses the received text character as the AG KeySym to send to the AG_KeyboardUpdate function.

		Returns the KeyCode (kc) recorded by the KeyDown event and the received text character 
		to the event processor via the device event structure

	KeyUp Event Received
		Uses the received KeySym as AG keySym for the AG_KeyboardUpdate function

		If the TextInput event recorded a keysym (ksym) 
			Uses that as the unicode for the AG_KeyboardUpdate function
		else
		    Generates the unicode from the ScanCode via the function AG_SDL2_Translate_UniCode()
			and uses that as the unicode for the AG_KeyboardUpdate function

		Returns both determined things (keySym & unicode) to the event processor via the device event structure

		Clears the persistant ksym variable

	(Only works for Latin Character Keyboards)

	And after all that it fails when multiple keys are pressed. Arghhh!

	Totally re-wrote the Key event routine.  Much simpler and eliminated the need for the Text Event.
	Instead the complexity has been moved to the translate scancode to unicode function : AG_SDL2_Translate_UniCode().

	In addition had to disable automatic key repeats.  It appears AGAR optionally generates its own key repeats so it
	only confuses things if both AGAR and SDL are generating repeats.
*/
int
AG_SDL2_GetNextEvent(void *obj, AG_DriverEvent *dev)
{
	AG_DriverSDL2 *sdl = obj;
	AG_Driver *drv = obj;
	AG_Window *win;
	AG_KeySym ks;
	SDL_Event ev;
	Uint32 unicode;
	char *pkn, *kn;
	SDL_Keycode kc;
	int x, y, w, h;

	if(SDL_PollEvent(&ev) == 0)
	{
		return (0);
	}
	
	switch (ev.type) {
	case SDL_MOUSEMOTION:
		if ((win = LookupWindowByID(ev.motion.windowID)) == 0) {
			return (-1);
		}

		x = AGDRIVER_BOUNDED_WIDTH(win, ev.motion.x);
		y = AGDRIVER_BOUNDED_HEIGHT(win, ev.motion.y);
		AG_MouseMotionUpdate(WIDGET(win)->drv->mouse, x, y);
	
		dev->type = AG_DRIVER_MOUSE_MOTION;
		dev->win = win;
		dev->data.motion.x = x;
		dev->data.motion.y = y;
		break;

	case SDL_MOUSEBUTTONUP:
		if ((win = LookupWindowByID(ev.button.windowID)) == 0) {
			return (-1);
		}

		AG_MouseButtonUpdate(WIDGET(win)->drv->mouse, AG_BUTTON_RELEASED, ev.button.button);

		dev->type = AG_DRIVER_MOUSE_BUTTON_UP;
		dev->win = win;
		dev->data.button.which = (AG_MouseButton)ev.button.button;
		dev->data.button.x = ev.button.x;
		dev->data.button.y = ev.button.y;
		break;

	case SDL_MOUSEBUTTONDOWN:
		if ((win = LookupWindowByID(ev.button.windowID)) == 0) {
			return (-1);
		}

		AG_MouseButtonUpdate(WIDGET(win)->drv->mouse, AG_BUTTON_PRESSED, ev.button.button);

		dev->type = AG_DRIVER_MOUSE_BUTTON_DOWN;
		dev->win = win;
		dev->data.button.which = (AG_MouseButton)ev.button.button;
		dev->data.button.x = ev.button.x;
		dev->data.button.y = ev.button.y;
		break;

	case SDL_KEYDOWN:
	case SDL_KEYUP:
		if ((win = LookupWindowByID(ev.key.windowID)) == 0) {
			return (-1);
		}

		if (ev.key.repeat) 
		{
			return (-1);
		}

		ks = AG_SDL2_Translate_KeySym(ev.key.keysym);
		unicode = AG_SDL2_Translate_UniCode(ev.key.keysym);

		if (ev.type == SDL_KEYDOWN)
		{
			AG_KeyboardUpdate(WIDGET(win)->drv->kbd, AG_KEY_PRESSED, ks, unicode);
			dev->type = AG_DRIVER_KEY_DOWN;
		}
		else
		{
			AG_KeyboardUpdate(WIDGET(win)->drv->kbd, AG_KEY_RELEASED, ks, unicode);
			dev->type = AG_DRIVER_KEY_UP;
		}
	
		dev->win = win;
		dev->data.key.ks = ks;
		dev->data.key.ucs = unicode;
		break;

	case SDL_WINDOWEVENT:
		switch(ev.window.event)
		{
			case SDL_WINDOWEVENT_RESIZED:
				w = (int)ev.window.data1;
				h = (int)ev.window.data2;

				if ((w + h) == 0)
				{
					return (-1);
				}

				if ((win = LookupWindowByID(ev.window.windowID)) == 0) 
				{
					return (-1);
				}

				dev->type = AG_DRIVER_VIDEORESIZE;
				dev->win = win;
				SDL_GetWindowPosition(SDL_GetWindowFromID(ev.window.windowID), &x, &y);
				dev->data.videoresize.x = x;
				dev->data.videoresize.y = y;
				dev->data.videoresize.w = w;
				dev->data.videoresize.h = h;

				// Need to update the default clipRect to match any resize in the window. 

				sdl = (AG_DriverSDL2*)WIDGET(win)->drv;
				if (sdl->nClipRects > 0)
				{
					sdl->clipRects[0].r.w = w;
					sdl->clipRects[0].r.h = h;
				}

				SDL_RenderSetLogicalSize(sdl->r, w, h);
				SDL_GetRendererOutputSize(sdl->r, &w, &h);
				//printf("SDL:Resize %i %i\n", w, h);
				break;

			case SDL_WINDOWEVENT_MOVED:
				if ((win = LookupWindowByID(ev.window.windowID)) == 0) {
					return (-1);
				}

				dev->type = AG_DRIVER_VIDEORESIZE;
				dev->win = win;
				SDL_GetWindowSize(SDL_GetWindowFromID(ev.window.windowID), &w, &h);
				dev->data.videoresize.x = (int)ev.window.data1;
				dev->data.videoresize.y = (int)ev.window.data2;
				dev->data.videoresize.w = w;
				dev->data.videoresize.h = h;
				break;

			case SDL_WINDOWEVENT_EXPOSED:
				if ((win = LookupWindowByID(ev.window.windowID)) == 0) {
					return (-1);
				}

				dev->type = AG_DRIVER_EXPOSE;
				dev->win = win;
				break;

			case SDL_WINDOWEVENT_ENTER:
				if ((win = LookupWindowByID(ev.window.windowID)) == NULL) {
					return (-1);
				}

				dev->type = AG_DRIVER_MOUSE_ENTER;
				dev->win = win;
				break;

			case SDL_WINDOWEVENT_LEAVE:
				if ((win = LookupWindowByID(ev.window.windowID)) == NULL) {
					return (-1);
				}

				dev->type = AG_DRIVER_MOUSE_LEAVE;
				dev->win = win;
				break;

			case SDL_WINDOWEVENT_FOCUS_GAINED:
				if ((win = LookupWindowByID(ev.window.windowID)) == NULL) {
					return (-1);
				}

				dev->type = AG_DRIVER_FOCUS_IN;
				dev->win = win;
				break;

			case SDL_WINDOWEVENT_FOCUS_LOST:
				if ((win = LookupWindowByID(ev.window.windowID)) == NULL) {
					return (-1);
				}

				dev->type = AG_DRIVER_FOCUS_OUT;
				dev->win = win;
				break;

			case SDL_WINDOWEVENT_CLOSE:
				if ((win = LookupWindowByID(ev.window.windowID)) == NULL) {
					return (-1);
				}

				dev->type = AG_DRIVER_CLOSE;
				dev->win = win;
				break;

			default:
				return (-1);
		}
		break;

	case SDL_QUIT:
	case SDL_USEREVENT:
		if ((win = LookupWindowByID(ev.user.windowID)) == 0) {
			return (-1);
		}

		dev->type = AG_DRIVER_CLOSE;
		dev->win = win;
		break;

	default:
		dev->type = AG_DRIVER_UNKNOWN;
		dev->win = NULL;
		break;
	}

	//printf("Next Event %i %i, %p \n", et, dev->type, dev->win);
	return (1);
}

/*
 * Process an input device event.
 * The agDrivers VFS must be locked.
 */

int
AG_SDL2_ProcessEvent(void *obj, AG_DriverEvent *dev)
{
	AG_Driver *drv;
	AG_SizeAlloc a;
	int rv = 1;

	if (dev->win == NULL)
		return (0);
		
	//printf("Process Event %i, %p \n", dev->type, dev->win);

	if (dev->win->flags & AG_WINDOW_DETACHING)
		return (0);

	AG_LockVFS(&agDrivers);
	drv = WIDGET(dev->win)->drv;

	switch (dev->type) {
	case AG_DRIVER_MOUSE_MOTION:
		AG_ProcessMouseMotion(dev->win,
		    dev->data.motion.x, dev->data.motion.y,
		    drv->mouse->xRel, drv->mouse->yRel,
		    drv->mouse->btnState);
		AG_MouseCursorUpdate(dev->win,
		     dev->data.motion.x, dev->data.motion.y);
		break;
	case AG_DRIVER_MOUSE_BUTTON_DOWN:
		AG_ProcessMouseButtonDown(dev->win,
		    dev->data.button.x, dev->data.button.y,
		    dev->data.button.which);
		break;
	case AG_DRIVER_MOUSE_BUTTON_UP:
		AG_ProcessMouseButtonUp(dev->win,
		    dev->data.button.x, dev->data.button.y,
		    dev->data.button.which);
		break;
	case AG_DRIVER_KEY_UP:
		AG_ProcessKey(drv->kbd, dev->win, AG_KEY_RELEASED,
		    dev->data.key.ks, dev->data.key.ucs);
		break;
	case AG_DRIVER_KEY_DOWN:
		AG_ProcessKey(drv->kbd, dev->win, AG_KEY_PRESSED,
		    dev->data.key.ks, dev->data.key.ucs);
		break;
	case AG_DRIVER_MOUSE_ENTER:
		AG_PostEvent(NULL, dev->win, "window-enter", NULL);
		break;
	case AG_DRIVER_MOUSE_LEAVE:
		AG_PostEvent(NULL, dev->win, "window-leave", NULL);
		break;
	case AG_DRIVER_FOCUS_IN:
		if (agWindowFocused != dev->win) {
			agWindowFocused = dev->win;
			AG_PostEvent(NULL, dev->win, "window-gainfocus", NULL);
		}
		break;
	case AG_DRIVER_FOCUS_OUT:
		if (agWindowFocused == dev->win) {
			AG_PostEvent(NULL, dev->win, "window-lostfocus", NULL);
			agWindowFocused = NULL;
		}
		break;
	case AG_DRIVER_VIDEORESIZE:
		a.x = dev->data.videoresize.x;
		a.y = dev->data.videoresize.y;
		a.w = dev->data.videoresize.w;
		a.h = dev->data.videoresize.h;
		if (a.x != WIDGET(dev->win)->x || a.y != WIDGET(dev->win)->y) {
			SDL2_PostMoveCallback(dev->win, &a);
		}
		if (a.w != WIDTH(dev->win) || a.h != HEIGHT(dev->win)) {
			SDL2_PostResizeCallback(dev->win, &a);
		}
		break;
	case AG_DRIVER_CLOSE:
		AG_PostEvent(NULL, dev->win, "window-close", NULL);
		break;
	case AG_DRIVER_EXPOSE:
		dev->win->dirty = 1;
		break;
	default:
		rv = 0;
		break;
	}

	AG_UnlockVFS(&agDrivers);
	return (rv);
}

/*
 * Standard event sink for AG_EventLoop().
 *
 * TODO where AG_SINK_READ capability and pipes are available,
 * could we create a separate thread running SDL_WaitEvent() and
 * sending notifications over a pipe, instead of using a spinner?
 */
int
AG_SDL2_EventSink(AG_EventSink *es, AG_Event *event)
{
	AG_DriverEvent dev;
	AG_Driver *drv = AG_PTR(1);
	int rv = 0;

	if (SDL_PollEvent(NULL) != 0) {
		while (AG_SDL2_GetNextEvent(drv, &dev) == 1)
			rv = AG_SDL2_ProcessEvent(drv, &dev);
	} else {
		AG_Delay(1);
	}
	return (rv); // was zero
}

int
AG_SDL2_EventEpilogue(AG_EventSink *es, AG_Event *event)
{
	AG_WindowDrawQueued();
	AG_WindowProcessQueued();
	return (0);
}

static void
SDL2_BeginRendering(void *obj)
{
	AG_DriverSDL2 *sdl = obj;
}

static void
SDL2_RenderWindow(AG_Window *win)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;

	AG_WidgetDraw(win);
}

static void
SDL2_EndRendering(void *obj)
{
	AG_DriverSDL2 *sdl = obj;

    SDL_RenderPresent(sdl->r);
}

static void
SDL2_FillRect(void *obj, AG_Rect r, AG_Color c)
{
	AG_DriverSDL2 *sdl = obj;
	SDL_Rect sr;

	sr.x = r.x;
	sr.y = r.y;
	sr.w = r.w;
	sr.h = r.h;

	SDL_SetRenderDrawColor(sdl->r, c.r, c.g, c.b, c.a);
	SDL_RenderFillRect(sdl->r, &sr);
}

/*
 * Clipping and blending control (rendering context)
 */

static void
SDL2_PushClipRect(void *obj, AG_Rect r)
{
	AG_DriverSDL2 *sdl = obj;
	AG_ClipRect *cr, *crPrev;
	SDL_Rect sr;

	sdl->clipRects = Realloc(sdl->clipRects, (sdl->nClipRects+1) * sizeof(AG_ClipRect));
	crPrev = &sdl->clipRects[sdl->nClipRects-1];
	cr = &sdl->clipRects[sdl->nClipRects++];
	cr->r = r; /* AG_RectIntersect(&crPrev->r, &r); */

	sr.x = (Sint16)cr->r.x;
	sr.y = (Sint16)cr->r.y;
	sr.w = (Uint16)cr->r.w;
	sr.h = (Uint16)cr->r.h;
	SDL_RenderSetClipRect(sdl->r, &sr);
	// printf("PushClipRect %i %i : %i %i %i %i\n", sdl->wid, sdl->nClipRects-1, sr.x, sr.y, sr.w, sr.h);
}

static void
SDL2_PopClipRect(void *obj)
{
	AG_DriverSDL2 *sdl = obj;
	AG_ClipRect *cr;
	SDL_Rect sr;
	int n;

	if (sdl->nClipRects < 2) 
	{
		//printf("POP ERROR!\n");
		return;
	}

	sdl->nClipRects--;
	n = sdl->nClipRects-1;

	cr = &sdl->clipRects[n];

	sr.x = (Sint16)cr->r.x;
	sr.y = (Sint16)cr->r.y;
	sr.w = (Uint16)cr->r.w;
	sr.h = (Uint16)cr->r.h;
	SDL_RenderSetClipRect(sdl->r, &sr);
	// printf("PopClipRect %i %i : %i %i %i %i (%i %i %i %i)\n", 
	// 	sdl->wid, sdl->nClipRects, 
	// 	sr.x, sr.y, sr.w, sr.h,
	// 	sdl->clipRects[0].r.x, sdl->clipRects[0].r.y, sdl->clipRects[0].r.w, sdl->clipRects[0].r.h);
}

/* Get corresponding SDL2 blending function */
static __inline__ unsigned int
AG_SDL2_GetBlendingFunc(AG_BlendFn fn)
{
	switch (fn) {
	case AG_ALPHA_ONE:		return (SDL_BLENDFACTOR_ONE);
	case AG_ALPHA_ZERO:		return (SDL_BLENDFACTOR_ZERO);
	case AG_ALPHA_SRC:		return (SDL_BLENDFACTOR_SRC_ALPHA);
	case AG_ALPHA_DST:		return (SDL_BLENDFACTOR_DST_ALPHA);
	case AG_ALPHA_ONE_MINUS_DST:	return (SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA);
	case AG_ALPHA_ONE_MINUS_SRC:	return (SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA);
	case AG_ALPHA_OVERLAY:		return (SDL_BLENDFACTOR_ONE);	/* XXX */
	default:			return (SDL_BLENDFACTOR_ONE);
	}
}

static void
SDL2_PushBlendingMode(void *drv, AG_BlendFn fnSrc, AG_BlendFn fnDst)
{
	AG_DriverSDL2 *sdl = drv;
	SDL_BlendFactor asf, dsf;
	SDL_BlendOperation ao;

	switch (fnSrc) 
	{
		case AG_ALPHA_ZERO:				asf = SDL_BLENDFACTOR_ZERO ;				dsf = SDL_BLENDFACTOR_ZERO ; 				ao = SDL_BLENDOPERATION_ADD; break;
		case AG_ALPHA_OVERLAY:			asf = SDL_BLENDFACTOR_ONE ;					dsf = SDL_BLENDFACTOR_ONE ; 				ao = SDL_BLENDOPERATION_ADD; break;
		case AG_ALPHA_DST:				asf = SDL_BLENDFACTOR_ZERO ;				dsf = SDL_BLENDFACTOR_ONE ; 				ao = SDL_BLENDOPERATION_ADD; break;
		case AG_ALPHA_ONE_MINUS_DST:	asf = SDL_BLENDFACTOR_ZERO ;				dsf = SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA ;	ao = SDL_BLENDOPERATION_ADD; break;
		case AG_ALPHA_ONE_MINUS_SRC:	asf = SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA ;	dsf = SDL_BLENDFACTOR_ZERO ; 				ao = SDL_BLENDOPERATION_ADD; break;
		default:
		case AG_ALPHA_SRC:
		case AG_ALPHA_ONE:				asf = SDL_BLENDFACTOR_ONE ;					dsf = SDL_BLENDFACTOR_ZERO ; 				ao = SDL_BLENDOPERATION_ADD; break;
	}

	SDL_BlendMode bm = SDL_ComposeCustomBlendMode(
		AG_SDL2_GetBlendingFunc(fnSrc), // Src Color Func
		AG_SDL2_GetBlendingFunc(fnDst), // Dst Color Func
		SDL_BLENDOPERATION_ADD,			// Color operation
		asf,							// Src Alpha Func
		dsf,							// Dst Alpha Func
		ao);							// Alpha operation

	SDL_SetRenderDrawBlendMode(sdl->r, bm);
	//SDL_SetRenderDrawBlendMode(sdl->r, SDL_BLENDMODE_ADD);

	/* No-op (handle blending on a per-blit basis) */
}

static void
SDL2_PopBlendingMode(void *drv)
{
	AG_DriverSDL2 *sdl = drv;

	SDL_SetRenderDrawBlendMode(sdl->r, SDL_BLENDMODE_NONE);

	/* No-op (handle blending on a per-blit basis) */
}

/* Return the corresponding Agar PixelFormat structure for a SDL_Surface. */
AG_PixelFormat *
AG_SDL2_GetPixelFormat(SDL_Surface *su)
{
	switch (su->format->BytesPerPixel) {
	case 1:
		return AG_PixelFormatIndexed(su->format->BitsPerPixel);
	case 2:
	case 3:
	case 4:
		if (su->format->Amask != 0) {
			return AG_PixelFormatRGBA(su->format->BitsPerPixel,
			                          su->format->Rmask,
			                          su->format->Gmask,
			                          su->format->Bmask,
						  su->format->Amask);
		} else {
			return AG_PixelFormatRGB(su->format->BitsPerPixel,
			                         su->format->Rmask,
			                         su->format->Gmask,
			                         su->format->Bmask);
		}
	default:
		AG_SetError("Unsupported pixel depth (%d bpp)",
		    (int)su->format->BitsPerPixel);
		return (NULL);
	}
}

/* Convert a SDL_Surface to an AG_Surface. */

AG_Surface *
AG_SDL2_ImportSurface(SDL_Surface *ss)
{
	AG_PixelFormat *pf;
	AG_Surface *ds;
	Uint8 *pSrc, *pDst;
	int y;

	if ((pf = AG_SDL2_GetPixelFormat(ss)) == NULL) {
		return (NULL);
	}

	if (pf->palette != NULL) {
		AG_SetError("Indexed formats not supported");
		AG_PixelFormatFree(pf);
		return (NULL);
	}

	if ((ds = AG_SurfaceNew(AG_SURFACE_PACKED, ss->w, ss->h, pf, 0)) == NULL) {
		goto out;
	}
	
	if (SDL_MUSTLOCK(ss)) 
	{
		SDL_LockSurface(ss);
	}

	pSrc = (Uint8 *)ss->pixels;
	pDst = (Uint8 *)ds->pixels;

	if (ss->pitch == ds->pitch)
	{
		memcpy(pDst, pSrc, ss->h * ss->pitch);
	}
	else
	{
		for (y = 0; y < ss->h; y++) 
		{
			memcpy(pDst, pSrc, ss->pitch);
			pSrc += ss->pitch;
			pDst += ds->pitch;
		}
	}

	if (SDL_MUSTLOCK(ss))
		SDL_UnlockSurface(ss);

out:
	AG_PixelFormatFree(pf);
	return (ds);
}

/* Initialize the default cursor. */
int
AG_SDL2_InitDefaultCursor(void *obj)
{
	AG_Driver *drv = AGDRIVER(obj);
	AG_Cursor *ac;
	SDL_Cursor *sc;
	
	if ((sc = SDL_GetCursor()) == NULL) {
		AG_SetError("SDL_GetCursor() returned NULL");
		return (-1);
	}
	if ((ac = TryMalloc(sizeof(AG_Cursor))) == NULL) {
		return (-1);
	}
	AG_CursorInit(ac);
	ac->p = sc;

	TAILQ_INSERT_HEAD(&drv->cursors, ac, cursors);
	drv->nCursors++;
	return (0);
}

/* Change the cursor. */
int
AG_SDL2_SetCursor(void *obj, AG_Cursor *ac)
{
	AG_Driver *drv = obj;
	
	if (drv->activeCursor == ac)
		return (0);

	SDL_SetCursor((SDL_Cursor *)ac->p);
	drv->activeCursor = ac;
	return (0);
}

/* Revert to the default cursor. */
void
AG_SDL2_UnsetCursor(void *obj)
{
	AG_Driver *drv = obj;
	AG_Cursor *ac0 = TAILQ_FIRST(&drv->cursors);
	
	if (drv->activeCursor == ac0)
		return;
	
	SDL_SetCursor((SDL_Cursor *)ac0->p);
	drv->activeCursor = ac0;
}

/* Configure refresh rate. */
int
AG_SDL2_SetRefreshRate(void *obj, int fps)
{
	AG_DriverSw *dsw = obj;

	if (fps < 1) {
		AG_SetError("Invalid refresh rate");
		return (-1);
	}
	dsw->rNom = 1000/fps;
	return (0);
}

/* Create a cursor. */
AG_Cursor *
AG_SDL2_CreateCursor(void *obj, Uint w, Uint h, const Uint8 *data,
    const Uint8 *mask, int xHot, int yHot)
{
	AG_Cursor *ac;
	SDL_Cursor *sc;
	Uint size = w*h;
	
	if ((ac = TryMalloc(sizeof(AG_Cursor))) == NULL) {
		return (NULL);
	}
	if ((ac->data = TryMalloc(size)) == NULL) {
		free(ac);
		return (NULL);
	}
	if ((ac->mask = TryMalloc(size)) == NULL) {
		free(ac->data);
		free(ac);
		return (NULL);
	}
	memcpy(ac->data, data, size);
	memcpy(ac->mask, mask, size);
	ac->w = w;
	ac->h = h;
	ac->xHot = xHot;
	ac->yHot = yHot;

	sc = SDL_CreateCursor(ac->data, ac->mask,
	    ac->w, ac->h,
	    ac->xHot, ac->yHot);
	if (sc == NULL) {
		AG_SetError("SDL_CreateCursor failed");
		goto fail;
	}
	ac->p = (void *)sc;
	return (ac);
fail:
	free(ac->data);
	free(ac->mask);
	free(ac);
	return (NULL);
}

/* Release a cursor. */
void
AG_SDL2_FreeCursor(void *obj, AG_Cursor *ac)
{
	AG_Driver *drv = obj;

	if (ac == drv->activeCursor)
		drv->activeCursor = NULL;

	SDL_FreeCursor((SDL_Cursor *)(ac->p));
	free(ac->data);
	free(ac->mask);
	free(ac);
}

/* Retrieve cursor visibility status. */
int
AG_SDL2_GetCursorVisibility(void *obj)
{
	return (SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE);
}

/* Set cursor visibility. */
void
AG_SDL2_SetCursorVisibility(void *obj, int flag)
{
	SDL_ShowCursor(flag ? SDL_ENABLE : SDL_DISABLE);
}

/* Return the desktop display size in pixels. */
int
AG_SDL2_GetDisplaySize(Uint *w, Uint *h)
{
	SDL_Rect db;
	SDL_GetDisplayUsableBounds(0, &db);
	*w = db.w;
	*h = db.h;
	return (0);
}

/*
 * Texture operstions
 * 
 * Texture operations require the driver to identify the texture from a provided
 * texture ID.  For SDL the textureID is an SDL_Texture pointer.
 * 
 * Modifications to the AGAR AG_Widget structure were made changing the textures pointer from
 * a Uint32* to a Uint64* and several changes to the Widget class to correctly alloc the changed
 * memory requirements. Originally the 32 bit pointer was suitable for GLuint pointers which are
 * 32 bit but lacked support for other drivers that required 64 bit IDs.
 *
 * See widget.c and widget.h for more comments. 
 */

void
SDL2_UploadTexture(void *drv, void *rtexture, AG_Surface *su, AG_TexCoord *tc)
{
	AG_DriverSDL2 *sdl = drv;
	SDL_Texture **ppt = (SDL_Texture*)rtexture;
	SDL_Texture *pt;
	void *pixels;
	int pitch;
	Uint tid = 0; // Must be zero to create a new texture

	if (su->w == 0 || su->h == 0) return;

	if (tc != NULL) 
	{
		tc->x = 0.0f;
		tc->y = 0.0f;
		tc->w = (float)su->w / (float)su->w;
		tc->h = (float)su->h / (float)su->h;
	}
	
	/* Upload as an SDL texture. */

	//texture = SDL_CreateTexture(sdl->r, sdl->f, SDL_TEXTUREACCESS_STREAMING, su->w, su->h);

	#if AG_BYTEORDER == AG_BIG_ENDIAN 
		pt = SDL_CreateTexture(sdl->r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, su->w, su->h);
	#else
		pt = SDL_CreateTexture(sdl->r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, su->w, su->h);
	#endif

	ntexturemaps++;

	if (pt != NULL)
	{
		SDL_LockTexture(pt, NULL, &pixels, &pitch);
		memcpy(pixels, su->pixels, pitch * su->h);
		SDL_UnlockTexture(pt);
	}

	*ppt = pt;
}

/*
	SDL requires a destructive update of the Texture when the texture size changes
	However the AGAR API for this function does not accomodate pointers to the texture ID in order
	to facilitate rewriting the ID (as does the previous upload function).

	So we cast the provided Uint64 as a Uint64 pointer.
*/
int
SDL2_UpdateTexture(void *drv, void *textureID, AG_Surface *su, AG_TexCoord *tc)
{
	AG_DriverSDL2 *sdl = drv;
	SDL_Texture **ppt = (SDL_Texture*)textureID; // pointer to Texture pointer
	SDL_Texture *pt = NULL; // Texture Pointer (the texture)
	SDL_Rect r;

	pt = *ppt;

	if (tc != NULL) 
	{
		tc->x = 0.0f;
		tc->y = 0.0f;
		tc->w = (float)su->w / (float)su->w;
		tc->h = (float)su->h / (float)su->h;
	}

	void *pixels;
	int pitch;
	int tw, th;

	SDL_QueryTexture(pt, NULL, NULL, &tw, &th);

	// Only Update if the required texture size is different from the previous size

	if (tw != su->w || th != su->h)
	{
		SDL_DestroyTexture(pt);
		pt = SDL_CreateTexture(sdl->r, sdl->f, SDL_TEXTUREACCESS_STREAMING, su->w, su->h);
	}

	// fprintf(stderr, "Update Texture\n");

	if (pt != NULL)
	{
		SDL_LockTexture(pt, NULL, &pixels, &pitch);
		memcpy(pixels, su->pixels, pitch * su->h);
		SDL_UnlockTexture(pt);
	}

	*ppt = pt;

	return (0);
}

void
SDL2_DeleteTexture(void *obj, void *textureID)
{
	AG_DriverSDL2 *sdl = obj;
	SDL_Texture **ppt = (SDL_Texture*)textureID;
	SDL_Texture *pt = NULL;

	if (ppt != NULL)
	{
		pt = *ppt;
	}

	if (pt != NULL)
	{
		SDL_DestroyTexture(pt);
		ntexturemaps--;
	}
}

/* Prepare a widget-bound texture for rendering. */
void
SDL2_PrepareTexture(void *obj, int s)
{
	AG_Widget *wid = obj;
	AG_Driver *drv = wid->drv;

	if (wid->textures[s] == 0) {
		SDL2_UploadTexture(drv, &wid->textures[s], wid->surfaces[s], &wid->texcoords[s]);
	} else if (wid->surfaceFlags[s] & AG_WIDGET_SURFACE_REGEN) {
		wid->surfaceFlags[s] &= ~(AG_WIDGET_SURFACE_REGEN);
		SDL2_UpdateTexture(drv, &wid->textures[s], wid->surfaces[s], &wid->texcoords[s]);
	}
}

/*
 * Surface operations (rendering context)
 * 
 * By Rendering Context AG means the final video destination.
 * 
 * In SDL 1.2 that was usually a surface tied to the Window.
 * 
 * In SDL 2 that can be a surface or direct rendering.
 * 
 * I have chosen direct rendering.
 */

/*
	Blit an AG Surface to the renderer
*/

static void
SDL2_BlitSurface(void *drv, AG_Widget *wid, AG_Surface *s, int x, int y)
{
	AG_DriverSDL2 *sdl = drv;
	AG_TexCoord tc;
	SDL_Rect dr;
	SDL_Texture *texture;
	SDL_Surface *ss;

	AG_ASSERT_CLASS(drv, "AG_Driver:*");
	AG_ASSERT_CLASS(wid, "AG_Widget:*");

	dr.x = x;
	dr.y = y;
	dr.w = s->w;
	dr.h = s->h;

	ss = (SDL_Surface*)AG_SDL2_SurfaceExportSDL2(s);
	texture = SDL_CreateTextureFromSurface(sdl->r, ss);
	
	SDL2_PushBlendingMode(drv, AG_ALPHA_SRC, AG_ALPHA_ONE_MINUS_SRC);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureAlphaMod(texture, 255);
	SDL_RenderCopy(sdl->r, texture, NULL, &dr);
	SDL2_PopBlendingMode(drv);
}

/*
	Blit the pre created SDL Texture associated with the AG Widget to the renderer
*/

static void                     
SDL2_BlitSurfaceFrom(void *drv, AG_Widget *wid, AG_Widget *widSrc, int s, AG_Rect *rSrc, int x, int y)
{
	AG_DriverSDL2 *sdl = drv;
	AG_Surface *su = widSrc->surfaces[s];
	AG_TexCoord *tc;
	SDL_Rect dr;
	int w, h;

	AG_ASSERT_CLASS(drv, "AG_Driver:*");
	AG_ASSERT_CLASS(wid, "AG_Widget:*");
	AG_ASSERT_CLASS(widSrc, "AG_Widget:*");

	// fprintf(stderr, "Blit From : %s surface %i texture %i\n", widSrc->obj.name, s, widSrc->textures[s]);
	SDL2_PrepareTexture(widSrc, s);

	SDL_Texture *texture = (SDL_Texture*)widSrc->textures[s];
	if (texture == NULL) return;

	SDL_QueryTexture(texture, NULL, NULL, &w, &h);

	if (rSrc == NULL) 
	{
		tc = &widSrc->texcoords[s];
		dr.x = x;
		dr.y = y;
		dr.w = w * tc->w;
		dr.h = h * tc->h;
	}
	else 
	{
		dr.x = x;
		dr.y = y;
		dr.w = rSrc->w;
		dr.h = rSrc->h;
	}

	SDL2_PushBlendingMode(drv, AG_ALPHA_SRC, AG_ALPHA_ONE_MINUS_SRC);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureAlphaMod(texture, 255);
	SDL_RenderCopy(sdl->r, texture, NULL, &dr);
	SDL2_PopBlendingMode(drv);
}

static void
SDL2_BlitSurfaceGL(void *drv, AG_Widget *wid, AG_Surface *s, float w, float h)
{
	/* Not applicable */
}

static void
SDL2_BlitSurfaceFromGL(void *drv, AG_Widget *wid, int s, float w, float h)
{
	/* Not applicable */
}

static void
SDL2_BlitSurfaceFlippedGL(void *drv, AG_Widget *wid, int s, float w, float h)
{
	/* Not applicable */
}


/* Convert an Agar surface to an SDL surface. This is a normal SDL surface and not the rendering context */

void *
AG_SDL2_SurfaceExportSDL2(const AG_Surface *ss)
{
	Uint32 sdlFlags = SDL_SWSURFACE;
	SDL_Surface *ds;

	ds = SDL_CreateRGBSurfaceFrom(ss->pixels, ss->w, ss->h,
	    ss->format->BitsPerPixel, ss->pitch,
	    ss->format->Rmask, ss->format->Gmask, ss->format->Bmask, ss->format->Amask);

	if (ds == NULL) {
		AG_SetError("SDL_CreateRGBSurface: %s", SDL_GetError());
		return (NULL);
	}

	return (void *)(ds);
}

/* 
	Render the provided AG Widget to an AG Surface

	In order to do this the Widget must be forcably rendered to the window
	and the we use the widget's window coordinates to extract the pixels 
	from the window renderer.
 */

static int
SDL2_RenderToSurface(void *drv, AG_Widget *wid, AG_Surface **s)
{
	AG_DriverSDL2 *sdl = drv;
	int visiblePrev;
	SDL_Surface *sd;
	SDL_Rect sr;
	int pitch;

	/* XXX TODO render to offscreen buffer instead */
	AG_BeginRendering(AGDRIVER(sdl));
	visiblePrev = wid->window->visible;
	wid->window->visible = 1;
	AG_WindowDraw(wid->window);
	wid->window->visible = visiblePrev;
	AG_EndRendering(AGDRIVER(sdl));

	sr.x = wid->rView.x1;
	sr.y = wid->rView.y1;
	sr.w = wid->w;
	sr.h = wid->h;
	pitch = wid->w * 4;

	void *pixels = malloc(wid->h * pitch);

	SDL_RenderReadPixels(sdl->r, &sr, 0, pixels, pitch);
		
	sd = SDL_CreateRGBSurfaceFrom(pixels, wid->w, wid->h, 32, pitch, 
		sdl->pf->Rmask, sdl->pf->Gmask, sdl->pf->Bmask, sdl->pf->Amask);
		
	if (sd == NULL) {
		return (-1);
	}

	if ((*s = AG_SDL2_ImportSurface(sd)) == NULL) {
		SDL_FreeSurface(sd);
		return (-1);
	}

	SDL_FreeSurface(sd);
	return (0);
}

/*
 * Rendering operations (rendering context)
 */

static void
SDL2_PutPixel(void *obj, int x, int y, AG_Color c)
{
	AG_DriverSDL2 *sdl = obj;
	SDL_Renderer *rr = sdl->r;

	SDL_SetRenderDrawColor(rr, c.r, c.g, c.b, c.a);
	SDL_RenderDrawPoint(rr, x, y);
}

static void
SDL2_PutPixel32(void *obj, int x, int y, Uint32 c)
{
	AG_DriverSDL2 *sdl = obj;
	SDL_Renderer *rr = sdl->r;
	Uint8 r, g, b, a;

	SDL_GetRGBA(c, sdl->pf, &r, &g, &b, &a);
	
	SDL_SetRenderDrawColor(rr, r, g, b, a);
	SDL_RenderDrawPoint(rr, x, y);
}

static void
SDL2_PutPixelRGB(void *obj, int x, int y, Uint8 r, Uint8 g, Uint8 b)
{
	AG_DriverSDL2 *sdl = obj;

	SDL2_PutPixel32(obj, x, y, SDL_MapRGB(sdl->pf, r, g, b));
}

static void
SDL2_BlendPixel(void *obj, int x, int y, AG_Color C, AG_BlendFn fnSrc, AG_BlendFn fnDst)
{
	SDL2_PushBlendingMode(obj, fnSrc, fnDst);
	SDL2_PutPixel(obj, x, y, C);
	SDL2_PopBlendingMode(obj);
}

static void
SDL2_DrawLine(void *obj, int x1, int y1, int x2, int y2, AG_Color C)
{
	AG_DriverSDL2 *sdl = obj;

	SDL_SetRenderDrawColor(sdl->r, C.r, C.g, C.b, C.a);
	SDL_RenderDrawLine(sdl->r, x1, y1, x2, y2);
}

static void
SDL2_DrawLineH(void *obj, int x1, int x2, int y, AG_Color C)
{
	AG_DriverSDL2 *sdl = obj;

	SDL_SetRenderDrawColor(sdl->r, C.r, C.g, C.b, C.a);
	SDL_RenderDrawLine(sdl->r, x1, y, x2, y);
}

static void
SDL2_DrawLineV(void *obj, int x, int y1, int y2, AG_Color C)
{
	AG_DriverSDL2 *sdl = obj;

	SDL_SetRenderDrawColor(sdl->r, C.r, C.g, C.b, C.a);
	SDL_RenderDrawLine(sdl->r, x, y1, x, y2);
}

static __inline__ void
SDL2_DrawLineBlended(void *obj, int x1, int y1, int x2, int y2, AG_Color C,
    AG_BlendFn fnSrc, AG_BlendFn fnDst)
{
	AG_DriverSDL2 *sdl = obj;

	SDL2_PushBlendingMode(obj, fnSrc, fnDst);
	SDL_SetRenderDrawColor(sdl->r, C.r, C.g, C.b, C.a);
	SDL_RenderDrawLine(sdl->r, x1, y1, x2, y2);
	SDL2_PopBlendingMode(obj);
}

static void
SDL2_DrawArrowUp(void *obj, int x, int y, int h, AG_Color C[2])
{
	AG_DriverSDL2 *sdl = obj;
	SDL_Point vertices[3];
	int h2 = h>>1;

	vertices[0].x = x;		vertices[0].y = y - h2;
	vertices[1].x = x - h2; vertices[1].y = y + h2;
	vertices[2].x = x + h2; vertices[2].y = y + h2;

	SDL_SetRenderDrawColor(sdl->r, C[0].r, C[0].g, C[0].b, C[0].a);
	SDL_RenderDrawLines(sdl->r, vertices, 3);
}

static void
SDL2_DrawArrowDown(void *obj, int x, int y, int h, AG_Color C[2])
{
	AG_DriverSDL2 *sdl = obj;
	SDL_Point vertices[3];
	int h2 = h>>1;

	vertices[0].x = x - h2; vertices[0].y = y - h2;
	vertices[1].x = x + h2;	vertices[1].y = y - h2;
	vertices[2].x = x; 		vertices[2].y = y + h2;

	SDL_SetRenderDrawColor(sdl->r, C[0].r, C[0].g, C[0].b, C[0].a);
	SDL_RenderDrawLines(sdl->r, vertices, 3);
}

static void
SDL2_DrawArrowLeft(void *obj, int x, int y, int h, AG_Color C[2])
{
	AG_DriverSDL2 *sdl = obj;
	SDL_Point vertices[3];
	int h2 = h>>1;

	vertices[0].x = x - h2; vertices[0].y = y;
	vertices[1].x = x + h2;	vertices[1].y = y + h2;
	vertices[2].x = x + h2; vertices[2].y = y - h2;

	SDL_SetRenderDrawColor(sdl->r, C[0].r, C[0].g, C[0].b, C[0].a);
	SDL_RenderDrawLines(sdl->r, vertices, 3);
}

static void
SDL2_DrawArrowRight(void *obj, int x, int y, int h, AG_Color C[2])
{
	AG_DriverSDL2 *sdl = obj;
	SDL_Point vertices[3];
	int h2 = h>>1;

	vertices[0].x = x + h2; vertices[0].y = y;
	vertices[1].x = x - h2;	vertices[1].y = y + h2;
	vertices[2].x = x - h2; vertices[2].y = y - h2;

	SDL_SetRenderDrawColor(sdl->r, C[0].r, C[0].g, C[0].b, C[0].a);
	SDL_RenderDrawLines(sdl->r, vertices, 3);
}

static void
SDL2_DrawBoxRoundedTop(void *obj, AG_Rect r, int z, int rad, AG_Color C[3])
{
	AG_DriverSDL2 *sdl = obj;
	SDL_PixelFormat *pf = sdl->pf;
	AG_Rect rd;
	int v, e, u;
	int x, y, i;
	Uint32 c[3];
	
	c[0] = SDL_MapRGB(pf, C[0].r, C[0].g, C[0].b);
	c[1] = SDL_MapRGB(pf, C[1].r, C[1].g, C[1].b);
	c[2] = SDL_MapRGB(pf, C[2].r, C[2].g, C[2].b);
	
	rd.x = r.x+rad;					/* Center rect */
	rd.y = r.y+rad;
	rd.w = r.w - rad*2;
	rd.h = r.h - rad;
	SDL2_DrawRectFilled(obj, rd, C[0]);
	rd.y = r.y;					/* Top rect */
	rd.h = r.h;
	SDL2_DrawRectFilled(obj, rd, C[0]);
	rd.x = r.x;					/* Left rect */
	rd.y = r.y+rad;
	rd.w = rad;
	rd.h = r.h-rad;
	SDL2_DrawRectFilled(obj, rd, C[0]);
	rd.x = r.x+r.w-rad;				/* Right rect */
	rd.y = r.y+rad;
	rd.w = rad;
	rd.h = r.h-rad;
	SDL2_DrawRectFilled(obj, rd, C[0]);

	/* Top, left and right lines */
	SDL2_DrawLineH(obj, r.x+rad,   r.x+r.w-rad, r.y,		C[0]);
	SDL2_DrawLineV(obj, r.x,       r.y+rad,     r.y+r.h,		C[1]);
	SDL2_DrawLineV(obj, r.x+r.w-1, r.y+rad,     r.y+r.h,		C[2]);

	/* Top left and top right rounded edges */
	v = 2*rad - 1;
	e = 0;
	u = 0;
	x = 0;
	y = rad;
	while (x <= y) {
		SDL2_PutPixel32(obj, r.x+rad-x, r.y+rad-y,         c[1]);
		SDL2_PutPixel32(obj, r.x+rad-y, r.y+rad-x,         c[1]);
		SDL2_PutPixel32(obj, r.x-rad+(r.w-1)+x, r.y+rad-y, c[2]);
		SDL2_PutPixel32(obj, r.x-rad+(r.w-1)+y, r.y+rad-x, c[2]);
		for (i = 0; i < x; i++) {
			SDL2_PutPixel32(obj, r.x+rad-i, r.y+rad-y,         c[0]);
			SDL2_PutPixel32(obj, r.x-rad+(r.w-1)+i, r.y+rad-y, c[0]);
		}
		for (i = 0; i < y; i++) {
			SDL2_PutPixel32(obj, r.x+rad-i, r.y+rad-x,         c[0]);
			SDL2_PutPixel32(obj, r.x-rad+(r.w-1)+i, r.y+rad-x, c[0]);
		}
		e += u;
		u += 2;
		if (v < 2*e) {
			y--;
			e -= v;
			v -= 2;
		}
		x++;
	}
}

static void
SDL2_DrawBoxRounded(void *obj, AG_Rect r, int z, int rad, AG_Color C[3])
{
	AG_DriverSDL2 *sdl = obj;
	SDL_PixelFormat *pf = sdl->pf;
	AG_Rect rd;
	int v, e, u;
	int x, y, i;
	int w1 = r.w - 1;
	Uint32 c[3];
	
	if (rad*2 > r.w || rad*2 > r.h) {
		rad = MIN(r.w/2, r.h/2);
	}
	if (r.w < 4 || r.h < 4)
		return;
	
	c[0] = SDL_MapRGB(pf, C[0].r, C[0].g, C[0].b);
	c[1] = SDL_MapRGB(pf, C[1].r, C[1].g, C[1].b);
	c[2] = SDL_MapRGB(pf, C[2].r, C[2].g, C[2].b);
	
	rd.x = r.x + rad;					/* Center */
	rd.y = r.y + rad;
	rd.w = r.w - rad*2;
	rd.h = r.h - rad*2;
	SDL2_DrawRectFilled(obj, rd, C[0]);
	rd.y = r.y;						/* Top */
	rd.h = rad;
	SDL2_DrawRectFilled(obj, rd, C[0]);
	rd.y = r.y+r.h - rad;					/* Bottom */
	rd.h = rad;
	SDL2_DrawRectFilled(obj, rd, C[0]);
	rd.x = r.x;						/* Left */
	rd.y = r.y + rad;
	rd.w = rad;
	rd.h = r.h - rad*2;
	SDL2_DrawRectFilled(obj, rd, C[0]);
	rd.x = r.x + r.w - rad;					/* Right */
	rd.y = r.y + rad;
	rd.w = rad;
	rd.h = r.h - rad*2;
	SDL2_DrawRectFilled(obj, rd, C[0]);

	/* Rounded edges */
	v = 2*rad - 1;
	e = 0;
	u = 0;
	x = 0;
	y = rad;
	while (x <= y) {
		SDL2_PutPixel32(obj, r.x+rad-x,    r.y+rad-y,     	c[1]);
		SDL2_PutPixel32(obj, r.x+rad-y,    r.y+rad-x,     	c[1]);
		SDL2_PutPixel32(obj, r.x-rad+w1+x, r.y+rad-y,     	c[2]);
		SDL2_PutPixel32(obj, r.x-rad+w1+y, r.y+rad-x,     	c[2]);

		SDL2_PutPixel32(obj, r.x+rad-x,    r.y+r.h-rad+y, 	c[1]);
		SDL2_PutPixel32(obj, r.x+rad-y,    r.y+r.h-rad+x, 	c[1]);
		SDL2_PutPixel32(obj, r.x-rad+w1+x, r.y+r.h-rad+y, 	c[2]);
		SDL2_PutPixel32(obj, r.x-rad+w1+y, r.y+r.h-rad+x, 	c[2]);

		for (i = 0; i < x; i++) {
			SDL2_PutPixel32(obj, r.x+rad-i,    r.y+rad-y,	c[0]);
			SDL2_PutPixel32(obj, r.x-rad+w1+i, r.y+rad-y,	c[0]);
			SDL2_PutPixel32(obj, r.x+rad-i,    r.y+r.h-rad+y,	c[0]);
			SDL2_PutPixel32(obj, r.x-rad+w1+i, r.y+r.h-rad+y,	c[0]);
		}
		for (i = 0; i < y; i++) {
			SDL2_PutPixel32(obj, r.x+rad-i,    r.y+rad-x,	c[0]);
			SDL2_PutPixel32(obj, r.x-rad+w1+i, r.y+rad-x,	c[0]);
			SDL2_PutPixel32(obj, r.x+rad-i,    r.y+r.h-rad+x,	c[0]);
			SDL2_PutPixel32(obj, r.x-rad+w1+i, r.y+r.h-rad+x,	c[0]);
		}
		e += u;
		u += 2;
		if (v < 2*e) {
			y--;
			e -= v;
			v -= 2;
		}
		x++;
	}
	
	/* Contour lines */
	SDL2_DrawLineH(obj, r.x+rad,   r.x+r.w-rad,   r.y,         C[0]);
	SDL2_DrawLineH(obj, r.x+rad/2, r.x+r.w-rad/2, r.y,         C[1]);
	SDL2_DrawLineH(obj, r.x+rad/2, r.x+r.w-rad/2, r.y+r.h,     C[2]);
	SDL2_DrawLineV(obj, r.x,       r.y+rad,       r.y+r.h-rad, C[1]);
	SDL2_DrawLineV(obj, r.x+w1,    r.y+rad,       r.y+r.h-rad, C[2]);
}

static void
SDL2_DrawCircle(void *obj, int x1, int y1, int radius, AG_Color C)
{
	AG_DriverSDL2 *sdl = obj;
	SDL_PixelFormat *pf = sdl->pf;
	int v = 2*radius - 1;
	int e = 0, u = 1;
	int x = 0, y = radius;
	Uint32 c;

	c = SDL_MapRGB(pf, C.r, C.g, C.b);
	while (x < y) {
		SDL2_PutPixel32(obj, x1+x, y1+y, c);
		SDL2_PutPixel32(obj, x1+x, y1-y, c);
		SDL2_PutPixel32(obj, x1-x, y1+y, c);
		SDL2_PutPixel32(obj, x1-x, y1-y, c);
		e += u;
		u += 2;
		if (v < 2*e) {
			y--;
			e -= v;
			v -= 2;
		}
		x++;
		SDL2_PutPixel32(obj, x1+y, y1+x, c);
		SDL2_PutPixel32(obj, x1+y, y1-x, c);
		SDL2_PutPixel32(obj, x1-y, y1+x, c);
		SDL2_PutPixel32(obj, x1-y, y1-x, c);
	}
	SDL2_PutPixel32(obj, x1-radius, y1, c);
	SDL2_PutPixel32(obj, x1+radius, y1, c);
}

static void
SDL2_DrawCircleFilled(void *obj, int x1, int y1, int radius, AG_Color C)
{
	int v = 2*radius - 1;
	int e = 0, u = 1;
	int x = 0, y = radius;

	while (x < y) {
		SDL2_DrawLineV(obj, x1+x, y1+y, y1-y, C);
		SDL2_DrawLineV(obj, x1-x, y1+y, y1-y, C);

		e += u;
		u += 2;
		if (v < 2*e) {
			y--;
			e -= v;
			v -= 2;
		}
		x++;
		
		SDL2_DrawLineV(obj, x1+y, y1+x, y1-x, C);
		SDL2_DrawLineV(obj, x1-y, y1+x, y1-x, C);
	}
}

static void
SDL2_DrawRectFilled(void *obj, AG_Rect r, AG_Color C)
{
	AG_DriverSDL2 *sdl = obj;
	SDL_Rect rd;

	rd.x = (Sint16)r.x < 0 ? 0 : (Sint16)r.x;
	rd.y = (Sint16)r.y < 0 ? 0 : (Sint16)r.y;
	rd.w = (Uint16)r.w;
	rd.h = (Uint16)r.h;

	SDL_SetRenderDrawColor(sdl->r, C.r, C.g, C.b, C.a);
	SDL_RenderFillRect(sdl->r, &rd);
}

static void
SDL2_DrawRectBlended(void *obj, AG_Rect r, AG_Color C, AG_BlendFn fnSrc, AG_BlendFn fnDst)
{
	AG_DriverSDL2 *sdl = obj;
	SDL_Rect rd;

	rd.x = (Sint16)r.x < 0 ? 0 : (Sint16)r.x;
	rd.y = (Sint16)r.y < 0 ? 0 : (Sint16)r.y;
	rd.w = (Uint16)r.w;
	rd.h = (Uint16)r.h;

	SDL2_PushBlendingMode(obj, fnSrc, fnDst);
	SDL_SetRenderDrawColor(sdl->r, C.r, C.g, C.b, C.a);
	SDL_RenderFillRect(sdl->r, &rd);
	SDL2_PopBlendingMode(obj);
}

static void
SDL2_DrawRectDithered(void *obj, AG_Rect r, AG_Color C)
{
	AG_DriverSDL2 *sdl = obj;
	SDL_PixelFormat *pf = sdl->pf;
	int x, y;
	int flag = 0;
	Uint32 c;
	
	/* XXX inefficient */
	c = SDL_MapRGB(pf, C.r, C.g, C.b);
	for (y = r.y; y < r.y+r.h-2; y++) {
		flag = !flag;
		for (x = r.x+1+flag; x < r.x+r.w-2; x+=2)
			SDL2_PutPixel32(obj, x, y, c);
	}
}

static void
SDL2_UpdateGlyph(void *drv, AG_Glyph *gl)
{
	AG_DriverSDL2 *sdl = drv;
	SDL_Texture *texture = NULL;
	SDL_Surface *ss = NULL;
	int pitch, depth;

	if (gl->su->w == 0 || gl->su->h == 0) return ;

	pitch = gl->su->pitch;
	depth = pitch / gl->su->w * 8;

	texture = (SDL_Texture*)gl->texture;

	if (texture != NULL)
	{
		int tw, th;

		SDL_QueryTexture(texture, NULL, NULL, &tw, &th);

		// Only Update if the required texture size is different from the previous size

		if (tw != gl->su->w || th != gl->su->h)
		{
			SDL_DestroyTexture(texture);
			texture = NULL;
		}
	}
	
	if (texture == NULL)
	{
		ss = SDL_CreateRGBSurfaceFrom(gl->su->pixels, gl->su->w, gl->su->h, depth, pitch, 
			gl->su->format->Rmask, gl->su->format->Gmask, gl->su->format->Bmask, gl->su->format->Amask);
		texture = SDL_CreateTextureFromSurface(sdl->r, ss);
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		SDL_SetTextureAlphaMod(texture, 255);
		SDL_FreeSurface(ss);
	}

	gl->texture = (Uint64)texture;
}

static void
SDL2_DrawGlyph(void *drv, AG_Glyph *gl, int x, int y)
{
	AG_DriverSDL2 *sdl = drv;
	SDL_Texture *texture = NULL;
	SDL_Rect dr = { 0, 0, 0, 0 };

	if (gl->su->w == 0 || gl->su->h == 0) return ;

	if (x < 0) x = 0;
	if (y < 0) y = 0;

	dr.x = x;
	dr.y = y;
	dr.w = gl->su->w;
	dr.h = gl->su->h;

	texture = (SDL_Texture*)gl->texture;

	if (texture == NULL) return;

	SDL_RenderCopy(sdl->r, texture, NULL, &dr);
}

/*
 * Multiple-display specific operations.
 */

/* Initialize the clipping rectangle stack. */
static int
InitClipRects(AG_DriverSDL2 *sdl, int wView, int hView)
{
	AG_ClipRect *cr;

	/* Rectangle 0 always covers the whole view. */
	if ((sdl->clipRects = TryMalloc(sizeof(AG_ClipRect))) == NULL) {
		return (-1);
	}
	cr = &sdl->clipRects[0];
	cr->r = AG_RECT(0, 0, wView, hView);
	sdl->nClipRects = 1;
	return (0);
}

static int
SDL2_OpenWindow(AG_Window *win, AG_Rect r, int depthReq, Uint mwFlags)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;
	AG_Driver *drv = WIDGET(win)->drv;
	Uint32 winflags = 0;
	int depth;

	/* For some reason Flags are never passed */
	
	if (mwFlags == 0) mwFlags = win->flags;

	if (win->transientFor == NULL)
	{
		//fprintf(stderr, "OpenWindow Input Grabbed %i\n", mwFlags&1);
		winflags |= mwFlags & AG_WINDOW_MODAL ? SDL_WINDOW_INPUT_GRABBED : 0;
	}

	winflags |= mwFlags & AG_WINDOW_MAXIMIZED ? SDL_WINDOW_MAXIMIZED : 0;
	winflags |= mwFlags & AG_WINDOW_MINIMIZED ? SDL_WINDOW_MINIMIZED : 0;
	winflags |= mwFlags & AG_WINDOW_KEEPABOVE ? SDL_WINDOW_ALWAYS_ON_TOP : 0;
	winflags |= mwFlags & AG_WINDOW_NOBORDERS ? SDL_WINDOW_BORDERLESS : 0;
	winflags |= mwFlags & AG_WINDOW_NOHRESIZE ? 0 : SDL_WINDOW_RESIZABLE;
	winflags |= mwFlags & AG_WINDOW_NOVRESIZE ? 0 : SDL_WINDOW_RESIZABLE;

	sdl->mwflags = mwFlags; /* some of the flags that can't be used now are useful later so record the flags */

	sdl->w = SDL_CreateWindow("AGAR:Untitled", r.x, r.y, r.w, r.h, winflags | SDL_WINDOW_HIDDEN);
	sdl->r = SDL_CreateRenderer(sdl->w, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
	sdl->f = SDL_GetWindowPixelFormat(sdl->w); /* The pixel format will be checked alot so... */
	sdl->pf = SDL_AllocFormat(sdl->f); /* The pixel format will be checked alot so... */
	sdl->wid = SDL_GetWindowID(sdl->w); /* SDL Events return a Window ID. Used in the Event Manager to locate this window. */

	/* Honor AG_WindowMakeTransient() */
	if (win->transientFor != NULL)
	{
		SDL2_SetTransientFor(win->transientFor, win);
	}

	/* I'm not sure what the depthReq is used for but SDL2 makes that irrelevant 
		for now just check the window's surface depth and report on mismatches */
	
	depth = (depthReq >= 1) ? depthReq : sdl->pf->BitsPerPixel;

	if (sdl->pf->BitsPerPixel != depth) 
	{
		AG_SetError("Default Window Depth Mismatch.  Required %i, Window %i\n", depthReq, sdl->pf->BitsPerPixel);
		return (-1);
	}

	/* Initialize clipping rectangles. */
	if (InitClipRects(sdl, r.w, r.h) == -1)
	{
		return (-1);
	}

	/* Update agar's idea of the actual window coordinates */
	AG_SizeAlloc a;

	a.x = r.x;
	a.y = r.y;
	a.w = r.w;
	a.h = r.h;

	AG_WidgetSizeAlloc(win, &a);
	AG_WidgetUpdateCoords(win, a.x, a.y);

	return (0);
}

static void
SDL2_CloseWindow(AG_Window *win)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;
	AG_Driver *drv = WIDGET(win)->drv;

	/* Release allocated cursors. */
	AG_FreeCursors(drv);

	if(sdl->pf != NULL)
	{
		SDL_FreeFormat(sdl->pf);
		sdl->pf = NULL;
	}

	if (sdl->r != NULL)
	{
		SDL_DestroyRenderer(sdl->r);
		sdl->r = NULL;
	}

	if (sdl->w != NULL)
	{
		SDL_SetWindowGrab(sdl->w, SDL_FALSE);
		SDL_DestroyWindow(sdl->w);
		sdl->w = NULL;
	}
}

static int
SDL2_MapWindow(AG_Window *win)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;
	AG_Driver *drv = WIDGET(win)->drv;
	int w, h;

	if (sdl->w != NULL)
	{
		SDL_ShowWindow(sdl->w);
		SDL_RaiseWindow(sdl->w);
		SDL_SetWindowInputFocus(sdl->w);
	}

	return 0;
}

static int
SDL2_UnmapWindow(AG_Window *win)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;
	AG_Driver *drv = WIDGET(win)->drv;

	if (sdl->w != NULL)
	{
		SDL_HideWindow(sdl->w);
	}

	return 0;
}

static int
SDL2_RaiseWindow(AG_Window *win)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;
	AG_Driver *drv = WIDGET(win)->drv;

	if (sdl->w != NULL)
	{
		SDL_RaiseWindow(sdl->w);
	}

	return 0;
}

static int
SDL2_LowerWindow(AG_Window *win)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;
	AG_Driver *drv = WIDGET(win)->drv;

	if (sdl->w != NULL)
	{
		AG_Debug(NULL, "AG SDL Window Lowering is not supported\n");
	}

	return 0;
}

static int
SDL2_ReparentWindow(AG_Window *win, AG_Window *winparent, int x, int y)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;
	AG_Driver *drv = WIDGET(win)->drv;
	AG_DriverSDL2 *sdlparent = (AG_DriverSDL2 *)WIDGET(winparent)->drv;
	AG_Driver *drvparent = WIDGET(winparent)->drv;

	/* Not sure if setting the window modal to another has the same intention as AGAR Reparenting requires */

	if (sdl->w != NULL && sdlparent->w != NULL)
	{
		SDL_SetWindowModalFor(sdl->w, sdlparent->w); 
		//AG_Debug(NULL, "AG SDL Window Reparent %s to %s\n", SDL_GetWindowTitle(sdl->w), SDL_GetWindowTitle(sdlparent->w));
		//fprintf(stderr, "ReparentWindow Setting Modal : %s\n", win->caption);
	}

	return 0;
}


static int
SDL2_GetInputFocus(AG_Window **rv)
{
	AG_DriverSDL2 *sdl = NULL;
	SDL_Window *focus = SDL_GetMouseFocus();

	AGOBJECT_FOREACH_CHILD(sdl, &agDrivers, ag_sdl2_driver) 
	{
		if (AGDRIVER_IS_SDL2(sdl)) 
		{
			if (sdl->w == focus)
			{
				break;
			}
		}
	}

	if (sdl == NULL) 
	{
		AG_SetError("Input focus is external to this application");
		return (-1);
	}

	*rv = AGDRIVER_MW(sdl)->win;

	return (0);
}

static int
SDL2_SetInputFocus(AG_Window *win)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;

	if (sdl->w != NULL)
	{
		SDL_SetWindowInputFocus(sdl->w);
	}

	return (0);
}

static int
SDL2_MoveWindow(AG_Window *win, int x, int y)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;

	if (sdl->w != NULL)
	{
		SDL_SetWindowPosition(sdl->w, x, y);
	}

	return (0);
}

static int
SDL2_ResizeWindow(AG_Window *win, Uint w, Uint h)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;

	if (sdl->w != NULL)
	{
		SDL_SetWindowSize(sdl->w, w, h);
	}

	return (0);
}

static int
SDL2_MoveResizeWindow(struct ag_window *win, struct ag_size_alloc *a)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;

	if (sdl->w != NULL)
	{
		SDL_SetWindowPosition(sdl->w, a->x, a->y);
		SDL_SetWindowSize(sdl->w, a->w, a->h);
	}

	return (0);
}

static int
SDL2_SetBorderWidth(AG_Window *win, Uint width)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;
	
	if (sdl->w != NULL)
	{
		SDL_SetWindowBordered(sdl->w, width > 0);
	}
	
	return (0);
}


static int
SDL2_SetWindowCaption(AG_Window *win, const char *s)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;

	if (sdl->w != NULL)
	{
		SDL_SetWindowTitle(sdl->w, s);
	}
	
	return (0);
}

static void
SDL2_SetTransientFor(AG_Window *win, AG_Window *winparent)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;
	AG_Driver *drv = WIDGET(win)->drv;
	AG_DriverSDL2 *sdlparent = (AG_DriverSDL2 *)WIDGET(winparent)->drv;
	AG_Driver *drvparent = WIDGET(winparent)->drv;
	AG_DriverMw *dmwparent = AGDRIVER_MW(drvparent);

	/* Not sure if setting the window modal to another has the same intention as AGAR Reparenting requires */
	
	if (winparent != NULL &&
	    sdlparent != NULL &&
	    AGDRIVER_IS_SDL2(sdlparent) &&
	    (dmwparent->flags & AG_DRIVER_MW_OPEN)) {
		SDL_SetWindowBordered(sdl->w, 0);
		SDL_SetWindowModalFor(sdl->w, sdlparent->w); 
		//fprintf(stderr, "SetTransientFor Setting Modal : %s\n", win->caption);
	} else {
		// SDL_SetWindowModalFor(sdl->w, NULL); 
	}
}

static int
SDL2_SetOpacity(AG_Window *win, float f)
{
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)WIDGET(win)->drv;
	AG_Driver *drv = WIDGET(win)->drv;

	if (sdl->w != NULL)
	{
		SDL_SetWindowOpacity(sdl->w, f);
	}

	return 0;
}

static void
SDL2_TweakAlignment(AG_Window *win, AG_SizeAlloc *a, Uint wMax, Uint hMax)
{
	/* XXX TODO */
	switch (win->alignment) {
	case AG_WINDOW_TL:
	case AG_WINDOW_TC:
	case AG_WINDOW_TR:
		a->y += 50;
		if (a->y+a->h > hMax) { a->y = 0; }
		break;
	case AG_WINDOW_BL:
	case AG_WINDOW_BC:
	case AG_WINDOW_BR:
		a->y -= 100;
		if (a->y < 0) { a->y = 0; }
		break;
	default:
		break;
	}
}

static void
SDL2_PreResizeCallback(AG_Window *win)
{
}

static void
SDL2_PostResizeCallback(AG_Window *win, AG_SizeAlloc *a)
{
	AG_Driver *drv = WIDGET(win)->drv;
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)drv;
	AG_SizeAlloc aNew;
	
	/* Update the window size and coordinates. */
	aNew.x = 0;
	aNew.y = 0;
	aNew.w = a->w;
	aNew.h = a->h;
	//printf("Postresize %i %i %i %i\n", a->x, a->y, a->w, a->h);
	AG_WidgetSizeAlloc(win, &aNew);
	AG_WidgetUpdateCoords(win, 0, 0);
	WIDGET(win)->x = a->x;
	WIDGET(win)->y = a->y;
	win->dirty = 1;
}

static void
SDL2_PostMoveCallback(AG_Window *win, AG_SizeAlloc *a)
{
	AG_Driver *drv = WIDGET(win)->drv;
	AG_DriverSDL2 *sdl = (AG_DriverSDL2 *)drv;
	AG_SizeAlloc aNew;
	int xRel, yRel;
	
	xRel = a->x - WIDGET(win)->x;
	yRel = a->y - WIDGET(win)->y;

	// /* Update the window coordinates. */
	// aNew.x = 0;
	// aNew.y = 0;
	// aNew.w = a->w;
	// aNew.h = a->h;
	// AG_WidgetSizeAlloc(win, &aNew);
	// AG_WidgetUpdateCoords(win, 0, 0);
	WIDGET(win)->x = a->x;
	WIDGET(win)->y = a->y;
	win->dirty = 1;

	/* Move other windows pinned to this one. */
	AG_WindowMovePinned(win, xRel, yRel);
}



AG_DriverMwClass agDriverSDL2 = {
	{
		{
			"AG_Driver:AG_DriverMw:AG_DriverSDL2",
			sizeof(AG_DriverSDL2),
			{ 1,5 },
			Init,
			NULL,	/* reinit */
			Destroy,
			NULL,	/* load */
			NULL,	/* save */
			NULL,	/* edit */
		},
		"sdl2",
		AG_FRAMEBUFFER,
		AG_WM_MULTIPLE,
		AG_DRIVER_SDL|AG_DRIVER_TEXTURES,
		SDL2_Open,
		SDL2_Close,
		AG_SDL2_GetDisplaySize,
		AG_SDL2_BeginEventProcessing,
		AG_SDL2_PendingEvents,
		AG_SDL2_GetNextEvent,
		AG_SDL2_ProcessEvent,
		NULL,				/* genericEventLoop */
		NULL,				/* endEventProcessing */
		NULL,				/* terminate */
		SDL2_BeginRendering,
		SDL2_RenderWindow,
		SDL2_EndRendering,
		SDL2_FillRect,
		NULL,				/* updateRegion */
		SDL2_UploadTexture,				/* uploadTexture */
		SDL2_UpdateTexture,				/* updateTexture */
		SDL2_DeleteTexture,				/* deleteTexture */
		NULL,				/* AG_SDL2_SetRefreshRate, */
		SDL2_PushClipRect,
		SDL2_PopClipRect,
		SDL2_PushBlendingMode,
		SDL2_PopBlendingMode,
		AG_SDL2_CreateCursor,
		AG_SDL2_FreeCursor,
		AG_SDL2_SetCursor,
		AG_SDL2_UnsetCursor,
		AG_SDL2_GetCursorVisibility,
		AG_SDL2_SetCursorVisibility,
		SDL2_BlitSurface,
		SDL2_BlitSurfaceFrom,
		SDL2_BlitSurfaceGL,
		SDL2_BlitSurfaceFromGL,
		SDL2_BlitSurfaceFlippedGL,
		NULL,				/* backupSurfaces */
		NULL,				/* restoreSurfaces */
		SDL2_RenderToSurface,
		SDL2_PutPixel,
		SDL2_PutPixel32,
		SDL2_PutPixelRGB,
		SDL2_BlendPixel,
		SDL2_DrawLine,
		SDL2_DrawLineH,
		SDL2_DrawLineV,
		SDL2_DrawLineBlended,
		SDL2_DrawArrowUp,
		SDL2_DrawArrowDown,
		SDL2_DrawArrowLeft,
		SDL2_DrawArrowRight,
		SDL2_DrawBoxRounded,
		SDL2_DrawBoxRoundedTop,
		SDL2_DrawCircle,
		SDL2_DrawCircleFilled,
		SDL2_DrawRectFilled,
		SDL2_DrawRectBlended,
		SDL2_DrawRectDithered,
		SDL2_UpdateGlyph,
		SDL2_DrawGlyph,
		NULL				/* deleteList */
	},
	SDL2_OpenWindow,
	SDL2_CloseWindow,
	SDL2_MapWindow,
	SDL2_UnmapWindow,
	SDL2_RaiseWindow,
	SDL2_LowerWindow,
	SDL2_ReparentWindow,
	SDL2_GetInputFocus,
	SDL2_SetInputFocus,
	SDL2_MoveWindow,
	SDL2_ResizeWindow,
	SDL2_MoveResizeWindow,
	SDL2_PreResizeCallback,
	SDL2_PostResizeCallback,
	NULL,				/* captureWindow */
	SDL2_SetBorderWidth,
	SDL2_SetWindowCaption,
	SDL2_SetTransientFor,
	SDL2_SetOpacity,
	SDL2_TweakAlignment
};
