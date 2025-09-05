#ifdef MESHTASTIC_INCLUDE_INKHUD

#pragma once

#include "configuration.h"

namespace NicheGraphics::InkHUD {

    class Controllable {
    public:
        enum class Types {
            Uncontrollable,
            ThreadedMessage,
        };
        Controllable(const Types type);
        virtual ~Controllable();
        virtual bool handleUp();
        virtual bool handleDown();
        virtual bool handleEnter();
        virtual bool handleBack();
        static Types checkControllable(const void* ptr);
    protected:
        static inline std::unordered_map<const void*, Types> instances;
    };

} // namespace NicheGraphics::InkHUD

#endif