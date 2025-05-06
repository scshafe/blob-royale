#include <cmath>

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

PhyVector::~PhyVector() {}


std::ostream& operator<<(std::ostream& os, PhyVector const & pv)
{
  return os << "(x: " << pv.x << ", y: " << pv.y << ")";
}

int PhyVector::x_part()
{
  return int(x) / PARTITION_SIZE;
}

int PhyVector::y_part()
{
  return int(y) / PARTITION_SIZE;
}

float PhyVector::get_magnitude() {
  return sqrt(pow(x, 2.0) + pow(y, 2.0));
}


boost::json::array PhyVector::getPhyVectorJson()
{
  return {x, y};
}
