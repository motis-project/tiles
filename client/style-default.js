function createShield(opt) {
  const d = 32

  const cv = document.createElement('canvas');
  cv.width = d;
  cv.height = d;
  const ctx = cv.getContext('2d');

  // coord of the line (front = near zero, back = opposite)
  const l_front = 1;
  const l_back = d-1;

  // coord start of the arc
  const lr_front = l_front + 2;
  const lr_back = l_back - 2;

  // control point of the arc
  const lp_front = l_front + 1;
  const lp_back = l_back - 1;

  let p = new Path2D();
  p.moveTo(lr_front, l_front);

  // top line
  p.lineTo(lr_back, l_front);
  // top right corner
  p.bezierCurveTo(lp_back, lp_front, lp_back, lp_front, l_back, lr_front);
  // right line
  p.lineTo(l_back, lr_back);
  // bottom right corner
  p.bezierCurveTo(lp_back, lp_back, lp_back, lp_back, lr_back, l_back);
  // bottom line
  p.lineTo(lr_front, l_back);
  // bottom left corner
  p.bezierCurveTo(lp_front, lp_back, lp_front, lp_back, l_front, lr_back);
  // left line
  p.lineTo(l_front, lr_front);
  // top left corner
  p.bezierCurveTo(lp_front, lp_front, lp_front, lp_front, lr_front, l_front);

  p.closePath();

  ctx.fillStyle = opt.fill;
  ctx.fill(p);
  ctx.strokeStyle = opt.stroke;
  ctx.stroke(p);

  return [
    ctx.getImageData(0, 0, d, d),
    {
      content: [lr_front, lr_front, lr_back, lr_back],
      stretchX: [[lr_front, lr_back]],
      stretchY: [[lr_front, lr_back]]
    }
  ];
}

const water = "#bbd3f9";
const rail = "#5b5b5b";
// const pedestrian = "#ff0000";
const pedestrian = "#fcfcfc";

const sport = "#a4b1bc";
const sport_outline = "#94a7b7";

// const building = "#ff0000";
const building = "#d8d8d8";
const building_outline = "#c1c1c1";

