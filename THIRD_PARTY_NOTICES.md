# Third-Party Notices

CosmoSoft includes the following third-party components in its embedded map.

## Leaflet 1.9.4

- Project: https://leafletjs.com/
- License: BSD 2-Clause
- Bundled license: `assets/map/leaflet.LICENSE`

## Three.js 0.160.0

- Project: https://threejs.org/
- License: MIT
- Bundled license: `assets/map/vendor/three/LICENSE`

The bundled files are pinned copies used so that application code does not need
to download executable JavaScript at runtime.

## Remote map and terrain data

The embedded map can request raster data from the following services. These
services are not bundled with CosmoSoft and remain subject to their respective
terms and attribution requirements.

- CARTO basemaps: © OpenStreetMap contributors, © CARTO
  (<https://carto.com/attribution/index.html>)
- OpenTopoMap: map data © OpenStreetMap contributors; map style © OpenTopoMap,
  licensed CC BY-SA (<https://opentopomap.org/about>)
- Esri World Imagery: tiles © Esri; source credits include Esri, Maxar,
  Earthstar Geographics, and the GIS User Community
  (<https://www.esri.com/en-us/legal/terms/full-master-agreement>)
- Mapzen Terrain Tiles on AWS: © Mapzen, OpenStreetMap, and others. Source attribution varies
  by location and can include ArcticDEM; Geoscience Australia; offene Daten
  Österreichs; Open Government Licence — Canada; Copernicus EU-DEM; NOAA
  ETOPO1; INEGI; Land Information New Zealand; Kartverket; the UK Environment
  Agency; and USGS 3DEP, GMTED2010, and SRTM. The authoritative attribution list
  is maintained at
  <https://github.com/tilezen/joerd/blob/master/docs/attribution.md>.

Remote map and elevation data is for visualization and must not be used for
navigation.
