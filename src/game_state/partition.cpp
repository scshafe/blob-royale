#include "partition.hpp"



//Partition::Partition() {}
//Partition::~Partition() {}


//static size_t Partition::get_partition_index(const GamePiece* game_piece)
//{
//  return 1;
//}

void Partition::add_game_piece(GamePiece* game_piece)
{
  pieces.push_back(game_piece);
  game_piece->add_partition(this);
}

void Partition::remove_game_piece(GamePiece* game_piece)
{
  game_piece->remove_partition(this);
  for (int i = 0; i < pieces.size(); i++)
  {
    if (game_piece == pieces[i])
    {
      pieces.erase(pieces.begin() + i);
      break;
    }
  }
}

std::vector<GamePiece*> Partition::get_game_pieces()
{
  return pieces;
}
