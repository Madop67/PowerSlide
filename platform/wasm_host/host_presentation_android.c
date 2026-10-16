// Android second display for the Dual Screen tab. See host_presentation_android.h.
//
// PresentationBridge.java (UI thread) finds the display, shows a Presentation with
// a SurfaceView on it when the guest opens its second window, and passes the
// surface and taps here. The game thread draws into that surface with an EGL
// context shared with SDL's, as the desktop builds do with a second SDL window.
//
// Threads: the UI thread sets the display and surface; the game thread creates
// the EGL surface and draws. s_lock covers the shared state. The UI thread only
// destroys the EGL surface (EGL defers that while the surface is current), so it
// never waits for the game thread, which may be blocked while the app is paused.

#if defined(__ANDROID__)

#include "host_presentation_android.h"

#include <EGL/egl.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
#include <string.h>

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

// ---- shared with the UI thread (s_lock) ----
static bool s_hasDisplay;
static char s_displayName[128];
static int s_displayWidth;
static int s_displayHeight;
static ANativeWindow *s_nativeWindow;
static int s_surfaceWidth;
static int s_surfaceHeight;
static EGLSurface s_surface = EGL_NO_SURFACE;
// Set with s_context; read by the UI thread to destroy s_surface.
static EGLDisplay s_eglDisplay = EGL_NO_DISPLAY;

// ---- game thread ----
static jclass s_bridgeClass;
static char s_window; // the stand-in window; only its address is used
static bool s_windowOpen;
static EGLConfig s_config;
static EGLContext s_context = EGL_NO_CONTEXT;
static bool s_current;
static int s_swapInterval = 1;
static EGLSurface s_intervalSurface = EGL_NO_SURFACE;

// ---- display ------------------------------------------------------------------

bool HostPresentation_HasDisplay(void)
{
	pthread_mutex_lock(&s_lock);
	const bool has = s_hasDisplay;
	pthread_mutex_unlock(&s_lock);
	return has;
}

const char *HostPresentation_DisplayName(void)
{
	static char s_name[sizeof(s_displayName)];
	pthread_mutex_lock(&s_lock);
	const bool has = s_hasDisplay;
	memcpy(s_name, s_displayName, sizeof(s_name));
	pthread_mutex_unlock(&s_lock);
	return has ? s_name : NULL;
}

bool HostPresentation_DisplayBounds(SDL_Rect *rect)
{
	pthread_mutex_lock(&s_lock);
	const bool has = s_hasDisplay;
	rect->x = 0;
	rect->y = 0;
	rect->w = s_displayWidth;
	rect->h = s_displayHeight;
	pthread_mutex_unlock(&s_lock);
	return has;
}

// ---- Java -> native (UI thread) ---------------------------------------------------

static void HostPresentation_PushDisplayEvent(SDL_EventType type)
{
	// NOTE: native_second_screen.c reopens or closes its window on these.
	SDL_Event event;
	SDL_zero(event);
	event.type = type;
	event.display.timestamp = SDL_GetTicksNS();
	event.display.displayID = HOST_PRESENTATION_DISPLAY_ID;
	SDL_PushEvent(&event);
}

// Caller holds s_lock.
static void HostPresentation_DestroySurfaceLocked(void)
{
	if (s_surface != EGL_NO_SURFACE)
	{
		eglDestroySurface(s_eglDisplay, s_surface);
		s_surface = EGL_NO_SURFACE;
	}
}

JNIEXPORT void JNICALL Java_io_github_madop67_powerslide_PresentationBridge_nativeDisplayChanged(JNIEnv *env, jclass cls, jboolean present,
                                                                                                 jstring name, jint width, jint height)
{
	pthread_mutex_lock(&s_lock);
	if (s_bridgeClass == NULL)
	{
		s_bridgeClass = (jclass)(*env)->NewGlobalRef(env, cls);
	}

	char displayName[sizeof(s_displayName)] = "";
	if (present && (name != NULL))
	{
		const char *utf = (*env)->GetStringUTFChars(env, name, NULL);
		if (utf != NULL)
		{
			SDL_strlcpy(displayName, utf, sizeof(displayName));
			(*env)->ReleaseStringUTFChars(env, name, utf);
		}
	}

	// NOTE: Java reports every change of the display; only some matter here.
	const bool changed = (s_hasDisplay != (bool)present) || (s_displayWidth != width) || (s_displayHeight != height) ||
	                     (strcmp(s_displayName, displayName) != 0);
	s_hasDisplay = present;
	s_displayWidth = width;
	s_displayHeight = height;
	memcpy(s_displayName, displayName, sizeof(s_displayName));
	pthread_mutex_unlock(&s_lock);

	if (changed)
	{
		SDL_Log("[CTR Native] Second display: %s (%dx%d)", present ? displayName : "none", (int)width, (int)height);
		HostPresentation_PushDisplayEvent(present ? SDL_EVENT_DISPLAY_ADDED : SDL_EVENT_DISPLAY_REMOVED);
	}
}

