#pragma once

#include "engine/core/Math.h"

namespace cosmic {

class GameServices;

/// Sections of ONLINE, reached from the navigation column shared by its screens.
enum class OnlineSection { Ranking, Social, Workshop };

/// Draws the ONLINE navigation (ONLINE RANKING, SOCIAL, WORKSHOP, RETOUR) at the top of
/// `area` and switches screen when an item is chosen. Returns the height used.
float drawOnlineNav(GameServices& g, const Rect& area, OnlineSection current);

/// Badge count of the SOCIAL item: unread messages + friend requests.
int onlineSocialBadge(GameServices& g);

}  // namespace cosmic
