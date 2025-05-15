#include "map_object.hpp"




MapObject::MapObject(const int& id_, const std::vector<std::string>& row_) :
  GamePiece(id_, row_)
{
}





void MapObject::update_partitions()
{
  /* if velocity == 0
   *    find_static_parititions()
   *    move_to_stationary()
   * else
   *    GamePiece::update_partitions()
   */

}




bool MapObject::is_stationary()
{
  return false;
}
