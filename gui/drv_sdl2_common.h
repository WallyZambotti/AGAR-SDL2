/*	Public domain	*/
/*
 * Code common to all drivers using the SDL library.
 */

#include <agar/config/have_sdl2.h>
#ifdef HAVE_SDL2

/* XXX */
#undef HAVE_SNPRINTF
#undef HAVE_VSNPRINTF
#undef HAVE_SYS_TYPES_H
#undef HAVE_STDIO_H
#undef HAVE_STDLIB_H
#undef HAVE_STDARG_H
#undef Uint8
#undef Sint8
#undef Uint16
#undef Sint16
#undef Uint32
#undef Sint32
#undef Uint64
#undef Sint64

#ifdef _USE_SDL_FRAMEWORK
# include <SDL2/SDL.h>
# ifdef main
#  undef main
# endif
#else
# include <SDL.h>
#endif

#include <agar/gui/begin.h>

__BEGIN_DECLS
AG_PixelFormat *AG_SDL2_GetPixelFormat(SDL_Surface *);
AG_Surface     *AG_SDL2_ImportSurface(SDL_Surface *);

int             AG_SDL2_SetRefreshRate(void *, int);

int             AG_SDL2_InitDefaultCursor(void *);
int             AG_SDL2_SetCursor(void *, AG_Cursor *);
void            AG_SDL2_UnsetCursor(void *);
AG_Cursor      *AG_SDL2_CreateCursor(void *, Uint, Uint, const Uint8 *, const Uint8 *, int, int);
void            AG_SDL2_FreeCursor(void *, AG_Cursor *);
int             AG_SDL2_GetCursorVisibility(void *);
void            AG_SDL2_SetCursorVisibility(void *, int);

int             AG_SDL2_GetDisplaySize(Uint *, Uint *);
void            AG_SDL2_GetPrefDisplaySettings(void *, Uint *, Uint *, int *);
void            AG_SDL2_BeginEventProcessing(void *);
int             AG_SDL2_PendingEvents(void *);
void            AG_SDL2_TranslateEvent(void *, const SDL_Event *, AG_DriverEvent *);
int             AG_SDL2_GetNextEvent(void *, AG_DriverEvent *);
int             AG_SDL2_ProcessEvent(void *, AG_DriverEvent *);
int             AG_SDL2_EventSink(AG_EventSink *, AG_Event *);
int             AG_SDL2_EventEpilogue(AG_EventSink *, AG_Event *);
void            AG_SDL2_EndEventProcessing(void *);
__END_DECLS

#include <agar/gui/close.h>
#endif /* HAVE_SDL2 */
