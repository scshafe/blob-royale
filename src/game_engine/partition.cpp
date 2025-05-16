

#include "boost-log.hpp"

#include "game_engine_parameters.hpp"
#include "partition.hpp"
#include "game_piece.hpp"


// ---------- CELL -----------

Cell::Cell()
{
  ERROR << "Cell() constructor should not be called";
  exit(1);
}

// for finding what Partition a GamePiece is in
Cell::Cell(std::shared_ptr<GamePiece> gp)
{
  _row = floor(gp->get_position().y / PARTITION_HEIGHT);
  _col = floor(gp->get_position().x / PARTITION_WIDTH);
}

// for building partiitons
Cell::Cell(const size_t& row, const size_t& col)
{
  _row = row;
  _col = col;
}

Cell::Cell(const float& x, const float& y)
{
  _row = floor(y / PARTITION_HEIGHT);
  _col = floor(x / PARTITION_WIDTH);
}

Cell::Cell(const GamePiece& gp)
{
  _row = floor(gp.get_position().y / PARTITION_HEIGHT);
  _col = floor(gp.get_position().x / PARTITION_WIDTH);
}

const size_t Cell::row()
{
  return _row;
}

const size_t Cell::col()
{
  return _col;
}

boost::json::array Cell::getCellJson()
{
  return boost::json::array({_row, _col});
}

std::ostream& operator<<(std::ostream& os, const Cell& c)
{
  return os << "[" << c._row << "," << c._col << "]";
}

bool Cell::operator==(const Cell& other)
{
  return _row == other._row && _col == other._col;
}
bool Cell::operator<(const Cell& other)
{
  if (_row < other._row) return true;
  if (_row > other._row) return false;
  return _row < other._row;
}


// ---------- PARTITION ------------


Partition::Partition(const size_t& row, const size_t& col) :
  c(row, col),
  pieces(std::unordered_set<std::shared_ptr<GamePiece>>())
{}


void Partition::add_game_piece(std::shared_ptr<GamePiece> game_piece)
{
  m.lock();
  ENTRANCE << "Partition::add_game_piece()";
  pieces.insert(game_piece);
  TRACE << *this << "now has " << pieces.size() << " pieces";
  m.unlock();
}

void Partition::remove_game_piece(std::shared_ptr<GamePiece> game_piece)
{
  m.lock();
  ENTRANCE << "Partition::remove_game_piece()";
  pieces.erase(game_piece);
  m.unlock();
}



const std::unordered_set<std::shared_ptr<GamePiece>>& Partition::get_pieces()
{
  return pieces;
}



boost::json::array Partition::getPartJson()
{
  return c.getCellJson();
}

std::ostream& operator<<(std::ostream& os, const Partition& p)
{
  return os << "Partition: " << p.c;
}

void Partition::print_gp_list()
{
  TRACE << "Partition " << *this << " gamepieces:";
  for (auto it = pieces.begin(); it != pieces.end(); ++it)
  {
    if (*it == nullptr)
    {
      ERROR << "INVALID GP";
      exit(1);
    }
    TRACE << **it;
  }
}

bool Partition::operator==(const Partition& other)
{
  return c == other.c;
}


bool Partition::operator<(const Partition& other)
{
  return c < other.c;
}


