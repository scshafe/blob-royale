#ifndef _MY_BOOST_PROGRAM_OPTIONS_HPP_
#define _MY_BOOST_PROGRAM_OPTIONS_HPP_


// --------- PROGRAM OPTIONS

#include <boost/program_options.hpp>

namespace po = boost::program_options;



po::variables_map handle_configuration(int argc, char** argv);


#endif
