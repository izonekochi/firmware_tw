#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "Controllable.h"

namespace NicheGraphics::InkHUD {

    std::unordered_map<const void*, Controllable::Types> Controllable::instances;
    
    bool Controllable::handleUp() {
        return false;
    }
    
    bool Controllable::handleDown() {
        return false;
    }
    
    bool Controllable::handleEnter() {
        return false;
    }
    
    bool Controllable::handleBack() {
        return false;
    }
    
    void Controllable::registerControllable(const void* ptr, const Types type) {
        instances[ptr] = type;
    }
    
    void Controllable::unregisterControllable(const void* ptr) {
        instances.erase(ptr);
    }


    Controllable::Types Controllable::checkControllable(const void* ptr) {
        return instances.find(ptr) == instances.cend() ? Types::Uncontrollable : instances[ptr];
    }

} // namespace NicheGraphics::InkHUD

#endif