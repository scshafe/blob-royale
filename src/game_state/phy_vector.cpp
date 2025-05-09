#include <cmath>

#include "game_engine_parameters.hpp"
#include "phy_vector.hpp"


PhyVector::PhyVector()
{
  x = 1.0;
  y = 2.0;
}

PhyVector::PhyVector(const float& x_in, const float& y_in)
{
  x = x_in;
  y = y_in;
}

//PhyVector::PhyVector(const float x_in, const float y_in)
//{
//  x = x_in;
//  y = y_in;
//}

PhyVector::~PhyVector() {}

PhyVector& PhyVector::operator=(const PhyVector& other)
{
  if (this == &other) return *this;

  return *this;
}

std::ostream& operator<<(std::ostream& os, PhyVector const & pv)
{
  return os << "(x: " << pv.x << ", y: " << pv.y << ")";
}

float PhyVector::get_magnitude() {
  return sqrt(pow(x, 2.0) + pow(y, 2.0));
}


boost::json::array PhyVector::getPhyVectorJson()
{
  return {x, y};
}

PhyVector PhyVector::get_inverse()
{  
  return PhyVector(-x, -y);
}

float PhyVector::dot_product(const PhyVector& other)
{
  return x * other.x + y * other.y;
}



