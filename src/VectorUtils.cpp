#include <ogg/ogg.h>
#include <algorithm>
#include <vector>
#include <map>
#include "Decoder.hpp"

using namespace std;

/* This file contains basic utility functions for the important operations
   on vectors. */

// Rounds off the shift LSBs of first_in and second_in, placing the outputs
// in first_out and second_out.  first_in and second_in must be
// nondecreasing, and have the same length. first_in is rounded up,
// while second_in is rounded down.  If rounding causes either first or
// second to produce two consecutive entries that are equal, the later
// entry will be discarded, as will the corresponding entry in the other
// vector.
static void round_together (vector<ogg_int64_t>* first_out,
                            vector<ogg_int64_t>* second_out,
                            vector<ogg_int64_t>* first_in,
                            vector<ogg_int64_t>* second_in,
                            unsigned char shift) {
  ogg_int64_t i, mask, offset, tmp1, tmp2;
  assert(first_in.size() == second_in.size());
  if (first_in.size() == 0) { 
    return;
  }
  offset = 1;
  offset = (offset<<shift) - 1; //paranoia about 32bit vs. 64bit shift
  mask = ~offset;

  first_out->push_back((first_in[0] + offset) & mask);
  second_out->push_back(second_in[0] & mask);
  for(i = 1; i < first_in.size(); i++) {
    tmp1 = (first_in[i] + offset) & mask;
    tmp2 = second_in[i] & mask;
    if (tmp1 > first_out->back() && tmp2 > second_out->back()) {
      first_out->push_back(tmp1);
      second_out->push_back(tmp2);
    }
  }
}

// Given values that have already been rounded but not shifted, compute
// shifted differences and initial value.  The rounded input values must
// be strictly increasing, because the differences are stored with 1
// subtracted.
static void differentiate (vector<ogg_int64_t>* differences,
                                 ogg_int64_t* initval,
                                 vector<ogg_int64_t>* values,
                                 unsigned char shift) {
  vector<ogg_int64_t>::iterator it = values->begin();
  ogg_int64_t offset = 0, prev, tmp;
  *initval = *it;
  prev = (*it)>>shift;
  ++it;
  while(it < values->end()) {
    tmp = (*it)>>shift;
    differences->push_back((tmp-prev)-1);
    prev = tmp;
    ++it;
  }
}

// Given a list of shifted differences, integrate them and store the result in
// integrated.  Note that we store differences reduced by
// 1, so we must invert that here.
static void shift_integrate (vector<ogg_int64_t>* integrated,
                             vector<ogg_int64_t>* differences,
                             unsigned char shift,
                             ogg_int64_t initval) {
  vector<ogg_int64_t>::iterator it=differences.begin();
  integrated->push_back(initval);
  for(;it<differences.end();++it) {
    integrated->push_back(integrated->back() + ((*it)+1)<<shift);
  }
}

// Given a RangeMap m, split out its granuleposes and offset start points
// into two new vectors.
static void split_rangemap(vector<ogg_int64_t>* gps,
                           vector<ogg_int64_t>* offsets,
                           RangeMap* m) {
  RangeMap::iterator it = m->begin();
  for(;it< m->end(); ++it) {
    if (it->second.start > offsets->back()) {
      gps->push_back(it->first);
      offsets->push_back(it->second.start);
    }
  }
}

// Return the number of bytes you must read beyond offsets[i+1] when looking
// for a granpos between gps[i] and gps[i+1] in order to ensure that you have
// captured sufficient data.
static ogg_int64_t measure_bmax(vector<ogg_int64_t>* gps,
                                vector<ogg_int64_t>* offsets,
                                RangeMap* m) {
  RangeMap::iterator it;
  ogg_int64_t i = 0, b_max = 0;
  while (gps[i] <= m->begin()->first) {
    ++i;
  }
  assert(gps->size() == offsets->size());
  for(; i < gps->size(); i++) {
    it = m->lower_bound(gps[i]-1);
    b_max = max(b_max, it->second.end - offsets[i]);
  }
  return b_max;
}
