/*
   Copyright (C) 2009, Mozilla Foundation

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   - Neither the name of the Mozilla Foundation nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
   PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE ORGANISATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
 * SkeletonEncoder.cpp - Encodes skeleton track.
 *
 * Contributor(s): 
 *   Chris Pearce <chris@pearce.org.nz>
 */


#include <fstream>
#include <iostream>
#include <algorithm>
#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include "SkeletonEncoder.hpp"
#include "Options.hpp"
#include "Utils.hpp"
#include "Decoder.hpp"
#include "VectorUtils.hpp"
#include "RiceCode.hpp"

using namespace std;

#define SKELETON_3_0_HEADER_LENGTH 64
#define SKELETON_4_0_HEADER_LENGTH 80
#define FISBONE_MAGIC "fisbone"
#define FISBONE_MAGIC_LEN (sizeof(FISBONE_MAGIC) / sizeof(FISBONE_MAGIC[0]))
#define FISBONE_BASE_SIZE 56

// FIXME: User should be able to control the granularity.  Granularity
// should adapt to available size in one-pass mode.  Optimal granularity
// settings are coupled, with similar error in both time and space.

// Temporal quantization of 16 samples  FIXME: Should be in terms of time.
#define GRANPOS_QUANT (4)
// Spatial granularity of 64 Kibibytes
#define OFFSET_QUANT (16)

static bool
IsIndexable(Decoder* decoder) {
  return decoder->Type() == TYPE_VORBIS ||
         decoder->Type() == TYPE_THEORA ||
         decoder->Type() == TYPE_KATE;
}

static bool
IsUniqueSerialno(ogg_uint32_t serialno, vector<Decoder*>& decoders)
{
  for (unsigned i=0; i<decoders.size(); i++) {
    if (decoders[i]->GetSerial() == serialno) {
      return false;
    }
  }
  return true;
}

static ogg_uint32_t
GetUniqueSerialNo(vector<Decoder*>& decoders)
{
  ogg_uint32_t serialno;
  srand((unsigned)time(0));
  do {
    serialno = rand();
  } while (!IsUniqueSerialno(serialno, decoders));
  return serialno;
}

SkeletonEncoder::SkeletonEncoder(DecoderMap& decoders,
                                 ogg_int64_t fileLength,
                                 ogg_int64_t oldSkeletonLength,
                                 ogg_uint64_t contentOffset)
  : mSkeletonDecoder(0),
    mFileLength(fileLength),
    mOldSkeletonLength(oldSkeletonLength),
    mPacketCount(0),
    mContentOffset(contentOffset),
    mGranuleposShift(0),
    mOffsetShift(OFFSET_QUANT)
{
  DecoderMap::iterator itr = decoders.begin();
  while (itr != decoders.end()) {
    Decoder* d = itr->second;
    if (IsIndexable(d)) {
      mDecoders.push_back(d);
    }
    if (d->Type() == TYPE_SKELETON) {
      mSkeletonDecoder = (SkeletonDecoder*)d;
    }
    itr++;
  }
  mSerial = mSkeletonDecoder ? mSkeletonDecoder->GetSerial()
                             : GetUniqueSerialNo(mDecoders);
}

SkeletonEncoder::~SkeletonEncoder() {
  for (ogg_uint32_t i=0; i<mIndexPackets.size(); i++) {
    delete[] mIndexPackets[i]->packet;
    delete mIndexPackets[i];
  }
  ClearIndexPages();
}

void
SkeletonEncoder::AddBosPacket()
{
  // Should not call this more than once, this should be the first packet.
  assert(mPacketCount == 0);

  // Allocate packet to return...
  ogg_packet* bos = new ogg_packet();
  memset(bos, 0, sizeof(ogg_packet));
  bos->packet = new unsigned char[SKELETON_4_0_HEADER_LENGTH];
  memset(bos->packet, 0, SKELETON_4_0_HEADER_LENGTH);
  bos->bytes = SKELETON_4_0_HEADER_LENGTH;
  bos->b_o_s = 1;
  
  if (mSkeletonDecoder) {
    // We already have a skeleton track. Use what data we can from its
    // bos packet.
    ogg_packet* original = mSkeletonDecoder->mPackets.front();
    memcpy(bos->packet, original->packet, SKELETON_3_0_HEADER_LENGTH);
  } else {
    // We need to construct the skeleton bos packet...
    memcpy(bos->packet, "fishead", 8);
    
    WriteLEUint64(bos->packet + SKELETON_PRES_TIME_DENOM_OFFSET, 1000);
    WriteLEUint64(bos->packet + SKELETON_BASE_TIME_DENOM_OFFSET, 1000);
  }
  
  // Set the version fields.
  WriteLEUint16(bos->packet + SKELETON_VERSION_MAJOR_OFFSET, SKELETON_VERSION_MAJOR);
  WriteLEUint16(bos->packet + SKELETON_VERSION_MINOR_OFFSET, SKELETON_VERSION_MINOR);
  
  WriteLEUint64(bos->packet + SKELETON_CONTENT_OFFSET, mContentOffset);

  mPacketCount++;

  mIndexPackets.push_back(bos);

}

