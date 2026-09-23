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
    rkmoon::HidClient client(argv[1]);
    client.submit({rkmoon::HidEvent::key,65,0,true});
    std::this_thread::sleep_for(100ms);
    if(std::string(argv[2])=="abrupt")_exit(0); // Explicitly test EOF without C++ destruction.
    client.submit({rkmoon::HidEvent::move,256,-128,false});
    client.submit({rkmoon::HidEvent::wheel,0,120,false});
    std::this_thread::sleep_for(700ms); // Actual C++ heartbeats must preserve a held key.
    if(!client.healthy())return 3;
    client.submit({rkmoon::HidEvent::key,65,0,false});
    std::this_thread::sleep_for(100ms);
    return 0;
  }catch(...){return 1;}
}
