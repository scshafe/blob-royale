#ifndef _PARTITION_HPP_
#define _PARTITION_HPP_

#include <vector>
#include "game_piece.hpp"

class GamePiece;

class Partition {
private:
  std::vector<GamePiece*> pieces;

public:
  static size_t get_partition_index(const GamePiece* game_piece) { return 1; }
  void add_game_piece(GamePiece* game_piece);
  void remove_game_piece(GamePiece* game_piece);
  std::vector<GamePiece*> get_game_pieces();
};


#endif
