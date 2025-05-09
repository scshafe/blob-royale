
#include "boost-log.hpp"

#include "game_engine_parameters.hpp"
#include "partition.hpp"
#include "game_piece.hpp"

Partition::Partition() :
  pieces(std::unordered_set<GamePiece*>(0))
{}

void Partition::add_game_piece(GamePiece* game_piece)
{
  ENTRANCE << "Partition::add_game_piece()";
  pieces.insert(game_piece);
}

void Partition::remove_game_piece(GamePiece* game_piece)
{
  ENTRANCE << "Partition::remove_game_piece()";
  pieces.erase(game_piece);
}

const std::unordered_set<GamePiece*> Partition::get_game_pieces()
{
  return pieces;
}
