// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon/capture.hpp"
#include <iostream>
int main(int argc,char** argv) {
  if(argc!=2||std::string(argv[1])=="--help") {std::cout<<"Usage: rkmoon-v4l2-probe /dev/videoN\nRead QUERYCAP, QUERY_DV_TIMINGS, G_FMT only. No REQBUFS/STREAMON/S_FMT/S_DV_TIMINGS.\n";return argc==2?0:2;}
  try{rkmoon::Capture capture(argv[1]);capture.describe();return 0;}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
