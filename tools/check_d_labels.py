"""Differential oracle for sparse labels, wraparound probes and generations."""
import argparse
from pathlib import Path
import subprocess
from verify import ROOT

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT / 'track-d/main.cpp')
args = parser.parse_args()
source = args.source.read_text()
start = source.index('struct SearchState {')
end = source.index('struct alignas(16) Node', start)
program = r'''
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <immintrin.h>
#include <random>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>
''' + source[start:end] + r'''
int main(){
 constexpr uint32_t n=1740512;SearchState state(n);std::mt19937_64 rng(519013);
 std::vector<uint32_t> tails;
 for(size_t capacity=1024;capacity<=65536;capacity*=2){
  unsigned found=0;
  for(uint32_t u=0;u<n && found<6;++u)
   if((SearchState::hash(u)&(capacity-1))>=capacity-3){tails.push_back(u);++found;}
 }
 size_t operations=0;
 for(unsigned round=0;round<2050;++round){
  state.clear();std::unordered_map<uint32_t,uint64_t> oracle;
  auto update=[&](uint32_t u,uint64_t value){
   auto it=oracle.find(u);bool fresh=it==oracle.end()||value<it->second;
   if(fresh)oracle[u]=value;
   if(state.relax(u,value)!=fresh)throw std::runtime_error("relax result");
   if(state.get(u)!=oracle.at(u)||state.cross(u)!=oracle.at(u))throw std::runtime_error("stored distance");
   ++operations;
  };
  for(auto u:tails)update(u,(rng()%2)?UINT32_MAX+uint64_t{1}:rng()%1000);
  for(unsigned i=0;i<(round<3?30000:240);++i){
   uint32_t u=rng()%n;uint64_t value=i%7==0?rng()%100:(rng()&((uint64_t{1}<<52)-1));
   update(u,value);
   if(i%5==0)update(u,value?value-1:0);
   uint32_t absent=rng()%n;auto it=oracle.find(absent);uint64_t expected=it==oracle.end()?UINT64_MAX:it->second;
   if(state.get(absent)!=expected||state.cross(absent)!=expected)throw std::runtime_error("lookup");
  }
  for(auto [u,d]:oracle)if(state.get(u)!=d||state.cross(u)!=d)throw std::runtime_error("final lookup");
 }
 std::cout<<operations<<" label updates passed; grow, wraparound, wide values, generation reuse\n";
}
'''
folder = ROOT / '.build'
folder.mkdir(exist_ok=True)
cpp, binary = folder / 'check-d-labels.cpp', folder / 'check-d-labels'
cpp.write_text(program)
subprocess.run(['g++', '-std=c++20', '-O3', '-march=x86-64-v3', str(cpp), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