bool
SkeletonEncoder::Encode() {
  AddBosPacket();

  AddFisbonePackets();

  // Construct and store the index packets.
  ConstructIndexPackets();
  
  AddEosPacket();
  
  // Convert packets to pages. We now know how much space they take up when
  // stored as pages. This tells us how much extra data we're adding to the
  // file. Stores the result in mExtraLength.
  ConstructPages();

  // Adjust index packets' page offsets to account for extra file length.
  CorrectOffsets();

  return true;
}

void
SkeletonEncoder::AddEosPacket() {
  ogg_packet* eos = new ogg_packet();
  memset(eos, 0, sizeof(ogg_packet));
  eos->e_o_s = 1;
  eos->packetno = mPacketCount;
  mPacketCount++;
  mIndexPackets.push_back(eos);
}

const char* sStreamType[] = {
  "Unknown",
  "Vorbis",
  "Theora",
  "Kate",
  "Skeleton",
  "Unsupported"
};

void
SkeletonEncoder::ConstructIndexPackets() {
  assert(mIndexPackets.size() > 0);
  for (ogg_uint32_t i=0; i<mDecoders.size(); i++) {
    ogg_packet* packet = new ogg_packet();
    memset(packet, 0, sizeof(ogg_packet));
    
    Decoder* decoder = mDecoders[i];
    const RangeMap& seekblocks = decoder->GetSeekBlocks();
    
    FisboneInfo info = decoder->GetFisboneInfo();
    mGranuleposShift = info.mGranuleShift + GRANPOS_QUANT;
    vector<ogg_int64_t> gps, offsets;
    split_rangemap(&offsets, &gps, &seekblocks);
    vector<ogg_int64_t> gps_rounded, offsets_rounded;
    round_together(&offsets_rounded, &gps_rounded, &offsets, &gps,
                   mOffsetShift, mGranuleposShift, decoder->GetLastGranulepos());
    ogg_int64_t b_max;
    b_max = measure_bmax(&offsets_rounded, &gps_rounded, &seekblocks);
    ogg_int64_t init_offset, init_granpos;
    vector<ogg_int64_t> gp_diffs, offset_diffs;
    differentiate(&offset_diffs, &init_offset, &offsets_rounded, mOffsetShift);
    differentiate(&gp_diffs, &init_granpos, &gps_rounded, mGranuleposShift);
    unsigned char offset_rice_param, gp_rice_param;
    offset_rice_param = optimal_rice_parameter(&offset_diffs);
    gp_rice_param = optimal_rice_parameter(&gp_diffs);
    vector<char> bits;
    rice_encode_alternate(&bits, &offset_diffs, &gp_diffs,
                          offset_rice_param, gp_rice_param);
    
    const ogg_int32_t uncompressed_size = INDEX_SEEKPOINT_OFFSET +
                                  (int)seekblocks.size() * 16;

    ogg_int64_t compressed_size =
      INDEX_SEEKPOINT_OFFSET + tobytes(bits.size());

    double savings = ((double)compressed_size / (double)uncompressed_size) * 100.0;
    cout << sStreamType[mDecoders[i]->Type()] << "/" << mDecoders[i]->GetSerial()
         << " index uses " << uncompressed_size 
         << " bytes, compresses to " << compressed_size << " (" << savings << "%),"
         << " duration [" << decoder->GetStartTime() << "," << decoder->GetEndTime() << "] ms"
         << endl;

    packet->bytes = compressed_size;
    unsigned char* p = new unsigned char[compressed_size];
    memset(p, 0, compressed_size);
    packet->packet = p;

    // Identifier bytes.
    memcpy(packet->packet, HEADER_MAGIC, HEADER_MAGIC_LEN);

    // Stream serialno.
    WriteLEUint32(packet->packet + INDEX_SERIALNO_OFFSET,
                  mDecoders[i]->GetSerial());
    
    // Number of key points.
    assert(keyframes.size() < UINT_MAX);
    WriteLEUint64(packet->packet + INDEX_NUM_KEYPOINTS_OFFSET,
                  (ogg_uint64_t)keyframes.size());
    
    WriteLEInt64(packet->packet + INDEX_LAST_GRANPOS,
                                                  decoder->GetLastGranulepos());

    WriteUint8(packet->packet + INDEX_GRANPOS_SHIFT, mGranuleposShift);
    WriteUint8(packet->packet + INDEX_GRANPOS_RICE_PARAM, gp_rice_param);
    WriteUint8(packet->packet + INDEX_OFFSET_SHIFT, mOffsetShift);
    WriteUint8(packet->packet + INDEX_OFFSET_RICE_PARAM, offset_rice_param);

    WriteLEInt64(packet->packet + INDEX_MAX_EXCESS_BYTES, b_max);

    WriteLEInt64(packet->packet + INDEX_INIT_OFFSET, init_offset);
    WriteLEInt64(packet->packet + INDEX_INIT_GRANPOS, init_granpos);

    p = packet->packet + INDEX_SEEKPOINT_OFFSET;

    squeeze_bits(p, bits);
    
    packet->packetno = mPacketCount;
    mPacketCount++;

    assert(packet->e_o_s == 0);
    assert(packet->b_o_s == 0);
    mIndexPackets.push_back(packet);
  }
}


