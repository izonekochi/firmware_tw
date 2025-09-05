#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "Controllable.h"

namespace NicheGraphics::InkHUD {

    Controllable::Controllable(const Types type) {
        instances[this] = type;
    }

    Controllable::~Controllable() {
        instances.erase(this);
    }
    
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
    
    Controllable::Types Controllable::checkControllable(const void* ptr) {
        return instances.find(ptr) == instances.cend() ? Types::Uncontrollable : instances[ptr];
    }

} // namespace NicheGraphics::InkHUD

#endif