JNIEXPORT void JNICALL Java_io_github_madop67_powerslide_PresentationBridge_nativeSurfaceChanged(JNIEnv *env, jclass cls, jobject surface, jint width,
                                                                                                 jint height)
{
	(void)cls;
	ANativeWindow *window = ANativeWindow_fromSurface(env, surface);

	pthread_mutex_lock(&s_lock);
	if (window != s_nativeWindow)
	{
		HostPresentation_DestroySurfaceLocked();
		if (s_nativeWindow != NULL)
		{
			ANativeWindow_release(s_nativeWindow);
		}
		s_nativeWindow = window;
	}
	else if (window != NULL)
	{
		ANativeWindow_release(window);
	}
	s_surfaceWidth = width;
	s_surfaceHeight = height;
	pthread_mutex_unlock(&s_lock);
}

JNIEXPORT void JNICALL Java_io_github_madop67_powerslide_PresentationBridge_nativeSurfaceDestroyed(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	pthread_mutex_lock(&s_lock);
	HostPresentation_DestroySurfaceLocked();
	if (s_nativeWindow != NULL)
	{
		ANativeWindow_release(s_nativeWindow);
		s_nativeWindow = NULL;
	}
	s_surfaceWidth = 0;
	s_surfaceHeight = 0;
	pthread_mutex_unlock(&s_lock);
}

JNIEXPORT void JNICALL Java_io_github_madop67_powerslide_PresentationBridge_nativeTouch(JNIEnv *env, jclass cls, jint action, jfloat x, jfloat y)
{
	(void)env;
	(void)cls;
	// One finger as the left mouse button, on the stand-in window (what
	// native_second_screen.c routes to its UI).
	SDL_Event event;
	SDL_zero(event);
	if (action == 1)
	{
		event.type = SDL_EVENT_MOUSE_MOTION;
		event.motion.timestamp = SDL_GetTicksNS();
		event.motion.windowID = HOST_PRESENTATION_WINDOW_ID;
		event.motion.which = SDL_TOUCH_MOUSEID;
		event.motion.state = SDL_BUTTON_LMASK;
		event.motion.x = x;
		event.motion.y = y;
	}
	else
	{
		event.type = (action == 0) ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
		event.button.timestamp = SDL_GetTicksNS();
		event.button.windowID = HOST_PRESENTATION_WINDOW_ID;
		event.button.which = SDL_TOUCH_MOUSEID;
		event.button.button = SDL_BUTTON_LEFT;
		event.button.down = (action == 0);
		event.button.clicks = 1;
		event.button.x = x;
		event.button.y = y;
	}
	SDL_PushEvent(&event);
}

// ---- native -> Java (game thread) --------------------------------------------------

static bool HostPresentation_CallBridge(const char *method)
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	pthread_mutex_lock(&s_lock);
	jclass cls = s_bridgeClass;
	pthread_mutex_unlock(&s_lock);
	if ((env == NULL) || (cls == NULL))
	{
		return false;
	}

	jmethodID id = (*env)->GetStaticMethodID(env, cls, method, "()V");
	if (id == NULL)
	{
		(*env)->ExceptionClear(env);
		return false;
	}
	(*env)->CallStaticVoidMethod(env, cls, id);
	if ((*env)->ExceptionCheck(env))
	{
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		return false;
	}
	return true;
}

// ---- window ---------------------------------------------------------------------

bool HostPresentation_IsWindow(const void *ptr)
{
	return (ptr != NULL) && (ptr == &s_window);
}

void *HostPresentation_CreateWindow(void)
{
	if (!HostPresentation_HasDisplay())
	{
		SDL_SetError("No second display");
		return NULL;
	}
	if (!HostPresentation_CallBridge("open"))
	{
		SDL_SetError("PresentationBridge is not available");
		return NULL;
	}
	s_windowOpen = true;
	return &s_window;
}

void HostPresentation_DestroyWindow(void)
{
	if (s_windowOpen)
	{
		HostPresentation_CallBridge("close");
		s_windowOpen = false;
	}
}

void HostPresentation_GetSize(int *width, int *height)
{
	pthread_mutex_lock(&s_lock);
	*width = s_surfaceWidth;
	*height = s_surfaceHeight;
	pthread_mutex_unlock(&s_lock);
}

// ---- GL context -------------------------------------------------------------------

bool HostPresentation_IsContext(const void *ptr)
{
	return (ptr != NULL) && (s_context != EGL_NO_CONTEXT) && (ptr == (const void *)s_context);
}

