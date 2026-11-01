"""Differentially test the submission queue against an ordered reference."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from verify import ROOT

parser = argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=ROOT/'track-d/main.cpp')
source = parser.parse_args().source.read_text()
queue = source[source.index('using Item ='):source.index('// The state grows')]
test = r'''
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <vector>
QUEUE
int main(){
 std::mt19937_64 rng(930428);RadixHeap queue;
 uint32_t serial=0;size_t checked=0;
 for(unsigned epoch=0;epoch<5;++epoch){
  queue.clear();std::map<uint64_t,std::set<uint32_t>> oracle;
  uint64_t last=0;size_t pending=0;
  auto pop=[&]{
   uint64_t wanted=oracle.begin()->first;
   if(queue.minimum()!=wanted)throw std::runtime_error("minimum differs");
   auto [key,node]=queue.pop();auto it=oracle.find(key);
   if(key!=wanted||it==oracle.end()||!it->second.erase(node))throw std::runtime_error("item differs");
   if(it->second.empty())oracle.erase(it);last=key;--pending;++checked;
  };
  for(size_t i=0;i<100000;++i){
   if(oracle.empty()||rng()%3){
    const std::array<uint64_t,12> gaps={0,1,255,256,1023,1024,4095,4096,UINT32_MAX,uint64_t(UINT32_MAX)+1,uint64_t{1}<<40,rng()%8192};
    uint64_t key=last+gaps[rng()%gaps.size()];queue.push(key,serial);oracle[key].insert(serial++);++pending;
   }else pop();
   if(queue.count!=pending)throw std::runtime_error("count differs");
  }
  if(epoch%2)while(!oracle.empty())pop();
 }
 std::cout<<"D queue: "<<checked<<" reference pops passed; wide keys, wraps, duplicates and resets\n";
}
'''
with tempfile.TemporaryDirectory() as folder:
    path, binary = Path(folder)/'queue.cpp', Path(folder)/'queue'
    path.write_text(test.replace('QUEUE', queue))
    subprocess.run(['g++','-O2','-std=c++20',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
