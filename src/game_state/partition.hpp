#ifndef _PARTITION_HPP_
#define _PARTITION_HPP_

#include <unordered_set>
#include <memory>
#include <ostream>
#include "game_piece.hpp"





class GamePiece;

class Partition {
public:
  Partition();

  int id;
  std::unordered_set<GamePiece*> pieces;

public:
  void add_game_piece(GamePiece* game_piece);
  void remove_game_piece(GamePiece* game_piece);
  const std::unordered_set<GamePiece*> get_game_pieces();
  


};


#endif
