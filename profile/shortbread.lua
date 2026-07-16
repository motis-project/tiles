-- Shortbread-compatible profile for the tiles engine.
--
-- Ported from the Shortbread tilemaker profile
--   https://github.com/shortbread-tiles/shortbread-tilemaker/blob/main/process.lua
-- so that tiles served by this engine can be rendered with the Shortbread OSM
-- style (https://github.com/shortbread-tiles/shortbread-osm-bright / mapnik style).
--
-- The goal is to emit the same layer names and the same `kind` / `name*` /
-- attribute vocabulary that the Shortbread schema (v1.0) defines, so an existing
-- Shortbread GL/mapnik style renders against this backend with as few changes as
-- possible.
--
-- Differences that are inherent to the tiles engine (NOT new features were added
-- here, the script simply works within the engine's capabilities):
--
--  * One OSM object -> exactly one target layer. Shortbread/tilemaker duplicates
--    some objects into a geometry layer *and* a dedicated label layer
--    (water_polygons_labels, water_lines_labels, street_labels,
--    streets_polygons_labels) or a POI centroid. Here the primary
--    geometry/point layer wins and the `name`/`name_de`/`name_en` attributes are
--    attached to it. The separate `*_labels` layers are therefore not produced;
--    a GL style still finds the geometry and, for symbol layers, places labels on
--    the polygon/line automatically.
--  * `ocean` and `boundary_labels` come from shapefiles in Shortbread. This
--    engine has its own coastline handling that fills the hardcoded `coastline`
--    layer instead; `boundary_labels` is not produced.
--  * No relation access -> `boundaries` are derived from tags on the boundary
--    *way* itself (boundary=administrative + admin_level) instead of from the
--    parent relation, so coverage is limited to ways carrying those tags.
--  * No line length / polygon area exposed to Lua -> area-scaled min zooms use
--    the engine's set_approved_min_by_area(); line min zooms use fixed values.
--  * z_order is not part of the engine's feature model and is omitted.

local INF = 99

--------------------------------------------------------------------------------
-- helpers
--------------------------------------------------------------------------------

-- NOTE: the tiles engine runs this script (script_file) *before* it opens the
-- Lua standard libraries, so no library functions (ipairs, string.*, tonumber,
-- ...) may be called at the top level. Only core-language constructs (numeric
-- for, the `#` operator) are safe here. Library functions are available inside
-- process_node/way/area because those run after the libraries are opened.
local function set(list)
  local s = {}
  for i = 1, #list do s[list[i]] = true end
  return s
end

-- value if it is in the accepted set, else ""
local function accepted_or_empty(s, v)
  if v ~= "" and s[v] then return v end
  return ""
end

local function fallback(a, b, c)
  if a ~= "" then return a end
  if b ~= "" then return b end
  return c
end

-- name / name_de / name_en with the Shortbread fallback logic
local function set_names(f)
  local name = f:get_tag("name")
  local name_de = f:get_tag("name:de")
  local name_en = f:get_tag("name:en")
  f:add_string("name", fallback(name, name_en, name_de))
  f:add_string("name_de", fallback(name_de, name, name_en))
  f:add_string("name_en", fallback(name_en, name, name_de))
end

local function set_address(f)
  f:add_string("housename", f:get_tag("addr:housename"))
  f:add_string("housenumber", f:get_tag("addr:housenumber"))
end

local function is_tunnel(f)
  local t = f:get_tag("tunnel")
  return t == "yes" or t == "culvert" or t == "building_passage" or
         f:get_tag("covered") == "yes"
end

local function is_bridge(f)
  local b = f:get_tag("bridge")
  return b == "yes" or b == "viaduct" or b == "boardwalk" or b == "cantilever" or
         b == "covered" or b == "low_water_crossing" or b == "movable" or
         b == "trestle"
end

local function is_oneway(v)
  return v == "yes" or v == "1" or v == "true" or v == "-1"
end

-- OSM `level` tag -> numeric (from_level, to_level).
-- Accepts single values ("1", "-2"), ranges ("0;2") and semicolon lists
-- ("-1;0;2" -> min/max). Returns nil, nil if no number is present.
-- NOTE: the level attributes MUST be emitted as integers (add_integer /
-- add_tag_as_integer) — the MapLibre indoor overlay filters with numeric
-- comparisons (['==', 'level', 0], ['<=', 'from_level', level]); a string
-- "0" would never match the number 0 and the floor would stay invisible.
-- NOTE: only string.match / tonumber may be used here — the tiles Lua sandbox
-- does not open the `math` library. (Same approach as full.lua.)
local function split_level(level)
  if level == nil or level == "" then return nil, nil end
  local from_level, to_level = level:match("(-?%d+);(-?%d+)")
  if from_level and to_level then
    return tonumber(from_level), tonumber(to_level)
  end
  local lvl = level:match("(-?%d+)")
  if lvl then return tonumber(lvl), tonumber(lvl) end
  return nil, nil
end

--------------------------------------------------------------------------------
-- accepted POI values (from the Shortbread profile)
--------------------------------------------------------------------------------

local poi_amenity_values = set { "police", "fire_station", "post_box", "post_office", "telephone",
  "library", "townhall", "courthouse", "prison", "embassy", "community_centre", "nursing_home",
  "arts_centre", "grave_yard", "marketplace", "recycling", "university", "school", "college", "public_building",
  "pharmacy", "hospital", "clinic", "doctors", "dentist", "veterinary", "theatre", "nightclub", "cinema",
  "restaurant", "fast_food", "cafe", "pub", "bar", "foot_court", "biergarten", "shelter",
  "car_rental", "car_wash", "car_sharing", "bicycle_rental", "vending_machine", "bank", "atm",
  "toilets", "bench", "drinking_water", "fountain", "hunting_stand", "waste_basket", "place_of_worship" }
local catering_values = set { "restaurant", "fast_food", "pub", "bar", "cafe" }
local poi_leisure_values = set { "playground", "dog_park", "sports_centre", "pitch", "swimming_pool", "water_park",
  "golf_course", "stadium", "ice_rink" }
local sport_values = set { "pitch", "sports_centre" }
local poi_tourism_values = set { "hotel", "motel", "artwork", "bed_and_breakfast", "guest_house", "hostel", "chalet",
  "camp_site", "alpine_hut", "caravan_site", "information", "picnic_site", "viewpoint", "zoo", "theme_park" }
local poi_shop_values = set { "supermarket", "bakery", "kiosk", "mall", "department_store", "general",
  "convenience", "clothes", "florist", "chemist", "books", "butcher", "shoes", "alcohol",
  "beverages", "optician", "jewelry", "gift", "sports", "stationery", "outdoor", "mobile_phone",
  "toys", "newsagent", "greengrocer", "beauty", "video", "car", "bicycle", "doityourself",
  "hardware", "furniture", "computer", "garden_centre", "hairdresser", "travel_agency", "laundry",
  "dry_cleaning" }
local poi_man_made_values = set { "surveillance", "tower", "windmill", "lighthouse", "wastewater_plant",
  "water_well", "watermill", "water_works" }
local poi_historic_values = set { "monument", "memorial", "castle", "ruins", "archaeological_site",
  "wayside_cross", "wayside_shrine", "battlefield", "fort" }
local poi_emergency_values = set { "phone", "fire_hydrant", "defibrillator" }
local poi_highway_values = set { "emergency_access_point" }
local poi_office_values = set { "diplomatic" }

--------------------------------------------------------------------------------
-- pois (Shortbread layer "pois")
-- returns true if the feature was written to the pois layer
--------------------------------------------------------------------------------

local function process_pois(f)
  local amenity = accepted_or_empty(poi_amenity_values, f:get_tag("amenity"))
  local shop = accepted_or_empty(poi_shop_values, f:get_tag("shop"))
  local tourism = accepted_or_empty(poi_tourism_values, f:get_tag("tourism"))
  local man_made = accepted_or_empty(poi_man_made_values, f:get_tag("man_made"))
  local historic = accepted_or_empty(poi_historic_values, f:get_tag("historic"))
  local leisure = accepted_or_empty(poi_leisure_values, f:get_tag("leisure"))
  local emergency = accepted_or_empty(poi_emergency_values, f:get_tag("emergency"))
  local highway = accepted_or_empty(poi_highway_values, f:get_tag("highway"))
  local office = accepted_or_empty(poi_office_values, f:get_tag("office"))

  if amenity == "" and shop == "" and tourism == "" and man_made == "" and
     historic == "" and leisure == "" and emergency == "" and highway == "" and
     office == "" then
    return false
  end

  f:set_target_layer("pois")
  f:set_approved_min(14)
  f:add_string("amenity", amenity)
  f:add_string("shop", shop)
  f:add_string("tourism", tourism)
  f:add_string("man_made", man_made)
  f:add_string("historic", historic)
  f:add_string("leisure", leisure)
  f:add_string("emergency", emergency)
  f:add_string("highway", highway)
  f:add_string("office", office)

  if catering_values[amenity] then f:add_tag_as_string("cuisine") end
  if sport_values[leisure] then f:add_tag_as_string("sport") end
  if amenity == "vending_machine" then f:add_tag_as_string("vending") end
  if tourism == "information" then f:add_tag_as_string("information") end
  if man_made == "tower" then f:add_tag_as_string("tower:type") end
  if amenity == "recycling" then
    f:add_tag_as_bool("recycling:glass_bottles")
    f:add_tag_as_bool("recycling:paper")
    f:add_tag_as_bool("recycling:clothes")
    f:add_tag_as_bool("recycling:scrap_metal")
  end
  if amenity == "bank" then f:add_tag_as_bool("atm") end
  if amenity == "place_of_worship" then
    f:add_tag_as_string("religion")
    f:add_tag_as_string("denomination")
  end

  set_names(f)
  set_address(f)
  return true
end

--------------------------------------------------------------------------------
-- public_transport (Shortbread layer "public_transport")
--------------------------------------------------------------------------------

local function process_public_transport(f)
  local railway = f:get_tag("railway")
  local aeroway = f:get_tag("aeroway")
  local aerialway = f:get_tag("aerialway")
  local highway = f:get_tag("highway")
  local amenity = f:get_tag("amenity")
  local kind = ""

  if railway == "station" or railway == "halt" or railway == "tram_stop" then
    kind = railway
  elseif highway == "bus_stop" then
    kind = highway
  elseif amenity == "bus_station" or amenity == "ferry_terminal" then
    kind = amenity
  elseif aerialway == "station" then
    kind = "aerialway_station"
  elseif aeroway == "aerodrome" or aeroway == "helipad" then
    kind = aeroway
  else
    return false
  end

  f:set_target_layer("public_transport")
  f:set_approved_min(11)
  f:add_string("kind", kind)
  local iata = f:get_tag("iata")
  if iata ~= "" then f:add_string("iata", iata) end
  set_names(f)
  return true
end

--------------------------------------------------------------------------------
-- place_labels (Shortbread layer "place_labels")
--------------------------------------------------------------------------------

local function process_place_labels(node)
  local place = node:get_tag("place")
  local mz = INF
  local kind = place
  local population = node:get_tag("population")

  if place == "city" then
    mz = 6
    if population == "" then population = "100000" end
  elseif place == "town" then
    mz = 7
    if population == "" then population = "5000" end
  elseif place == "village" then
    mz = 10
    if population == "" then population = "100" end
  elseif place == "hamlet" then
    mz = 10
    if population == "" then population = "50" end
  elseif place == "suburb" then
    mz = 10
    if population == "" then population = "1000" end
  elseif place == "quarter" then
    mz = 10
    if population == "" then population = "500" end
  elseif place == "neighbourhood" then
    mz = 10
    if population == "" then population = "100" end
  elseif place == "locality" or place == "island" then
    mz = 10
    if population == "" then population = "0" end
  elseif place == "isolated_dwelling" or place == "farm" then
    mz = 10
    if population == "" then population = "5" end
  end

  if (place == "city" or place == "town" or place == "village" or place == "hamlet")
     and node:has_any_tag("capital") then
    local capital = node:get_tag("capital")
    if capital == "yes" then
      mz = 4
      kind = "capital"
    elseif capital == "4" then
      mz = 4
      kind = "state_capital"
    end
  end

  if mz >= INF then return false end

  node:set_target_layer("place_labels")
  node:set_approved_min(mz)
  node:add_string("kind", kind)
  set_names(node)
  local pop = tonumber(population)
  if pop ~= nil then node:add_integer("population", pop) end
  return true
end

--------------------------------------------------------------------------------
-- nodes
--------------------------------------------------------------------------------

function process_node(node)
  -- place_labels
  if node:has_any_tag("place") and node:has_any_tag("name") then
    if process_place_labels(node) then return end
  end

  -- elevators (indoor overlay). A lift is a node connecting two floors;
  -- from_level/to_level drive which levels it is shown on.
  if node:has_tag("highway", "elevator") then
    node:set_target_layer("indoor")
    node:set_approved_min(16)
    node:add_string("indoor", "elevator")
    local from_level, to_level = split_level(node:get_tag("level"))
    if from_level ~= nil then
      node:add_integer("from_level", from_level)
      node:add_integer("to_level", to_level)
    end
    set_names(node)
    return
  end

  -- street_labels_points (motorway junctions)
  if node:has_tag("highway", "motorway_junction") then
    node:set_target_layer("street_labels_points")
    node:set_approved_min(12)
    node:add_string("kind", "motorway_junction")
    set_names(node)
    node:add_string("ref", node:get_tag("ref"))
    return
  end

  -- public_transport
  if process_public_transport(node) then return end

  -- pois
  if process_pois(node) then return end

  -- addresses
  local housenumber = node:get_tag("addr:housenumber")
  local housename = node:get_tag("addr:housename")
  if housenumber ~= "" or housename ~= "" then
    node:set_target_layer("addresses")
    node:set_approved_min(14)
    set_address(node)
  end
end

--------------------------------------------------------------------------------
-- streets (Shortbread layer "streets", written from streets/streets_med/
-- streets_low in tilemaker -> a single min-zoom feature here)
--------------------------------------------------------------------------------

local function process_streets(way)
  local highway = way:get_tag("highway")
  local railway = way:get_tag("railway")
  local aeroway = way:get_tag("aeroway")
  local service = way:get_tag("service")
  local mz = INF
  local kind = ""
  local rail = false

  if highway ~= "" then
    -- min zooms as in the old full.lua profile (NOT Shortbread's): links get
    -- their own, much later min zoom instead of inheriting the parent's.
    if highway == "motorway" then
      mz = 5; kind = "motorway"
    elseif highway == "trunk" then
      mz = 5; kind = "trunk"
    elseif highway == "motorway_link" then
      mz = 9; kind = "motorway"
    elseif highway == "trunk_link" then
      mz = 9; kind = "trunk"
    elseif highway == "primary" then
      mz = 9; kind = "primary"
    elseif highway == "secondary" then
      mz = 9; kind = "secondary"
    elseif highway == "tertiary" then
      mz = 9; kind = "tertiary"
    elseif highway == "primary_link" then
      mz = 12; kind = "primary"
    elseif highway == "secondary_link" then
      mz = 12; kind = "secondary"
    elseif highway == "tertiary_link" then
      mz = 12; kind = "tertiary"
    elseif highway == "unclassified" or highway == "residential" or
           highway == "living_street" or highway == "service" or
           highway == "footway" or highway == "track" or highway == "steps" or
           highway == "cycleway" or highway == "path" then
      mz = 12; kind = highway
    elseif highway == "bus_guideway" or highway == "busway" then
      mz = 12; kind = highway
    elseif highway == "pedestrian" then
      mz = 13; kind = highway
    end
  elseif railway == "rail" and service == "" then
    -- Shortbread uses 8; approve earlier so the main rail network is already
    -- visible at country-level zooms (as the old full.lua profile did).
    kind = railway; rail = true; mz = 5
  elseif railway == "narrow_gauge" and service == "" then
    kind = railway; rail = true; mz = 8
  elseif ((railway == "rail" or railway == "narrow_gauge") and service ~= "") or
         railway == "light_rail" or railway == "tram" or railway == "subway" or
         railway == "funicular" or railway == "monorail" then
    kind = railway; rail = true; mz = 10
  elseif aeroway == "runway" then
    kind = aeroway; mz = 11
  elseif aeroway == "taxiway" then
    kind = aeroway; mz = 13
  end

  if mz >= INF then return false end

  local surface = way:get_tag("surface")
  if kind ~= "" and surface ~= "" then
    if surface == "unpaved" or surface == "compacted" or surface == "dirt" or
       surface == "earth" or surface == "fine_gravel" or surface == "grass" or
       surface == "grass_paver" or surface == "gravel" or surface == "ground" or
       surface == "mud" or surface == "pebblestone" or surface == "salt" or
       surface == "woodchips" or surface == "clay" then
      surface = "unpaved"
    elseif surface == "paved" or surface == "asphalt" or surface == "cobblestone" or
           surface == "cobblestone:flattened" or surface == "sett" or
           surface == "concrete" or surface == "concrete:lanes" or
           surface == "concrete:plates" or surface == "paving_stones" then
      surface = "paved"
    else
      surface = ""
    end
  end

  local link = (highway == "motorway_link" or highway == "trunk_link" or
                highway == "primary_link" or highway == "secondary_link" or
                highway == "tertiary_link")
  local oneway = way:get_tag("oneway")

  way:set_target_layer("streets")
  way:set_approved_min(mz)
  way:add_string("kind", kind)
  way:add_bool("link", link)
  if surface ~= "" then way:add_string("surface", surface) end
  way:add_string("bicycle", way:get_tag("bicycle"))
  way:add_string("horse", way:get_tag("horse"))
  way:add_bool("tunnel", is_tunnel(way))
  way:add_bool("bridge", is_bridge(way))
  way:add_bool("oneway", not rail and is_oneway(oneway))
  way:add_bool("oneway_reverse", not rail and oneway == "-1")
  local tracktype = way:get_tag("tracktype")
  if tracktype ~= "" then way:add_string("tracktype", tracktype) end
  way:add_bool("rail", rail)
  if service ~= "" then way:add_string("service", service) end
  -- name attributes so a style can label streets off this layer as well
  -- (Shortbread's separate street_labels line layer is not produced here).
  set_names(way)

  -- road ref for shields (Shortbread carries this on street_labels, which is
  -- not produced here — see the layer notes at the top). Not for railways:
  -- rail route refs would otherwise render road shields.
  if not rail then
    local ref = way:get_tag("ref")
    if ref ~= "" then way:add_string("ref", ref) end
  end

  -- level information for the indoor overlay (same idioms as full.lua):
  -- indoor footpaths are filtered by `level`; stairs (kind=steps) connect two
  -- floors and are filtered by from_level/to_level.
  if kind == "steps" then
    local from_level, to_level = split_level(way:get_tag("level"))
    if from_level ~= nil then
      way:add_integer("from_level", from_level)
      way:add_integer("to_level", to_level)
    end
  else
    way:add_tag_as_integer("level")
  end
  return true
end

--------------------------------------------------------------------------------
-- water_lines (Shortbread layer "water_lines") + dam_lines
--------------------------------------------------------------------------------

local function process_water_lines(way)
  local kind = way:get_tag("waterway")
  local mz = INF
  if kind == "river" or kind == "canal" then
    mz = 9
  elseif kind == "drain" or kind == "stream" then
    mz = 13
  elseif kind == "ditch" then
    mz = 14
  end
  if mz >= INF then return false end

  way:set_target_layer("water_lines")
  way:set_approved_min(mz)
  way:add_string("kind", kind)
  way:add_bool("tunnel", is_tunnel(way))
  way:add_bool("bridge", is_bridge(way))
  set_names(way)
  return true
end

--------------------------------------------------------------------------------
-- ways
--------------------------------------------------------------------------------

function process_way(way)
  -- elevators (indoor overlay). Handled before the generic streets branch so a
  -- highway=elevator way lands in the `indoor` layer instead of `streets`.
  if way:has_tag("highway", "elevator") then
    way:set_target_layer("indoor")
    way:set_approved_min(16)
    way:add_string("indoor", "elevator")
    local from_level, to_level = split_level(way:get_tag("level"))
    if from_level ~= nil then
      way:add_integer("from_level", from_level)
      way:add_integer("to_level", to_level)
    end
    set_names(way)
    return
  end

  -- streets (roads / railways / aeroways as lines)
  if way:has_any_tag("highway") or way:has_any_tag("railway") or way:has_any_tag("aeroway") then
    if process_streets(way) then return end
  end

  -- water_lines / dam_lines
  if way:has_any_tag("waterway") then
    if way:has_tag("waterway", "dam") then
      way:set_target_layer("dam_lines")
      way:set_approved_min(12)
      way:add_string("kind", "dam")
      return
    end
    if process_water_lines(way) then return end
  end

  -- pier_lines
  local man_made = way:get_tag("man_made")
  if man_made == "pier" or man_made == "breakwater" or man_made == "groyne" then
    way:set_target_layer("pier_lines")
    way:set_approved_min(12)
    way:add_string("kind", man_made)
    return
  end

  -- aerialways
  local aerialway = way:get_tag("aerialway")
  if aerialway == "cable_car" or aerialway == "gondola" or aerialway == "chair_lift" or
     aerialway == "drag_lift" or aerialway == "t-bar" or aerialway == "j-bar" or
     aerialway == "platter" or aerialway == "rope_tow" then
    way:set_target_layer("aerialways")
    way:set_approved_min(12)
    way:add_string("kind", aerialway)
    return
  end

  -- ferries
  if way:has_tag("route", "ferry") then
    local mz = 10
    if way:get_tag("motor_vehicle") == "no" then mz = 12 end
    way:set_target_layer("ferries")
    way:set_approved_min(mz)
    way:add_string("kind", "ferry")
    set_names(way)
    return
  end

  -- boundaries (from tags on the way; relations are not available)
  if way:has_tag("boundary", "administrative") then
    local al = tonumber(way:get_tag("admin_level"))
    local mz = INF
    if al == 2 then
      mz = 0
    elseif al ~= nil and al <= 4 then
      mz = 7
    end
    if mz < INF then
      way:set_target_layer("boundaries")
      way:set_approved_min(mz)
      way:add_integer("admin_level", al)
      way:add_bool("maritime", way:get_tag("maritime") == "yes" or
                               way:get_tag("natural") == "coastline")
      way:add_bool("disputed", way:get_tag("disputed") == "yes")
      return
    end
  end
end

--------------------------------------------------------------------------------
-- water_polygons (Shortbread layer "water_polygons")
--------------------------------------------------------------------------------

local function process_water_polygons(area)
  local waterway = area:get_tag("waterway")
  local natural = area:get_tag("natural")
  local water = area:get_tag("water")
  local landuse = area:get_tag("landuse")
  local kind = ""
  local is_river = (natural == "water" and water == "river") or waterway == "riverbank"

  if landuse == "reservoir" or landuse == "basin" or
     (natural == "water" and not is_river) or natural == "glacier" then
    if landuse == "reservoir" or landuse == "basin" then
      kind = landuse
    else
      kind = natural
    end
  elseif is_river or waterway == "dock" or waterway == "canal" then
    if is_river then
      kind = "river"
    else
      kind = waterway
    end
  else
    return false
  end

  area:set_target_layer("water_polygons")
  -- Shortbread scales the min zoom by area (floor 4); approximate with the
  -- engine's area-based approval.
  area:set_approved_min_by_area(14, 1e6, 12, 1e7, 10, 1e9, 8, 1e10, 6, 1e11, 4, -1)
  area:add_string("kind", kind)
  -- feature size in m² so the style can gate labels (Shortbread carries this
  -- as way_area on the water_polygons_labels layer, which is not produced).
  area:add_numeric("way_area", area:get_area_m2())
  set_names(area)
  return true
end

--------------------------------------------------------------------------------
-- land (Shortbread layer "land")
--------------------------------------------------------------------------------

local function process_land(area)
  local landuse = area:get_tag("landuse")
  local natural = area:get_tag("natural")
  local wetland = area:get_tag("wetland")
  local leisure = area:get_tag("leisure")
  local kind = ""
  local mz = INF

  if landuse == "forest" or natural == "wood" then
    kind = "forest"; mz = 7
  elseif landuse == "residential" or landuse == "industrial" or landuse == "commercial" or
         landuse == "retail" or landuse == "railway" or landuse == "landfill" or
         landuse == "brownfield" or landuse == "greenfield" or landuse == "farmyard" or
         landuse == "farmland" then
    kind = landuse; mz = 10
  elseif landuse == "grass" or landuse == "meadow" or landuse == "orchard" or
         landuse == "vineyard" or landuse == "allotments" or landuse == "village_green" or
         landuse == "recreation_ground" or landuse == "greenhouse_horticulture" or
         landuse == "plant_nursery" or landuse == "quarry" then
    kind = landuse; mz = 11
  elseif natural == "sand" or natural == "beach" then
    kind = natural; mz = 10
  elseif natural == "heath" or natural == "scrub" or natural == "grassland" or
         natural == "bare_rock" or natural == "scree" or natural == "shingle" then
    kind = natural; mz = 11
  elseif wetland == "swamp" or wetland == "bog" or wetland == "string_bog" or
         wetland == "wet_meadow" or wetland == "marsh" then
    kind = wetland; mz = 11
  elseif area:get_tag("amenity") == "grave_yard" then
    kind = "grave_yard"; mz = 13
  elseif landuse == "garages" then
    kind = landuse; mz = 12
  elseif leisure == "golf_course" or leisure == "park" or leisure == "garden" or
         leisure == "playground" or leisure == "miniature_golf" then
    kind = leisure; mz = 11
  elseif landuse == "cemetery" then
    kind = "cemetery"; mz = 13
  end

  if mz >= INF then return false end

  area:set_target_layer("land")
  -- area-scaled approval exactly like the old full.lua landuse layer (instead
  -- of Shortbread's fixed per-kind min zooms): large areas (farmland, forests,
  -- ...) from z8, medium from z10, small ones from z14.
  area:set_approved_min_by_area(14, 1e8,
                                10, 1e10,
                                 8, -1)
  area:add_string("kind", kind)
  return true
end

--------------------------------------------------------------------------------
-- sites (Shortbread layer "sites")
--------------------------------------------------------------------------------

local function process_sites(area)
  local amenity = area:get_tag("amenity")
  local military = area:get_tag("military")
  local leisure = area:get_tag("leisure")
  local landuse = area:get_tag("landuse")
  local kind = ""

  if amenity == "university" or amenity == "hospital" or amenity == "prison" or
     amenity == "parking" or amenity == "bicycle_parking" or amenity == "school" or
     amenity == "college" then
    kind = amenity
  elseif leisure == "sports_center" then
    kind = leisure
  elseif landuse == "construction" then
    kind = landuse
  elseif military == "danger_area" then
    kind = military
  else
    return false
  end

  area:set_target_layer("sites")
  area:set_approved_min(14)
  area:add_string("kind", kind)
  return true
end

--------------------------------------------------------------------------------
-- street_polygons (Shortbread layer "street_polygons")
--------------------------------------------------------------------------------

local function process_street_polygons(area)
  local highway = area:get_tag("highway")
  local aeroway = area:get_tag("area:aeroway")
  local kind = ""
  local mz = INF

  if highway == "pedestrian" or highway == "service" then
    kind = highway; mz = 14
  elseif aeroway == "runway" then
    kind = aeroway; mz = 11
  elseif aeroway == "taxiway" then
    kind = aeroway; mz = 13
  else
    return false
  end

  area:set_target_layer("street_polygons")
  area:set_approved_min(mz)
  area:add_string("kind", kind)
  local surface = area:get_tag("surface")
  if surface ~= "" then area:add_string("surface", surface) end
  area:add_bool("tunnel", is_tunnel(area))
  area:add_bool("bridge", is_bridge(area))
  area:add_bool("rail", false)
  local service = area:get_tag("service")
  if service ~= "" then area:add_string("service", service) end
  return true
end

--------------------------------------------------------------------------------
-- indoor (MOTIS indoor overlay layer "indoor")
-- rooms / corridors / walls / areas carrying an `indoor` tag, plus elevators
-- (indoor=elevator or highway=elevator). Filtered by `level`; elevators by
-- from_level/to_level.
--------------------------------------------------------------------------------

local function process_indoor(area)
  local indoor = area:get_tag("indoor")
  if indoor == "" and area:get_tag("highway") == "elevator" then
    indoor = "elevator"
  end
  if indoor == "" or indoor == "no" then return false end

  area:set_target_layer("indoor")
  area:add_string("indoor", indoor)
  area:add_tag_as_string("name")
  area:add_tag_as_string("shop")

  -- level info identical to full.lua: rooms carry `level`; elevators connect
  -- floors and carry from_level/to_level.
  if indoor == "elevator" then
    area:set_approved_min(16)
    local from_level, to_level = split_level(area:get_tag("level"))
    if from_level ~= nil then
      area:add_integer("from_level", from_level)
      area:add_integer("to_level", to_level)
    end
  else
    area:set_approved_min(17)
    area:add_tag_as_integer("level")
  end
  return true
end

--------------------------------------------------------------------------------
-- transit platforms -> `land` layer, kind=public_transport (the MOTIS indoor
-- overlay draws these and filters them by `level`). Shortbread's own
-- public_transport layer only carries station/stop points, not platform areas.
--------------------------------------------------------------------------------

local function process_platform(area)
  local pt = area:get_tag("public_transport")
  local railway = area:get_tag("railway")
  local highway = area:get_tag("highway")
  if pt ~= "platform" and pt ~= "stop_area" and
     railway ~= "platform" and highway ~= "platform" then
    return false
  end

  area:set_target_layer("land")
  area:set_approved_min(14)
  area:add_string("kind", "public_transport")
  area:add_tag_as_integer("level")
  set_names(area)
  return true
end

--------------------------------------------------------------------------------
-- areas
--------------------------------------------------------------------------------

function process_area(area)
  -- buildings
  if area:has_any_tag("building") and not area:has_tag("building", "no") then
    area:set_target_layer("buildings")
    area:set_approved_min(14)
    return
  end

  -- dam_polygons
  if area:has_tag("waterway", "dam") then
    area:set_target_layer("dam_polygons")
    area:set_approved_min(12)
    area:add_string("kind", "dam")
    return
  end

  -- pier_polygons
  local man_made = area:get_tag("man_made")
  if man_made == "pier" or man_made == "breakwater" or man_made == "groyne" then
    area:set_target_layer("pier_polygons")
    area:set_approved_min(12)
    area:add_string("kind", man_made)
    return
  end

  -- bridges
  if man_made == "bridge" then
    area:set_target_layer("bridges")
    area:set_approved_min(12)
    area:add_string("kind", "bridge")
    return
  end

  -- transit platforms (land kind=public_transport, carries a level). Checked
  -- BEFORE indoor (like the old full.lua) so platforms that also carry indoor
  -- tags (e.g. underground S-Bahn platforms) still render as platforms.
  if process_platform(area) then return end

  -- indoor rooms / corridors / elevators (indoor overlay). Checked before
  -- pois/land so an indoor room tagged e.g. amenity=cafe still lands in the
  -- `indoor` layer with its level.
  if process_indoor(area) then return end

  -- water_polygons
  if process_water_polygons(area) then return end

  -- land
  if process_land(area) then return end

  -- sites
  if process_sites(area) then return end

  -- street_polygons
  if process_street_polygons(area) then return end

  -- public_transport (rendered as polygon; a symbol style centroids it)
  if process_public_transport(area) then return end

  -- pois
  if process_pois(area) then return end

  -- addresses
  local housenumber = area:get_tag("addr:housenumber")
  local housename = area:get_tag("addr:housename")
  if housenumber ~= "" or housename ~= "" then
    area:set_target_layer("addresses")
    area:set_approved_min(14)
    set_address(area)
  end
end
