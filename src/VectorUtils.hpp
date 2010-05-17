#include <ogg/ogg.h>
#include <vector>
#include <map>
#include "Decoder.hpp"

using namespace std;

void round_together (vector<ogg_int64_t>* first_out,
                            vector<ogg_int64_t>* second_out,
                            vector<ogg_int64_t>* first_in,
                            vector<ogg_int64_t>* second_in,
                            unsigned char shift1,
                            unsigned char shift2);

void differentiate (vector<ogg_int64_t>* differences,
                                 ogg_int64_t* initval,
                                 vector<ogg_int64_t>* values,
                                 unsigned char shift);

void shift_integrate (vector<ogg_int64_t>* integrated,
                             vector<ogg_int64_t>* differences,
                             unsigned char shift,
                             ogg_int64_t initval);

void split_rangemap(vector<ogg_int64_t>* offsets,
                           vector<ogg_int64_t>* gps,
                           RangeMap const * m,
                           ogg_int64_t max_granpos);

void merge_vectors(RangeMap * m,
                          vector<ogg_int64_t>*offsets,
                          vector<ogg_int64_t>*gps,
                          ogg_int64_t b_max);

ogg_int64_t measure_bmax(vector<ogg_int64_t>* offsets,
                                vector<ogg_int64_t>* gps,
                                RangeMap const* m);
