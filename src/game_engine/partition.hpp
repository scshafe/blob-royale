#ifndef _PARTITION_HPP_
#define _PARTITION_HPP_

#include <unordered_set>
#include <memory>
#include <ostream>
#include <mutex>

#include "boost-json.hpp"

#include "game_piece.hpp"

class GamePiece;


class Cell
{
  size_t _row;
  size_t _col; 

public:
  Cell();
  Cell(const size_t& row, const size_t& col);
  Cell(std::shared_ptr<GamePiece> gp);
  Cell(const float& x, const float& y);
  Cell(const GamePiece& gp);
  boost::json::array getCellJson();

  const size_t row();
  const size_t col();

  friend std::ostream& operator<<(std::ostream& os, const Cell& c);

  bool operator==(const Cell& other);
  bool operator<(const Cell& other);
  

};



// -------- PARTITION --------


class Partition {
public:
  Partition(const size_t& row, const size_t& col);
  
  static std::shared_ptr<Partition> buildPartition(const size_t& row, const size_t& col)
  {
    return std::make_shared<Partition>(row, col);
  }

  Cell c;
  std::unordered_set<std::shared_ptr<GamePiece>> pieces;
  mutex m;

  const std::unordered_set<std::shared_ptr<GamePiece>>& get_pieces();

public:
  void add_game_piece(std::shared_ptr<GamePiece> game_piece);
  void remove_game_piece(std::shared_ptr<GamePiece> game_piece);
  void check_for_collisions(std::shared_ptr<GamePiece> gp);
  
  boost::json::array getPartJson();

  friend std::ostream& operator<<(std::ostream& os, const Partition& p);
  void print_gp_list();

  bool operator==(const Partition& other);
  bool operator<(const Partition& other);

};


#endif
