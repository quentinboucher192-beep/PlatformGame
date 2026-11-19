#pragma once

// 2.75.3: what an administrator sees through the privacy locks of a profile (moderation). The
// server alone decides (the role of the session, never a parameter sent by the game) and says
// which lock it lifted (`admin_view.locks`, the headers of a replay's download); these helpers
// only turn its words into the game's: the discreet marks « Vu en tant qu'admin », the status of
// a record or a replay that is not verified (never counted as a verified record), the Journal's
// lines. Nothing here grants anything: an account that is not an administrator never receives
// these fields.

#include <string>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic::adminview {

/// Words of a lock the server names (« profile_private » → « profil masqué ») ; "" for one this
/// game does not know (a newer server): never shown as a raw key.
std::string lockLabel(const std::string& lock);

/// The known locks of a JSON array (or of a single string), in their words, in order, without
/// duplicates.
std::vector<std::string> lockLabels(const Json& locks);

/// « profil masqué · présence masquée » ("" without any).
std::string joinLabels(const std::vector<std::string>& labels);

/// A record's or a replay's status: « vérifié », « en attente de vérification », « refusé »
/// ("" for an unknown one).
std::string statusLabel(const std::string& status);

/// The short form of a chip in a list: « vérifié », « en attente », « refusé ».
std::string statusChip(const std::string& status);

/// Only a VERIFIED record or replay counts as verified: never PENDING, REJECTED or unknown.
bool countsAsVerified(const std::string& status);

/// The mark shown while a replay plays: « Vu en tant qu'admin (profil masqué · en attente de
/// vérification) », from the download's headers `x-admin-view` (the privacy lock lifted) and
/// `x-admin-replay-status` (a status that is not verified) ; "" when neither is there (an
/// ordinary replay, or an old server).
std::string replayNote(const std::string& lockHeader, const std::string& statusHeader);

/// The same mark from a record's `admin_view` object (GET /records/{id}) ; "" without one.
std::string recordNote(const Json& adminView);

/// « 3 consultations, la dernière le 16/11 · 02:14 » for a grouped Journal entry (`count`; the
/// last one's date already written by the screen, in local time) ; "" for a single one.
std::string groupedText(int count, const std::string& lastStamp);

/// The country line of the administrator's view: a country the player hides stays hidden, even
/// from the administrators (the player's consent) ; only the setting is told.
std::string locationSetting(const std::string& visibility);

}  // namespace cosmic::adminview