void
SkeletonEncoder::ConstructPages() {
  
  assert(mIndexPackets.size() == 2 * mDecoders.size() + 2);
  
  ClearIndexPages();

  ogg_int32_t ret = 0;
  ogg_page page;
  ogg_stream_state state;
  memset(&state, 0, sizeof(ogg_stream_state));
  memset(&page, 0, sizeof(ogg_page));
  
  ret = ogg_stream_init(&state, mSerial);
  assert(ret == 0);

  // BOS packet, must be on own page.
  ret = ogg_stream_packetin(&state, mIndexPackets[0]);
  assert(ret == 0);
  ret = ogg_stream_flush(&state, &page);
  assert(ret != 0);
  AppendPage(page);

  // Normal skeleton header packets...
  for (ogg_uint32_t i=1; i<mIndexPackets.size(); i++) {
    ret = ogg_stream_packetin(&state, mIndexPackets[i]);
    assert(ret == 0);
  }
  
  while (ogg_stream_pageout(&state, &page) != 0) {
    assert(!ogg_page_bos(&page));
    AppendPage(page);
  }

  ret = ogg_stream_flush(&state, &page);
  if (ret != 0) {
    AppendPage(page);
  }
   
  ogg_stream_clear(&state); 

}

void
SkeletonEncoder::AppendPage(ogg_page& page) {
  ogg_page* clone = Clone(&page);
  mIndexPages.push_back(clone);
}

void
SkeletonEncoder::ClearIndexPages() {
  for (ogg_uint32_t i=0; i<mIndexPages.size(); i++) {
    FreeClone(mIndexPages[i]);
  }
  mIndexPages.clear();
}  

ogg_int64_t
SkeletonEncoder::GetTrackLength()
{
  ogg_int64_t length = 0;
  int packets = 0;
  for (ogg_uint32_t i=0; i<mIndexPages.size(); i++) {
    length += mIndexPages[i]->header_len + mIndexPages[i]->body_len;
    packets += ogg_page_packets(mIndexPages[i]);
  }
  return length;
}

void
SkeletonEncoder::CorrectOffsets() {
  assert(mIndexPackets.size() != 0);
  ogg_int64_t fileLength = mFileLength - mOldSkeletonLength + GetTrackLength();

  // Difference in file lengths before and after indexing. We must add this
  // amount to the page offsets in the index packets, as they've changed by
  // this much.
  ogg_int64_t lengthDiff = fileLength - mFileLength;
  assert(lengthDiff == (GetTrackLength() - mOldSkeletonLength));
  mContentOffset += lengthDiff;

  // Correct the initial offset in every track by how much
  // the offsets have changed.
  for (ogg_uint32_t idx=0; idx<mIndexPackets.size(); idx++) {
    ogg_packet* packet = mIndexPackets[idx];
    assert(packet);
    
    if (!IsIndexPacket(packet)) {
      continue;
    }

    unsigned char* p = packet->packet + INDEX_INIT_OFFSET;
    ogg_int64_t existing_offset = LEUint64(p);
    WriteLEUint64(p, existing_offset + lengthDiff)
  }

  // First correct the BOS packet's segment length field.
  WriteLEUint64(mIndexPackets[0]->packet + SKELETON_FILE_LENGTH_OFFSET, fileLength);


  // Correct the BOS packet's content offset field.
  WriteLEUint64(mIndexPackets[0]->packet + SKELETON_CONTENT_OFFSET, mContentOffset);

}