export const style = (map) => {
  map.setStyle({
    "version": 8,
    "sources": {
        "osm": {
            "type": "vector",
            "tiles": ["/{z}/{x}/{y}.mvt"],
            "maxzoom": 20
        }
    },
    "glyphs": "/glyphs/{fontstack}/{range}.pbf",
    "layers": [
        {
            "id": "background",
            "type": "background",
            "paint": { "background-color": "#dddddd" }
        }, {
            "id": "coastline",
            "type": "fill",
            "source": "osm",
            "source-layer": "coastline",
            "paint": {"fill-color": water}
        }, {
            "id": "landuse",
            "type": "fill",
            "source": "osm",
            "source-layer": "landuse",
            "paint": {
                "fill-color": ["match", ["get", "landuse"],
                    "complex", "#efe7d0",
                    "commercial", "#f1e3f4",
                    "industrial", "#eadfce",
                    "residential", "#edebd5",
                    "retail", "#f2dae7",
                    "construction", "#aaa69d",

                    "nature_light", "#d7f2e1",
                    "nature_heavy", "#c0dbbe",
                    "park", "#e6f4d4",
                    "cemetery", "#bedbd1",
                    "beach", "#fffcd3",

                    "magenta"]
            }
        }, {
            "id": "water",
            "type": "fill",
            "source": "osm",
            "source-layer": "water",
            "paint": {"fill-color": water}
        }, {
            "id": "sport",
            "type": "fill",
            "source": "osm",
            "source-layer": "sport",
            "paint": {
                "fill-color": sport,
                "fill-outline-color": sport_outline

            }
        }, {
            "id": "pedestrian",
            "type": "fill",
            "source": "osm",
            "source-layer": "pedestrian",
            "paint": {"fill-color": pedestrian}
        }, {
            "id": "waterway",
            "type": "line",
            "source": "osm",
            "source-layer": "waterway",
            "paint": {"line-color": water}
        }, {
            "id": "road_back",
            "type": "line",
            "source": "osm",
            "source-layer": "road",
            "filter": [
              "!in",
              "highway",
              "footway", "track", "steps", "cycleway", "path", "unclassified"
            ],
            "layout": {
              "line-cap": "round",
            },
            "paint": {
              "line-color": "#a5a5a5",
              "line-opacity": 0.5,
              "line-width": [
                "let",
                "base", ["match", ["get", "highway"],
                  "motorway", 4,
                  ["trunk", "motorway_link"], 3.5,
                  ["primary", "secondary", "aeroway", "trunk_link"], 3,
                  ["primary_link", "secondary_link", "tertiary", "tertiary_link"], 1.75,
                  "residential", 1.5,
                0.75],
                ["interpolate", ["linear"], ["zoom"],
                  5,  ["+", ["*", ["var", "base"], 0.1], 1],
                  9,  ["+", ["*", ["var", "base"], 0.4], 1],
                  12, ["+", ["*", ["var", "base"], 1], 1],
                  16, ["+", ["*", ["var", "base"], 4], 1],
                  20, ["+", ["*", ["var", "base"], 8], 1]
                ]
              ]
            }
        }, {
            "id": "road",
            "type": "line",
            "source": "osm",
            "source-layer": "road",
            "layout": {
              "line-cap": "round",
            },
            "paint": {
              "line-color": "#ffffff",
              "line-opacity": ["match", ["get", "highway"],
                ["footway", "track", "steps", "cycleway", "path", "unclassified"], 0.66,
                1],
              "line-width": [
                "let",
                "base", ["match", ["get", "highway"],
                  "motorway", 4,
                  ["trunk", "motorway_link"], 3.5,
                  ["primary", "secondary", "aeroway", "trunk_link"], 3,
                  ["primary_link", "secondary_link", "tertiary", "tertiary_link"], 1.75,
                  "residential", 1.5,
                0.75],
                ["interpolate", ["linear"], ["zoom"],
                  5,  ["*", ["var", "base"], 0.1],
                  9,  ["*", ["var", "base"], 0.4],
                  12, ["*", ["var", "base"], 1],
                  16, ["*", ["var", "base"], 4],
                  20, ["*", ["var", "base"], 8]
                ]
              ]
            }
        }, {
            "id": "rail_old",
            "type": "line",
            "source": "osm",
            "source-layer": "rail",
            "filter": ["==", "rail", "old"],
            "paint": {
                "line-color": rail,
            }
        }, {
            "id": "rail_detail",
            "type": "line",
            "source": "osm",
            "source-layer": "rail",
            "filter": ["==", "rail", "detail"],
            "paint": {
                "line-color": rail,
            }
        }, {
            "id": "rail_secondary",
            "type": "line",
            "source": "osm",
            "source-layer": "rail",
            "filter": ["==", "rail", "secondary"],
            "paint": {
                "line-color": rail,
                "line-width": 1.15
            }
        }, {
            "id": "rail_primary",
            "type": "line",
            "source": "osm",
            "source-layer": "rail",
            "filter": ["==", "rail", "primary"],
            "paint": {
                "line-color": rail,
                "line-width": 1.3
            }
        }, {
            "id": "building",
            "type": "fill",
            "source": "osm",
            "source-layer": "building",
            "paint": {
                "fill-color": building,
                "fill-outline-color": building_outline
            }
        }, {
            "id": "road-ref-shield",
            "type": "symbol",
            "source": "osm",
            "source-layer": "road",
            "minzoom": 6,
            "filter": ["all",
                        ["has", "ref"],
                        ["any", ["==", ["get", "highway"], "motorway"],
                                ["==", ["get", "highway"], "trunk"],
                                ["==", ["get", "highway"], "secondary"],
                                [">", ["zoom"], 11]]],
            "layout": {
              "symbol-placement": "line",
              "text-field": ["get", "ref"],
              "text-font": ["Noto Sans Regular"],
              "text-size": ["case", ["==", ["get", "highway"], "motorway"], 11, 10],
              "text-justify": "center",
              "text-rotation-alignment": "viewport",
              "text-pitch-alignment": "viewport",
              "icon-image": "shield",
              "icon-text-fit": "both",
              "icon-text-fit-padding": [.5, 4, .5, 4],
              "icon-rotation-alignment": "viewport",
              "icon-pitch-alignment": "viewport",
            },
            "paint": {
              "text-color": "#333333",
            },
        }, {
            "id": "road-name-text",
            "type": "symbol",
            "source": "osm",
            "source-layer": "road",
            "minzoom": 15,
            "layout": {
              "symbol-placement": "line",
              "text-field": ["get", "name"],
              "text-font": ["Noto Sans Regular"],
              "text-size": 9,
            },
            "paint": {
              "text-halo-width": 11,
              "text-halo-color": "white",
              "text-color": "#333333"
            }
        }, {

            "id": "towns",
            "type": "symbol",
            "source": "osm",
            "source-layer": "cities",
            "filter": ["!=", ["get", "place"], "city"],
            "layout": {
              // "symbol-sort-key": ["get", "population"],
              "text-field": ["get", "name"],
              "text-font": ["Noto Sans Regular"],
              "text-size": 12
            },
            "paint": {
              "text-halo-width": 1,
              "text-halo-color": "white",
              "text-color": "#333333"
            }
        }, {
            "id": "cities",
            "type": "symbol",
            "source": "osm",
            "source-layer": "cities",
            "filter": ["==", ["get", "place"], "city"],
            "layout": {
              // "symbol-sort-key": ["get", "population"],
              "text-field": ["get", "name"],
              "text-font": ["Noto Sans Bold"],
              "text-size": 18
            },
            "paint": {
              "text-halo-width": 2,
              "text-halo-color": "white",
              "text-color": "#111111"
            }
        }
    ]
  });

  map.addImage(
    "shield",
    ...createShield({
      fill: "hsl(0, 0%, 98%)",
      stroke: "hsl(0, 0%, 75%)",
    })
  );
};
