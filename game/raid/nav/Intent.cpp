#include "game/raid/nav/Intent.h"

namespace cosmic::raid::nav {

const char* intentText(Intent i) {
    switch (i) {
        case Intent::None: return "Attend un objectif";
        case Intent::Observe: return "Observe la zone";
        case Intent::Walk: return "Avance vers son objectif";
        case Intent::Wait: return "Attend à son poste";
        case Intent::Climb: return "Grimpe à l'échelle";
        case Intent::Drop: return "Se laisse tomber plus bas";
        case Intent::DropThrough: return "Traverse la plateforme vers le bas";
        case Intent::Jump: return "Saute vers la plateforme suivante";
        case Intent::JumpMover: return "Saut vers la plateforme mobile";
        case Intent::WaitMover: return "Attend la plateforme mobile";
        case Intent::Ride: return "Se laisse porter par la plateforme mobile";
        case Intent::DoubleJump: return "Double saut";
        case Intent::DoubleJumpVoid: return "Double saut au-dessus du vide";
        case Intent::TripleJump: return "Triple saut";
        case Intent::Dash: return "Ruée aérienne";
        case Intent::DashVoid: return "Ruée au-dessus du vide";
        case Intent::LongJump: return "Ruée puis saut allongé";
        case Intent::Impulse: return "Impulsion vers le haut";
        case Intent::WallJump: return "Saut mural";
        case Intent::Recover: return "Se rattrape en l'air";
        case Intent::DodgeShot: return "Esquive du tir";
        case Intent::DodgeBeam: return "Esquive du rayon";
        case Intent::DodgeBlow: return "Esquive du coup annoncé";
        case Intent::Duck: return "S'accroupit sous le coup";
        case Intent::FleeLiquid: return "Remonte au-dessus du magma";
        case Intent::Shelter: return "Se met à l'abri en hauteur";
        case Intent::AvoidHazard: return "Évite le danger";
        case Intent::Stuck: return "Cherche un autre chemin";
        case Intent::Count: break;
    }
    return "";
}

}  // namespace cosmic::raid::nav
