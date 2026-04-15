#ifndef DORIAX_EXPORT_H
#define DORIAX_EXPORT_H

#if defined(_MSC_VER)
    #ifdef DORIAX_SHARED
        #ifdef DORIAX_EXPORTS
            #define DORIAX_API __declspec(dllexport)
        #else
            #define DORIAX_API __declspec(dllimport)
        #endif
    #else
        #define DORIAX_API
    #endif
#else
    #define DORIAX_API
#endif

#endif // DORIAX_EXPORT_H