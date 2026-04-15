#include "Log.h"
#define SOKOL_LOG(s) { SOKOL_ASSERT(s); doriax::Log::error(s); }

#define SOKOL_IMPL

#if defined(DORIAX_ANDROID)
#define SOKOL_EXTERNAL_GL_LOADER
#include <GLES3/gl3.h>
#endif

#if defined(_WIN32)
#undef SOKOL_LOG
#define SOKOL_LOG(s) OutputDebugStringA(s)
#endif
/* this is only needed for the debug-inspection headers */
#define SOKOL_TRACE_HOOKS
/* sokol 3D-API defines are provided by build options */

#ifdef DORIAX_SOKOL
    #include "sokol_app.h"
#endif

#include "sokol_gfx.h"
#include "sokol_time.h"
//#include "sokol_audio.h"
//#include "sokol_log.h"

#ifdef DORIAX_SOKOL
    #include "sokol_fetch.h"
    #include "sokol_glue.h"
#endif
