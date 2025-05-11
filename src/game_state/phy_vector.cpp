#include <cmath>

#include "game_engine_parameters.hpp"
#include "phy_vector.hpp"


#include "boost-log.hpp"

float PhyVector::get_x()
{
  return x;
}

float PhyVector::get_y()
{
  return y;
}


PhyVector::PhyVector()
{
  x = 1.0;
  y = 2.0;
}



//PhyVector::PhyVector(const float& x_in, const float& y_in)
//{
//  x = x_in;
//  y = y_in;
//}

PhyVector::PhyVector(const float x_in, const float y_in)
{
  x = x_in;
  y = y_in;
}

PhyVector::~PhyVector() {}

PhyVector& PhyVector::operator=(const PhyVector& other)
{
  if (this == &other) return *this;
  x = other.x;
  y = other.y;
  return *this;
}

std::ostream& operator<<(std::ostream& os, PhyVector const & pv)
{
  return os << "(" << pv.x << "," << pv.y << ")";
}

float PhyVector::get_magnitude() {
  return sqrt(pow(x, 2.0) + pow(y, 2.0));
}


boost::json::array PhyVector::getPhyVectorJson()
{
  return {int(x), int(y)};
}

PhyVector PhyVector::get_inverse()
{ 
  PhyVector tmp(-x, -y);
  TRACE << "created inverse:" << *this << " --> " << tmp;
  return tmp;
}

float PhyVector::dot(const PhyVector& other)
{
  return x * other.x + y * other.y;
}

PhyVector PhyVector::normalize()
{
  return PhyVector(x / get_magnitude(), y / get_magnitude());
}


PhyVector PhyVector::operator-(const PhyVector b)
{
  return PhyVector(x - b.x, y - b.y);
}


PhyVector PhyVector::operator+(const PhyVector b)
{
  return PhyVector(x + b.x, y + b.y);
}


PhyVector PhyVector::operator*(const float b)
{
  return PhyVector(x * b, y * b);
}

PhyVector PhyVector::operator/(const float b)
{
  return PhyVector(x / b, y / b);
}
