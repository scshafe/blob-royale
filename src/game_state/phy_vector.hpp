#ifndef _PHY_VECTOR_HPP_
#define _PHY_VECTOR_HPP_

#include <ostream>

#include "boost-json.hpp"


class PhyVector
{
public:
  float x;
  float y;

  float get_x();
  float get_y();

  PhyVector();
  //PhyVector(const float& x_in, const float& y_in);
  PhyVector(const float x_in, const float y_in);
  //PhyVector(const float x_in, const float y_in);
  ~PhyVector();
  

  PhyVector& operator=(const PhyVector& other);
  friend std::ostream& operator<<(std::ostream& os, PhyVector const & pv);
  PhyVector operator-(const PhyVector b);
  PhyVector operator*(const float b);
  PhyVector operator/(const float b);
  PhyVector operator+(const PhyVector b);

  int x_part();
  int y_part();

  float get_magnitude();
  boost::json::array getPhyVectorJson();
  PhyVector get_inverse();
  float dot(const PhyVector& other);
  PhyVector normalize();

};

#endif
