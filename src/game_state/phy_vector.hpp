#ifndef _PHY_VECTOR_HPP_
#define _PHY_VECTOR_HPP_


#define PARTITION_SIZE 20

class PhyVector
{
public:
  float x;
  float y;


  PhyVector();
  PhyVector(const float& x_in, const float& y_in);
  ~PhyVector();

  int x_part();
  int y_part();
};


#endif
