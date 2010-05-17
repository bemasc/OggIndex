#include <ogg/ogg.h>
#include <vector>

using namespace std;

/* This file contains methods related to reading and writing Golomb-Rice codes*/

ogg_int64_t rice_bits_required(ogg_int64_t value,
                                      unsigned char rice_param);

ogg_int64_t tobytes(ogg_int64_t n);

ogg_int64_t rice_total_bits(vector<ogg_int64_t>* values,
                                    unsigned char rice_param);
                                    
unsigned char optimal_rice_parameter(vector<ogg_int64_t>* values);

void rice_write_one(vector<char>* bitstore,
                            ogg_int64_t value, unsigned char rice_param);

ogg_int64_t rice_read_one(vector<char>::iterator it,
                                  unsigned char rice_param);

void expand_bytes(vector<char>* bits, 
                               unsigned char* p, ogg_int64_t n);

void squeeze_bits(unsigned char* p, vector<char> bits);

void rice_read_alternate(vector<ogg_int64_t>* first,
                         vector<ogg_int64_t>* second,
                         unsigned char* p,
                         ogg_int64_t num_bytes,
                         ogg_int64_t num_pairs,
                         unsigned char rice_first,
                         unsigned char rice_second);
                         
void rice_encode_alternate(vector<char>* bits,
                           vector<ogg_int64_t>* first,
                           vector<ogg_int64_t>* second,
                           unsigned char rice_first,
                           unsigned char rice_second);
