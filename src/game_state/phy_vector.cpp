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


int PhyVector::x_part()
{
  return int(x) / PARTITION_SIZE;
}

int PhyVector::y_part()
{
  return int(y) / PARTITION_SIZE;
}


