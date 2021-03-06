// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.
//
#include "map.h"
#include "time.h"

bool RTSMap::find_two_nearby_empty_slots(const std::function<uint16_t(int)>& f,
                                         int* x1,
                                         int* y1,
                                         int* x2,
                                         int* y2,
                                         int i) const {
  const int kDist = 4;
  int kMaxTrial = 100;
  for (int j = 0; j < kMaxTrial; ++j) {
    *x1 = float(f(GetXSize())) / 3 + i * float(GetXSize()) / 3 * 2;
    *y1 = float(f(GetYSize())) / 3 + i * float(GetYSize()) / 3 * 2;
    if (!CanPass(Coord(*x1, *y1), INVALID))
      continue;

    *x2 = f(2 * kDist + 1) - kDist + *x1;
    *y2 = f(2 * kDist + 1) - kDist + *y1;
    if (CanPass(Coord(*x2, *y2), INVALID) && ((*x1 != *x2) || (*y1 != *y2)))
      return true;
  }
  return false;
}

void RTSMap::InitMap(int m, int n, int level) {
  _m = m;
  _n = n;
  _level = level;
  _map.assign(m * n * level, MapSlot());
}

bool RTSMap::GenerateImpassable(const std::function<uint16_t(int)>& f,
                                int nImpassable) {
  _map.assign(_m * _n * _level, MapSlot());
  for (int i = 0; i < nImpassable; ++i) {
    const int x = f(_m);
    const int y = f(_n);
    _map[GetLoc(Coord(x, y))].type = ROCK;
  }
  return true;
}

bool RTSMap::LoadMap(const std::string& filename) {
  (void)filename;
  return true;
}

bool RTSMap::GenerateMap(const std::function<uint16_t(int)>& f,
                         int nImpassable,
                         int num_player,
                         int init_resource) {
  // load a map for now simple format.
  bool success;
  do {
    success = true;
    GenerateImpassable(f, nImpassable);
    int x1 = -1, y1 = -1, x2 = -1, y2 = -1;
    _infos.clear();
    for (PlayerId i = 0; i < num_player; ++i) {
      if (!find_two_nearby_empty_slots(f, &x1, &y1, &x2, &y2, i)) {
        std::cout << "player " << i << " (" << x1 << ", " << y1 << "), (" << x2
                  << ", " << y2 << ") failed" << std::endl;
        success = false;
        break;
      }
      PlayerMapInfo info;
      info.player_id = i;
      info.base_coord = Coord(x1, y1);
      info.resource_coord = Coord(x2, y2);
      info.initial_resource = init_resource;
      _infos.emplace_back(info);
      // cout << "Player " << i << ": TOWN_HALL: (" << x1 << ", " << y1 << ")
      // RESOURCE: (" << x2 << ", " << y2 << ")" << endl;
    }
  } while (!success);

  ResetIntermediates();
  return true;
}

void RTSMap::ResetIntermediates() {
  // Locality Search
  _locality =
      LocalitySearch<UnitId>(PointF(-0.5, -0.5), PointF(_m + 0.5, _n + 0.5));

  // Precompute map structure.
  precompute_all_pair_distances();
}

void RTSMap::AddPlayer(int player_id,
                       int base_x,
                       int base_y,
                       int resource_x,
                       int resource_y,
                       int init_resource) {
  _infos.push_back({player_id,
                    {base_x, base_y, 0},
                    {resource_x, resource_y, 0},
                    init_resource});
}

void RTSMap::load_default_map() {
  _m = 20;
  _n = 20;
  _level = 1;
  _map.assign(_m * _n * _level, MapSlot());
}

void RTSMap::precompute_all_pair_distances() {
  // All-pair shortest distance for path-planning.
  // Floyd–Warshall algorithm O(V^3) = O(m^3n^3)
  // Not extremely fast, but since it is only computed
  // once for each map, we could just use it.
}

bool RTSMap::AddUnit(const UnitId& id, const PointF& new_p) {
  if (_locality.Exists(id))
    return false;
  if (!_locality.IsEmpty(new_p, kUnitRadius, INVALID))
    return false;

  _locality.Add(id, new_p, kUnitRadius);
  return true;
}

bool RTSMap::MoveUnit(const UnitId& id, const PointF& new_p) {
  if (!_locality.Exists(id)) {
    return false;
  }
  if (!_locality.IsEmpty(new_p, kUnitRadius, id)) {
    return false;
  }

  _locality.Remove(id);
  _locality.Add(id, new_p, kUnitRadius);
  return true;
}

bool RTSMap::RemoveUnit(const UnitId& id) {
  if (!_locality.Exists(id))
    return false;
  _locality.Remove(id);
  return true;
}

UnitId RTSMap::GetClosestUnitId(const PointF& p, float max_r) const {
  float dist_sqr;
  const UnitId* res = _locality.Loc2Key(p, &dist_sqr);
  if (res == nullptr || dist_sqr >= max_r * max_r)
    return INVALID;
  return *res;
}

std::set<UnitId> RTSMap::GetUnitIdInRegion(const PointF& left_top,
                                           const PointF& right_bottom) const {
  return _locality.KeysInRegion(left_top, right_bottom);
}

bool RTSMap::CanSee(const Coord& s, const Coord& t) const {
  const float dx = t.x - s.x;
  const float dy = t.y - s.y;
  float r = sqrt(dx * dx + dy * dy);
  auto p = PointF(s);
  while (true) {
    const auto c = p.ToCoord();
    if (c == t)
      break;
    if (c != s && IsIn(c)) {
      const auto terrain = _map[GetLoc(c)].type;
      if (terrain == ROCK) {
        return false;
      }
    }
    p.x += 0.5 * dx / r;
    p.y += 0.5 * dy / r;
  }
  return true;
}

std::string RTSMap::PrintCoord(Loc loc) const {
  // Print the coordinate.
  Coord coord = GetCoord(loc);

  std::stringstream ss;
  ss << "(" << coord.x << "," << coord.y << "," << coord.z << ")";
  return ss.str();
}

Coord RTSMap::GetCoord(Loc loc) const {
  int xy = loc % (_m * _n);
  int z = loc / (_m * _n);
  return Coord(xy % _m, xy / _m, z);
}

Loc RTSMap::GetLoc(const Coord& c) const {
  return (c.z * _n + c.y) * _m + c.x;
}

Loc RTSMap::GetLoc2d(int x, int y) const {
  // assume z = 0;
  return y * _m + x;
}

// Draw the map
std::string RTSMap::Draw() const {
  std::stringstream ss;
  ss << "m " << _m << " " << _n << " " << std::endl;
  for (int j = 0; j < _n; ++j) {
    for (int i = 0; i < _m; ++i) {
      // Draw the map (only level 0)
      Loc loc = GetLoc(Coord(i, j, 0));
      ss << _map[loc].type << " ";
    }
    ss << std::endl;
  }
  return ss.str();
}

std::string RTSMap::PrintDebugInfo() const {
  return _locality.PrintDebugInfo();
}
