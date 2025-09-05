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
        virtual ~Controllable() = default;
        virtual bool handleUp();
        virtual bool handleDown();
        virtual bool handleEnter();
        virtual bool handleBack();
        static void registerControllable(const void* ptr, Types type);
        static void unregisterControllable(const void* ptr);
        static Types checkControllable(const void* ptr);
    protected:
        static std::unordered_map<const void*, Types> instances;
    };

} // namespace NicheGraphics::InkHUD

#endif