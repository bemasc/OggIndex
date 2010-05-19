#include <ogg/ogg.h>
#include <algorithm>
#include <vector>
#include <map>
#include "Decoder.hpp"
#include <assert.h>

using namespace std;

/* This file contains basic utility functions for the important operations
   on vectors. */

// Rounds off the shift LSBs of first_in and second_in, placing the outputs
// in first_out and second_out.  first_in and second_in must be
// nondecreasing, and have the same length. first_in is rounded down,
// while second_in is rounded up.  If rounding causes either first or
// second to produce two consecutive entries that are equal, the later
// entry will be discarded, as will the corresponding entry in the other
// vector.
void round_together (vector<ogg_int64_t>* first_out,
                            vector<ogg_int64_t>* second_out,
                            vector<ogg_int64_t>* first_in,
                            vector<ogg_int64_t>* second_in,
                            unsigned char shift1,
                            unsigned char shift2) {
  ogg_int64_t i, mask1, offset1, mask2, offset2, tmp1, tmp2;
  assert(first_in->size() == second_in->size());
  if (first_in->size() == 0) { 
    return;
  }
  offset1 = 1;
  offset1 = ((offset1<<shift1) - 1); //paranoia about 32bit vs. 64bit shift
  mask1 = ~offset1;
  
  offset2 = 1;
  offset2 = (offset2<<shift2) - 1; 
  mask2 = ~offset2;

  first_out->push_back(first_in->at(0) & mask1);
  //special exception for the first seek point: we round down the granpos,
  //to ensure that no granpos in the stream is unseekable.  This is safe because
  //there are no preceding granposes in this stream that could potentially
  //get "pulled forward".
  second_out->push_back(second_in->at(0) & mask2);
  for(i = 1; i+1 < first_in->size(); i++) {
    //loop runs from the second to the second-to-last
    tmp1 = first_in->at(i) & mask1;
    tmp2 = (second_in->at(i) + offset2) & mask2;
    if (tmp1 > first_out->back()) {
      if (tmp2 > second_out->back()) { // Add a new seek point
        first_out->push_back(tmp1);
        second_out->push_back(tmp2);
      } else {  // refine an existing seek point
        assert(tmp2 == second_out->back());
        first_out->pop_back();
        first_out->push_back(tmp1);
      }
    }
  }
  //special exception for the last seek point: we round up the offset,
  //to ensure that every valid seek has an upper bound.
  tmp1 = (first_in->at(i) + offset1) & mask1;
  tmp2 = (second_in->at(i) + offset2) & mask2;
  first_out->push_back(tmp1);
  second_out->push_back(tmp2);
}

// Given values that have already been rounded but not shifted, compute
// shifted differences and initial value.  The rounded input values must
// be strictly increasing, because the differences are stored with 1
// subtracted.
void differentiate (vector<ogg_int64_t>* differences,
                                 ogg_int64_t* initval,
                                 vector<ogg_int64_t>* values,
                                 unsigned char shift) {
  vector<ogg_int64_t>::iterator it = values->begin();
  ogg_int64_t prev, tmp;
  *initval = *it;
  prev = (*it)>>shift;
  ++it;
  while(it != values->end()) {
    tmp = (*it)>>shift;
    differences->push_back((tmp-prev)-1);
    prev = tmp;
    ++it;
  }
}

// Given a list of shifted differences, integrate them and store the result in
// integrated.  Note that we store differences reduced by
// 1, so we must invert that here.
void shift_integrate (vector<ogg_int64_t>* integrated,
                             vector<ogg_int64_t>* differences,
                             unsigned char shift,
                             ogg_int64_t initval) {
  vector<ogg_int64_t>::iterator it=differences->begin();
  integrated->push_back(initval);
  for(;it<differences->end();++it) {
    integrated->push_back(integrated->back() + (((*it)+1)<<shift));
  }
}

// Given a RangeMap m, split out its granuleposes and offset start points
// into two new vectors.
void split_rangemap(vector<ogg_int64_t>* offsets,
                           vector<ogg_int64_t>* gps,
                           RangeMap const* m,
                           ogg_int64_t max_granpos) {
  if (m->size() == 0) {
    return;
  }
  RangeMap::const_iterator it = m->begin();
  ogg_int64_t last_end = 0;
  for(;it != m->end(); ++it) {
    if (offsets->size() == 0 || it->second.start > offsets->back()) {
      gps->push_back(it->first);
      offsets->push_back(it->second.start);
    }
    last_end = it->second.end;
  }
  // Add one more point at the end, to ensure finite b_max.
  gps->push_back(max_granpos+1);
  offsets->push_back(last_end);
}

// Given vectors of granulepos and start offsets, as well as the global
// b_max, construct the tightest possible safe RangeMap
void merge_vectors(RangeMap * m,
                          vector<ogg_int64_t>*offsets,
                          vector<ogg_int64_t>*gps,
                          ogg_int64_t b_max) {
  ogg_int64_t i;
  assert(m->size() == 0);
  assert(offsets->size() == gps->size());
  RangeMap::iterator it = m->end();
  for(i=0; i+1 < gps->size(); i++) {
    OffsetRange r={offsets->at(i), offsets->at(i+1)+b_max};
    it = m->insert(it,RangePair(gps->at(i),r));
  }
}

// Return the number of bytes you must read beyond offsets[i+1] when looking
// for a granpos between gps[i] and gps[i+1] in order to ensure that you have
// captured sufficient data.
ogg_int64_t measure_bmax(vector<ogg_int64_t>* offsets,
                                vector<ogg_int64_t>* gps,
                                RangeMap const* m) {
  assert(offsets->size() == gps->size());
  RangeMap::const_iterator it;
  ogg_int64_t i = 0, b_max = 0;
  while (i < gps->size() && gps->at(i) <= m->begin()->first) {
    ++i;
  }
  for(; i < gps->size(); i++) {
    it = m->upper_bound(gps->at(i)-1);
    --it;
    b_max = max(b_max, it->second.end - offsets->at(i));
  }
  return b_max;
}