// Write out the new skeleton BOS page.
void
SkeletonEncoder::WriteBosPage(ofstream& output) {
  assert(mIndexPages.size() > 0);

  // Write out the new skeleton page.
  WritePage(output, *mIndexPages[0]);
}

void
SkeletonEncoder::WritePages(ofstream& output) {
  assert(mIndexPages.size() > 0);
  for (ogg_uint32_t i=1; i<mIndexPages.size(); i++) {
    WritePage(output, *mIndexPages[i]);
  }
}

bool
SkeletonEncoder::HasFisbonePackets() {
  return mSkeletonDecoder &&
         mSkeletonDecoder->mPackets.size() == mDecoders.size() + 2;
}

static void ToLower(string& str) {
  for(unsigned int i=0;i<str.length();i++) {
    str[i] = tolower(str[i]);
  }
}

#define FISBONE_3_0_HEADER_OFFSET 52
#define FISBONE_4_0_HEADER_OFFSET 56

// Offset of fisbone fields. All field offsets are the same between Skeleton
// version 3 and version 4, except Radix, which doesn't exist in version 3.
#define FISBONE_HEADERS_OFFSET_FIELD_OFFSET 8
#define FISBONE_SERIALNO_OFFSET 12
#define FISBONE_NUM_HEADERS_OFFSET 16
#define FISBONE_GRAN_NUMER_OFFSET 20
#define FISBONE_GRAN_DENOM_OFFSET 28
#define FISBONE_START_GRAN_OFFSET 36
#define FISBONE_PREROLL_OFFSET 44
#define FISBONE_GRAN_SHIFT_OFFSET 48
#define FISBONE_RADIX_OFFSET 52

Decoder*
SkeletonEncoder::FindTrack(ogg_uint32_t serialno) {
  for (unsigned i=0; i<mDecoders.size(); i++) {
    if (mDecoders[i]->GetSerial() == serialno) {
      return mDecoders[i];
    }
  }
  return 0;
}

ogg_packet*
SkeletonEncoder::UpdateFisbone(ogg_packet* original) {
  assert(IsFisbonePacket(original));

  ogg_uint32_t serialno = LEUint32(original->packet + FISBONE_SERIALNO_OFFSET);
  Decoder* decoder = FindTrack(serialno);
  if (!decoder) {
    cerr << "ERROR: incoming fisbone packet for stream " << serialno << " is unknown" << endl;
    return 0;
  }
  FisboneInfo info = decoder->GetFisboneInfo();

  ogg_uint32_t version = mSkeletonDecoder->GetVersion();

  bool isVersion3x = version >= SKELETON_VERSION(3,0) && 
                     version < SKELETON_VERSION(4,0);

  int originalHeadersOffset = isVersion3x ? FISBONE_3_0_HEADER_OFFSET
                                          : FISBONE_4_0_HEADER_OFFSET;

  // Extract the message header fields, ensure we include the all compulsory
  // fields. These are: Content-Type, Role, Name.
  const char* x = (const char*)(original->packet + originalHeadersOffset);
  string header(x, original->bytes - originalHeadersOffset);
  vector<string> headers;
  Tokenize(header, headers, "\r\n");

  bool hasContentType = false;
  bool hasRole = false;
  bool hasName = false;
  for (unsigned i=0; i<headers.size(); i++) {
    string::size_type colon = headers[i].find_first_of(":");
    if (colon == string::npos) {
      continue;
    }
    string id = headers[i].substr(0, colon);
    ToLower(id);
    if (id.compare("content-type") == 0) {
      hasContentType = true;
    } else if (id.compare("name") == 0) {
      hasName = true;
    } else if (id.compare("role") == 0) {
      hasRole = true;
    }
  }

  unsigned size = original->bytes;
  if (isVersion3x) {
    // Version 3.x. We need to add the radix field, and any extra headers.
    size += 4;
  }

  if (!hasContentType)
    size += strlen("Content-Type: ") + info.mContentType.size() + 2;
  if (!hasName)
    size += strlen("Name: ") + info.mName.size() + 2;
  if (!hasRole)
    size += strlen("Role: ") + info.mRole.size() + 2;

  ogg_packet* packet = new ogg_packet();
  memcpy(packet, original, sizeof(ogg_packet));
  packet->packet = new unsigned char[size];
  packet->bytes = size;

  // Copy stuff up to radix
  memcpy(packet->packet, original->packet, originalHeadersOffset);

  // Overwrite the message-fields offset; it changes between v 3 and v4.
  WriteLEUint32(packet->packet + FISBONE_HEADERS_OFFSET_FIELD_OFFSET,
                FISBONE_4_0_HEADER_OFFSET);

  if (isVersion3x) {
    // Add the radix.
    WriteLEUint32(packet->packet + originalHeadersOffset, info.mRadix);
  }

  unsigned char* h = packet->packet + FISBONE_4_0_HEADER_OFFSET;

  // Write any existing headers.
  memcpy(h, original->packet+originalHeadersOffset, original->bytes - originalHeadersOffset);
  h += original->bytes - originalHeadersOffset;

  if (!hasContentType) {
    // Write a content type...
    string s = "Content-Type: " + info.mContentType + "\r\n";
    memcpy(h, s.c_str(), s.size());
    h += s.size();
  }
  if (!hasName) {
    // Write a name...
    string s = "Name: " + info.mName + "\r\n";
    memcpy(h, s.c_str(), s.size());
    h += s.size();
  }
  if (!hasRole) {
    // Write a role...
    string s = "Role: " + info.mRole + "\r\n";
    memcpy(h, s.c_str(), s.size());
    h += s.size();
  }
  assert(h == packet->packet + packet->bytes);
  return packet;
}

