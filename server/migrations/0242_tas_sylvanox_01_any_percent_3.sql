-- TAS published: Les Ruines Suspendues (sylvanox_01), Any%, revision 3: 1:18.483 (78483 ms), simulation 21,
-- level version 8 (fingerprint 49d7d22c…). Found by the TAS AI (trajectories, double-jump branch) and checked
-- like POST /admin/tas/…/publish does (an exact replay of this level, fingerprint and simulation; same time
-- as its record; not a team or a challenge) and by the game's own replay reading (0 deaths). Its two files
-- are in the bucket under the keys below (put with the release, like an installer); the row points at them.
INSERT INTO tas_runs (level_id, category, revision, level_hash, level_version, sim_version, time_ms, explored, record_key, record_sha256,
    record_size, replay_key, replay_sha256, replay_size, training_ms_at_publish, explored_at_publish, published_by, published_by_username, published_at)
VALUES ('sylvanox_01', 'ANY_PERCENT', 3, '49d7d22cc13e1286a88914755e0a3151c9288ae0b6f776b8fab05647398bbb0a', 8, 21, 78483, 4860819,
    'tas/sylvanox_01/ANY_PERCENT/3-64c5964c9469c5abe7bd07df381f09e3893f07339698e099c6fd5c7ad0fe60a1.json', '64c5964c9469c5abe7bd07df381f09e3893f07339698e099c6fd5c7ad0fe60a1', 10035, 'tas/sylvanox_01/ANY_PERCENT/3-958a2a9a5bafadf21972c271aba964f37adff75a65548a7281b2f324aef9eb3f.cprz', '958a2a9a5bafadf21972c271aba964f37adff75a65548a7281b2f324aef9eb3f', 19639, 0, 4860819, NULL, NULL, '2026-11-28T19:10:34.000Z')
ON CONFLICT (level_id, category, revision) DO NOTHING;
