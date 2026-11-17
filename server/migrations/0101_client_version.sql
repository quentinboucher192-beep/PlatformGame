-- 2.51 : version du jeu déclarée par le client (en-tête X-Cosmic-Client, ou son User-Agent),
-- affichée aux administrateurs dans l'onglet Joueurs. Information déclarative : elle n'autorise
-- jamais rien et n'est montrée à personne d'autre.
ALTER TABLE players ADD COLUMN client_version TEXT;
ALTER TABLE players ADD COLUMN client_version_at TEXT;