void
SkeletonEncoder::AddFisbonePackets() {
  if (HasFisbonePackets()) {
    // We have fisbone packets from the existing skeleton track. Convert to
    // Skeleton 4.0.
    for (ogg_uint32_t i=1; i<mSkeletonDecoder->mPackets.size()-1; i++) {
      ogg_packet* fisbone = UpdateFisbone(mSkeletonDecoder->mPackets[i]);
      if (fisbone) {
        mIndexPackets.push_back(fisbone);
      }
      mPacketCount++;
    }
  } else {
    // Have to construct fisbone packets.
    for (ogg_uint32_t i=0; i<mDecoders.size(); i++) {
      FisboneInfo info = mDecoders[i]->GetFisboneInfo();
      string headers = info.MessageHeaders();
      unsigned packetSize = FISBONE_BASE_SIZE + headers.size();

      ogg_packet* packet = new ogg_packet();
      memset(packet, 0, sizeof(ogg_packet));
      
      packet->packet = new unsigned char[packetSize];
      memset(packet->packet, 0, packetSize);

      // Magic bytes identifier.
      memcpy(packet->packet, FISBONE_MAGIC, FISBONE_MAGIC_LEN);
      
      // Offset of the message header fields.
      WriteLEInt32(packet->packet+FISBONE_HEADERS_OFFSET_FIELD_OFFSET,
                   FISBONE_4_0_HEADER_OFFSET);
      
      // Serialno of the stream.
      WriteLEUint32(packet->packet+FISBONE_SERIALNO_OFFSET,
                    mDecoders[i]->GetSerial());

      // Number of header packets. 3 for both vorbis and theora.
      WriteLEUint32(packet->packet+FISBONE_NUM_HEADERS_OFFSET, 3);
      
      // Granulrate numerator.
      WriteLEInt64(packet->packet+FISBONE_GRAN_NUMER_OFFSET, info.mGranNumer);
      
      // Granulrate denominator.
      WriteLEInt64(packet->packet+FISBONE_GRAN_DENOM_OFFSET, info.mGranDenom);
      
      // Start granule.
      WriteLEInt64(packet->packet+FISBONE_START_GRAN_OFFSET, 0);
      
      // Preroll.
      WriteLEUint32(packet->packet+FISBONE_PREROLL_OFFSET, info.mPreroll);
      
      // Granule shift.
      WriteLEUint32(packet->packet+FISBONE_GRAN_SHIFT_OFFSET, info.mGranuleShift);

      // Radix.
      WriteLEUint32(packet->packet+FISBONE_RADIX_OFFSET, info.mRadix);

      // Message header field, Content-Type */
      memcpy(packet->packet+FISBONE_BASE_SIZE,
             headers.c_str(),
             headers.size());

      packet->b_o_s = 0;
      packet->e_o_s = 0;
      packet->bytes = packetSize;

      packet->packetno = mPacketCount;      
      mIndexPackets.push_back(packet);
      mPacketCount++;
    }        
  }
}
