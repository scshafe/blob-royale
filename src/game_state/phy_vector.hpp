#ifndef _PHY_VECTOR_HPP_
#define _PHY_VECTOR_HPP_

#include <ostream>

#include "boost-json.hpp"


class PhyVector
{
public:
  float x;
  float y;


  PhyVector();
  PhyVector(const float& x_in, const float& y_in);
  //PhyVector(const float x_in, const float y_in);
  ~PhyVector();

  PhyVector& operator=(const PhyVector& other);
  friend std::ostream& operator<<(std::ostream& os, PhyVector const & pv);

  int x_part();
  int y_part();

  float get_magnitude();
  boost::json::array getPhyVectorJson();
  PhyVector get_inverse();
  float dot_product(const PhyVector& other);
};


#endif
