#pragma once

#include "engine/core/Math.h"

namespace cosmic {

class GameServices;

/// Sections of ONLINE, reached from the navigation column shared by its screens.
enum class OnlineSection { Levels, Ranking, Social, Clans, Gifts, Projects, Workshop };

/// Draws the ONLINE navigation (ONLINE LEVELS, ONLINE RANKING, SOCIAL, CLANS, CADEAUX once a gift was received,
/// PROJETS, WORKSHOP, RETOUR) at the top of
/// `area` and switches screen when an item is chosen. Returns the height used.
float drawOnlineNav(GameServices& g, const Rect& area, OnlineSection current);

/// 2.84: « COSMIC ONLINE » in neon (Cosmic kit) at the left of a header, its left end and centre
/// line at `leftMid`, at most `maxW` wide; returns its size.
Vec2 drawOnlineTitle(GameServices& g, Vec2 leftMid, float maxW = 1e9f);

/// Badge count of the SOCIAL item: unread messages + friend requests.
int onlineSocialBadge(GameServices& g);

}  // namespace cosmic
