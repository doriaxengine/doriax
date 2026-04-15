#ifndef DoriaxSokol_h
#define DoriaxSokol_h

#include "System.h"

class DoriaxSokol: public doriax::System{

public:

    DoriaxSokol();

    virtual int getScreenWidth();
    virtual int getScreenHeight();

    virtual sg_environment getSokolEnvironment();
    virtual sg_swapchain getSokolSwapchain();

    virtual bool isFullscreen();
    virtual void requestFullscreen();
    virtual void exitFullscreen();

    virtual void setMouseCursor(doriax::CursorType type);
    virtual void setShowCursor(bool showCursor);

    virtual std::string getAssetPath();
    virtual std::string getUserDataPath();
    virtual std::string getLuaPath();
    
};

#endif /* DoriaxSokol_h */