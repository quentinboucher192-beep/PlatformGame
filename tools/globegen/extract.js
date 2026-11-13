// Étape 1 du générateur du globe : lit les paquets npm (Natural Earth via world-atlas,
// i18n-iso-countries, all-the-cities, flag-icons) et écrit des fichiers intermédiaires
// dans le dossier passé en argument (par défaut tools/globegen/build/).
// Usage : node extract.js [dossier_sortie]
'use strict';

const fs = require('fs');
const path = require('path');
const topojson = require('topojson-client');
const iso = require('i18n-iso-countries');
const { Resvg } = require('@resvg/resvg-js');

const OUT = path.resolve(process.argv[2] || path.join(__dirname, 'build'));
const FLAG_SCALE = 4; // drapeaux rendus à 4x (192x144) puis réduits en Python
fs.mkdirSync(OUT, { recursive: true });
fs.mkdirSync(path.join(OUT, 'flags'), { recursive: true });

const topo = require('world-atlas/countries-50m.json');
const geoms = topo.objects.countries.geometries;

// --- Attribution d'un code ISO alpha-2 à chaque entité Natural Earth -------------------
// Entités sans identifiant numérique dans world-atlas 2.0.2 :
//  - Kosovo            -> XK (code utilisateur largement employé)
//  - Somaliland        -> SO (ISO 3166 le rattache à la Somalie)
//  - N. Cyprus         -> CY (ISO 3166 le rattache à Chypre)
//  - Indian Ocean Ter. -> polygones répartis entre CX (île Christmas) et CC (îles Cocos)
//  - Siachen Glacier   -> aucun code (zone disputée, reste « pas de pays »)
// « Ashmore and Cartier Is. » partage l'identifiant 036 de l'Australie -> AU.
const NAMED = { 'Kosovo': 'XK', 'Somaliland': 'SO', 'N. Cyprus': 'CY' };

// Territoires ayant leur propre code ISO mais inclus dans le polygone d'un autre pays chez
// Natural Earth : chaque polygone est attribué au premier code dont la boîte englobante
// correspond (b = {minLon, maxLon, minLat, maxLat}), sinon au code de l'entité.
const inBox = (b, lo0, lo1, la0, la1) => b.minLon >= lo0 && b.maxLon <= lo1 && b.minLat >= la0 && b.maxLat <= la1;
const SPLITS = {
  'Indian Ocean Ter.': [['CX', (b) => b.minLon > 100], ['CC', () => true]],
  'France': [
    ['GF', (b) => inBox(b, -55, -51, 2, 6.5)], ['GP', (b) => inBox(b, -62, -60.9, 15.8, 16.6)],
    ['MQ', (b) => inBox(b, -61.3, -60.7, 14.3, 15)], ['RE', (b) => inBox(b, 55, 56, -21.5, -20.8)],
    ['YT', (b) => inBox(b, 44.9, 45.4, -13.1, -12.5)],
  ],
  'Norway': [['SJ', (b) => b.minLat > 74 || (b.maxLon < -5 && b.minLat > 70)]],
  'Netherlands': [['BQ', (b) => inBox(b, -69.5, -62, 11.5, 18.5)]],
};
const bbox = (poly) => {
  const lon = poly[0].map((c) => c[0]), lat = poly[0].map((c) => c[1]);
  return { minLon: Math.min(...lon), maxLon: Math.max(...lon), minLat: Math.min(...lat), maxLat: Math.max(...lat) };
};

function codeOfGeom(g) {
  if (g.id !== undefined) {
    const a2 = iso.numericToAlpha2(g.id);
    if (!a2) throw new Error('identifiant numérique inconnu : ' + g.id + ' ' + g.properties.name);
    return a2;
  }
  if (NAMED[g.properties.name]) return NAMED[g.properties.name];
  return null;
}

const round5 = (v) => Math.round(v * 1e5) / 1e5;
const roundCoords = (c) => (typeof c[0] === 'number' ? [round5(c[0]), round5(c[1])] : c.map(roundCoords));
const asPolys = (geom) => (!geom ? [] : geom.type === 'Polygon' ? [geom.coordinates] : geom.type === 'MultiPolygon' ? geom.coordinates : []);

const byCode = new Map();
const extraPolys = new Map(); // polygones attribués individuellement (territoires éclatés)
const unassigned = [];
const splitLog = [];
for (const g of geoms) {
  const rules = SPLITS[g.properties.name];
  if (rules) {
    for (const poly of asPolys(topojson.feature(topo, g).geometry)) {
      const b = bbox(poly);
      const rule = rules.find(([, test]) => test(b));
      const code = rule ? rule[0] : codeOfGeom(g);
      if (rule) splitLog.push(code);
      if (!extraPolys.has(code)) extraPolys.set(code, []);
      extraPolys.get(code).push(poly);
    }
    continue;
  }
  const code = codeOfGeom(g);
  if (!code) { unassigned.push(g.properties.name); continue; }
  if (!byCode.has(code)) byCode.set(code, []);
  byCode.get(code).push(g);
}

