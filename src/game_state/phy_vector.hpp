#ifndef _PHY_VECTOR_HPP_
#define _PHY_VECTOR_HPP_

#include <ostream>

#define PARTITION_SIZE 20

class PhyVector
{
public:
  float x;
  float y;


  PhyVector();
  PhyVector(const float& x_in, const float& y_in);
  ~PhyVector();

  friend std::ostream& operator<<(std::ostream& os, PhyVector const & pv);

  int x_part();
  int y_part();

  float get_magnitude();
};


#endif
