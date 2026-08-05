#ifndef _MAP_OBJECT_HPP_
#define _MAP_OBJECT_HPP_

#include "game_piece.hpp"

class MapObject : public GamePiece
{
public:
  MapObject(const int& id_, const std::vector<std::string>& row_);
  void update_partitions();
  bool is_stationary() override;

private:
};

#endif