const countries = [];
const allCodes = new Set([...byCode.keys(), ...extraPolys.keys()]);
for (const code of [...allCodes].sort()) {
  let polys = [];
  if (byCode.has(code)) {
    const list = byCode.get(code);
    // merge() dissout les frontières internes (ex. Chypre + Chypre du Nord).
    polys = polys.concat(asPolys(list.length === 1 ? topojson.feature(topo, list[0]).geometry : topojson.merge(topo, list)));
  }
  if (extraPolys.has(code)) polys = polys.concat(extraPolys.get(code));
  countries.push({ code, polys: roundCoords(polys) });
}

// Frontières entre pays de codes différents, et traits de côte (arcs extérieurs).
const codeKey = (g) => codeOfGeom(g) || ('?' + g.properties.name);
const borders = topojson.mesh(topo, topo.objects.countries, (a, b) => a !== b && codeKey(a) !== codeKey(b));
const coast = topojson.mesh(topo, topo.objects.countries, (a, b) => a === b);
const land = topojson.feature(topo, topo.objects.land);

fs.writeFileSync(path.join(OUT, 'geo.json'), JSON.stringify({
  source: 'world-atlas@2.0.2 countries-50m.json (Natural Earth 1:50m)',
  unassigned,
  split_territories: [...new Set(splitLog)].sort(),
  countries,
  land: roundCoords(land.features ? land.features.flatMap((f) => asPolys(f.geometry)) : asPolys(land.geometry || land)),
  borders: roundCoords(borders.coordinates),
  coast: roundCoords(coast.coordinates),
}));

// --- Codes ISO et noms ---------------------------------------------------------------
const cities = require('all-the-cities');
const bestCity = new Map();
for (const c of cities) {
  const prev = bestCity.get(c.country);
  if (!prev || c.population > prev.population || (c.population === prev.population && c.cityId < prev.cityId)) bestCity.set(c.country, c);
}
const codes = Object.keys(iso.getAlpha2Codes()).sort().map((code) => {
  const c = bestCity.get(code);
  return {
    code,
    alpha3: iso.alpha2ToAlpha3(code),
    numeric: iso.alpha2ToNumeric(code),
    name_fr: iso.getName(code, 'fr'),
    name_en: iso.getName(code, 'en'),
    largest_city: c ? { lon: c.loc.coordinates[0], lat: c.loc.coordinates[1], population: c.population } : null,
    flag_svg: fs.existsSync(require.resolve('flag-icons/package.json').replace('package.json', 'flags/4x3/' + code.toLowerCase() + '.svg')),
  };
});
fs.writeFileSync(path.join(OUT, 'codes.json'), JSON.stringify(codes, null, 1));

// --- Villes (lumières nocturnes) : float32 little-endian [lon, lat, population] -------
const sorted = cities.slice().sort((a, b) => a.cityId - b.cityId);
const buf = Buffer.alloc(sorted.length * 12);
sorted.forEach((c, i) => {
  buf.writeFloatLE(c.loc.coordinates[0], i * 12);
  buf.writeFloatLE(c.loc.coordinates[1], i * 12 + 4);
  buf.writeFloatLE(c.population, i * 12 + 8);
});
fs.writeFileSync(path.join(OUT, 'cities.f32'), buf);

// --- Drapeaux : rendu SVG -> PNG 192x144 (anticrénelé) ---------------------------------
const flagDir = path.dirname(require.resolve('flag-icons/package.json'));
let nFlags = 0;
for (const c of codes) {
  if (!c.flag_svg) continue;
  const svg = fs.readFileSync(path.join(flagDir, 'flags', '4x3', c.code.toLowerCase() + '.svg'), 'utf8');
  const png = new Resvg(svg, { fitTo: { mode: 'width', value: 48 * FLAG_SCALE }, font: { loadSystemFonts: false } }).render().asPng();
  fs.writeFileSync(path.join(OUT, 'flags', c.code + '.png'), png);
  nFlags++;
}

console.log(`extract: ${countries.length} pays sur la carte (dont territoires séparés : ${[...new Set(splitLog)].sort().join(' ')}), ` +
  `${codes.length} codes ISO, ${sorted.length} villes, ${nFlags} drapeaux` +
  (unassigned.length ? `, sans code : ${unassigned.join(', ')}` : ''));
