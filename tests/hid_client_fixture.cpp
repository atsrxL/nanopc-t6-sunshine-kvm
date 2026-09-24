// SPDX-License-Identifier: GPL-3.0-or-later
// Offline transport fixture, NEVER part of the production worker or a USB device backend.
#include "rkmoon/hid_client.hpp"
#include <chrono>
#include <string>
#include <thread>
#include <unistd.h>
int main(int argc,char** argv){
  using namespace std::chrono_literals;
  if(argc!=3)return 2;
  try {
    if(rkmoon::absolute_coordinate(0,2560)!=-32768||rkmoon::absolute_coordinate(2560,2560)!=32767||
       rkmoon::absolute_coordinate(1280,2560)!=0||rkmoon::absolute_coordinate(-10,1440)!=-32768||
       rkmoon::absolute_coordinate(1500,1440)!=32767)return 4;
    try{rkmoon::absolute_coordinate(0,0);return 5;}catch(const std::invalid_argument&){}
    bool absolute=std::string(argv[2])=="absolute";
    rkmoon::HidClient client(argv[1],absolute);
    client.submit({rkmoon::HidEvent::key,65,0,true});
    std::this_thread::sleep_for(100ms);
    if(std::string(argv[2])=="abrupt")_exit(0); // Explicitly test EOF without C++ destruction.
    if(std::string(argv[2])=="wrong-mode"){
      if(client.submit({rkmoon::HidEvent::absolute,0,0,false})||client.healthy())return 6;
      return 0; // EOF must release the held key, with no absolute event transmitted.
    }
    if(absolute){
      client.submit({rkmoon::HidEvent::absolute,-32768,32767,false});
      client.submit({rkmoon::HidEvent::button,1,0,true});
      client.submit({rkmoon::HidEvent::absolute,32767,-32768,false});
    }else client.submit({rkmoon::HidEvent::move,256,-128,false});
    client.submit({rkmoon::HidEvent::wheel,0,120,false});
    std::this_thread::sleep_for(700ms); // Actual C++ heartbeats must preserve a held key.
    if(!client.healthy())return 3;
    client.submit({rkmoon::HidEvent::key,65,0,false});
    std::this_thread::sleep_for(100ms);
    return 0;
  }catch(...){return 1;}
}