void *HostPresentation_CreateContext(void)
{
	// NOTE: The guest asks for a context shared with the current one, SDL's.
	EGLDisplay display = eglGetCurrentDisplay();
	EGLContext share = eglGetCurrentContext();
	if ((display == EGL_NO_DISPLAY) || (share == EGL_NO_CONTEXT) || (s_context != EGL_NO_CONTEXT))
	{
		SDL_SetError("Second display: no current GL context to share with");
		return NULL;
	}

	// The same config as SDL's, so the shared context and the surface are compatible.
	EGLint configId = 0;
	eglQueryContext(display, share, EGL_CONFIG_ID, &configId);
	const EGLint configAttribs[] = {EGL_CONFIG_ID, configId, EGL_NONE};
	EGLint configCount = 0;
	if (!eglChooseConfig(display, configAttribs, &s_config, 1, &configCount) || (configCount < 1))
	{
		SDL_SetError("Second display: eglChooseConfig failed (0x%x)", eglGetError());
		return NULL;
	}

	const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
	EGLContext context = eglCreateContext(display, s_config, share, contextAttribs);
	if (context == EGL_NO_CONTEXT)
	{
		SDL_SetError("Second display: eglCreateContext failed (0x%x)", eglGetError());
		return NULL;
	}

	pthread_mutex_lock(&s_lock);
	s_eglDisplay = display;
	pthread_mutex_unlock(&s_lock);
	s_context = context;

	// NOTE: SDL_GL_CreateContext leaves the new context current; so does this.
	HostPresentation_MakeCurrent();
	return (void *)context;
}

bool HostPresentation_DestroyContext(void)
{
	if (s_context == EGL_NO_CONTEXT)
	{
		return false;
	}
	if (s_current)
	{
		eglMakeCurrent(s_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		s_current = false;
	}

	pthread_mutex_lock(&s_lock);
	HostPresentation_DestroySurfaceLocked();
	pthread_mutex_unlock(&s_lock);

	eglDestroyContext(s_eglDisplay, s_context);
	s_context = EGL_NO_CONTEXT;
	s_intervalSurface = EGL_NO_SURFACE;
	return true;
}

bool HostPresentation_MakeCurrent(void)
{
	if (s_context == EGL_NO_CONTEXT)
	{
		return false;
	}

	SDL_Window *sdlWindow = SDL_GL_GetCurrentWindow();
	SDL_GLContext sdlContext = SDL_GL_GetCurrentContext();

	pthread_mutex_lock(&s_lock);
	if ((s_surface == EGL_NO_SURFACE) && (s_nativeWindow != NULL))
	{
		EGLint format = 0;
		eglGetConfigAttrib(s_eglDisplay, s_config, EGL_NATIVE_VISUAL_ID, &format);
		ANativeWindow_setBuffersGeometry(s_nativeWindow, 0, 0, format);
		s_surface = eglCreateWindowSurface(s_eglDisplay, s_config, s_nativeWindow, NULL);
		if (s_surface == EGL_NO_SURFACE)
		{
			SDL_Log("[CTR Native] Second display: eglCreateWindowSurface failed (0x%x)", eglGetError());
		}
	}
	const EGLSurface surface = s_surface;

	// NOTE: Release SDL's context through SDL, so its "already current" check
	// doesn't skip the guest's switch back to the game window.
	if (sdlContext != NULL)
	{
		SDL_GL_MakeCurrent(sdlWindow, NULL);
	}
	// Without a surface (not shown yet, or gone) the context is current surfaceless,
	// so the guest can still free its GL objects in it.
	const bool ok = eglMakeCurrent(s_eglDisplay, surface, surface, s_context);
	pthread_mutex_unlock(&s_lock);

	if (!ok)
	{
		SDL_SetError("Second display: eglMakeCurrent failed (0x%x)", eglGetError());
		if (sdlContext != NULL)
		{
			SDL_GL_MakeCurrent(sdlWindow, sdlContext);
		}
		return false;
	}
	s_current = true;

	if ((surface != EGL_NO_SURFACE) && (surface != s_intervalSurface))
	{
		eglSwapInterval(s_eglDisplay, s_swapInterval);
		s_intervalSurface = surface;
	}
	return true;
}

bool HostPresentation_IsCurrent(void)
{
	return s_current;
}

void *HostPresentation_Context(void)
{
	return (s_context != EGL_NO_CONTEXT) ? (void *)s_context : NULL;
}

void HostPresentation_LostCurrent(void)
{
	s_current = false;
}

bool HostPresentation_Swap(void)
{
	if (!s_current)
	{
		return false;
	}
	// NOTE: The current surface stays valid even if the UI thread destroyed it meanwhile.
	const EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
	return (surface != EGL_NO_SURFACE) && eglSwapBuffers(s_eglDisplay, surface);
}

bool HostPresentation_SetSwapInterval(int interval)
{
	s_swapInterval = interval;
	const EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
	if (surface != EGL_NO_SURFACE)
	{
		s_intervalSurface = surface;
		return eglSwapInterval(s_eglDisplay, interval);
	}
	return true;
}

#endif
