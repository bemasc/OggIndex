#include <ogg/ogg.h>
#include <vector>
#include <cmath>
#include <algorithm> //just for max()
#include <assert.h>

using namespace std;

/* This file contains methods related to reading and writing Golomb-Rice codes*/

ogg_int64_t rice_bits_required(ogg_int64_t value,
                                      unsigned char rice_param) {
  ogg_int64_t cutoff=1<<rice_param, bits=1+rice_param;
  return bits + value/cutoff;
}

// Returns the number of bytes needed to store n bits.
ogg_int64_t tobytes(ogg_int64_t n) {
  return (n+7)/8;
}

ogg_int64_t rice_total_bits(vector<ogg_int64_t>* values,
                                    unsigned char rice_param) {
  ogg_int64_t total=0, i=0;
  for(;i < values->size(); i++) {
    total += rice_bits_required(values->at(i), rice_param);
  }
  return total;
}

// This algorithm comes from
// "Selecting the Golomb Parameter in Rice Coding"
// http://ipnpr.jpl.nasa.gov/progress_report/42-159/159E.pdf
unsigned char optimal_rice_parameter(vector<ogg_int64_t>* values) {
  ogg_int64_t total=0, cost, bestcost, i;
  float mean;
  unsigned char lower_bound, upper_bound, optimal, j;
  for(i=0; i < values->size(); i++) {
    total += values->at(i);
  }
  mean = ((float)total)/values->size();
  lower_bound = max(0, (int)floor(log((2.0/3)*(mean+1))/log(2.0)));
  upper_bound = max(0, (int)ceil(log(mean)/log(2.0)));
  if (upper_bound > lower_bound) {
    // There is a mathematical guarantee that upper_bound-lower_bound <= 2,
    // but floating point error may break this so the code is fully general.
    bestcost = rice_total_bits(values, upper_bound);
    optimal = upper_bound;
    for(j=lower_bound; j<upper_bound; j++){
      cost = rice_total_bits(values,j);
      if (cost < bestcost){
        bestcost = cost;
        optimal = j;
      }
    }
    return optimal;
  } else {
    //upper_bound == lower_bound, unless there's been some error due to
    //floating point imprecision.
    return upper_bound;
  }
}

// This function encodes value according to rice_param and appends the
// result to bitstore. 
void rice_write_one(vector<char>* bitstore,
                            ogg_int64_t value, unsigned char rice_param) {
  ogg_int64_t cutoff=1<<rice_param;
  while (value >= cutoff) {
    bitstore->push_back(true);
    value -= cutoff;
  }
  bitstore->push_back(false);
  rice_param--;
  while (rice_param >= 0) {
    bitstore->push_back((bool)(value&(1<<rice_param)));
    rice_param--;
  }
}

// Read one value from the rice-coded stream. Return the value, and
// leave the iterator pointing to the first bit of the next value.
ogg_int64_t rice_read_one(vector<char>::iterator it,
                                  unsigned char rice_param) {
  ogg_int64_t output=0;
  ogg_int64_t cutoff = 1<<rice_param;
  while(*it){
    output += cutoff;
    ++it;
  }
  while (rice_param > 0){
    rice_param--;
    ++it;
    output += (*it)<<rice_param;
  }
  ++it; //leave the iterator pointing to the first bit of the next value
  return output;
}

// Read 8*n bits out of p and write them into the elements of bits
void expand_bytes(vector<char>* bits, 
                               unsigned char* p, ogg_int64_t n) {
  for(int i=0;i<n;i++){
    for(int j=7;j>=0;j--){
      bits->push_back((*(p+i))&(1<<j));
    }
  }
}

// Write 8 bits into each char of p.  p must have enough space
// allocated to fit all of bits.  Unused bits in the final byte will
// be set to zero.
void squeeze_bits(unsigned char* p, vector<char> bits) {
  ogg_int64_t i, j, n=bits.size()/8, L=bits.size();
  for(i=0;i<=n;i++) {
    *(p+i) = 0;
    for(j=7;j>=0;j--) {
      ogg_int64_t q = 8*i+(7-j);
      if (q<L){
        *(p+i) |= bits[q]<<j;
      }
    }
  }
}

// Given two interleaved rice-coded streams stored packed
// in num_bytes starting at p, this function decodes both, storing values
// into first and second.
void rice_read_alternate(vector<ogg_int64_t>* first,
                         vector<ogg_int64_t>* second,
                         unsigned char* p,
                         ogg_int64_t num_bytes,
                         ogg_int64_t num_pairs,
                         unsigned char rice_first,
                         unsigned char rice_second) {
  vector<char> bits;
  expand_bytes(&bits, p, num_bytes);
  ogg_int64_t i=0;
  vector<char>::iterator it = bits.begin();
  while (i < num_pairs) {
    first->push_back(rice_read_one(it, rice_first));
    second->push_back(rice_read_one(it, rice_second));
  }
}

// Packs two streams of values into an interleaved Rice-coded block of bits
void rice_encode_alternate(vector<char>* bits,
                           vector<ogg_int64_t>* first,
                           vector<ogg_int64_t>* second,
                           unsigned char rice_first,
                           unsigned char rice_second) {
  ogg_int64_t i;
  assert(first->size() == second->size());
  for(i = 0; i < first->size(); i++) {
    rice_write_one(bits, first->at(i), rice_first);
    rice_write_one(bits, second->at(i), rice_second);
  }
}
