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
 * Decoder.hpp - Classes for decoding ogg streams for indexing.
 *
 * Contributor(s): 
 *   Chris Pearce <chris@pearce.org.nz>
 */

#ifndef __DECODER_HPP__
#define __DECODER_HPP__

#include <string>
#include <map>
#include <vector>
#include <ogg/ogg.h>

using namespace std;

// Minimum possible size of a compressed seek point, in bits.
#define MIN_SEEK_POINT_SIZE 2

// Magic bytes for index packet.
#define HEADER_MAGIC "index"
#define HEADER_MAGIC_LEN (sizeof(HEADER_MAGIC) / sizeof(HEADER_MAGIC[0]))

// Stores codec-specific skeleton info.
class FisboneInfo {
public:
  
  FisboneInfo()
    : mGranNumer(0)
    , mGranDenom(0)
    , mStartGran(0)
    , mPreroll(0)
    , mGranuleShift(0)
  {}

  // Granulerate numerator.
  ogg_int64_t mGranNumer;

  // Granulerate denominator.
  ogg_int64_t mGranDenom;  

  ogg_int64_t mStartGran;
  
  ogg_int32_t mPreroll;
  ogg_int32_t mGranuleShift;
  
  ogg_uint32_t mRadix;

  // Skeleton message header fields.
  string MessageHeaders() {
    return
      "Content-Type: " + mContentType + "\r\n" +
      "Name: " + mName + "\r\n" +
      "Role: " + mRole + "\r\n";
  }

  string mContentType;
  string mRole;
  string mName;
};

struct OffsetRange {
  // Offset of beginning of range in bytes.
  ogg_int64_t start;

  // Offset of end of range in bytes.
  // Special value end==-1 indicates that the endpoint is not yet
  // known.
  ogg_int64_t end;
};

// A pair of a granule and a range of bytes
typedef pair<ogg_int64_t,OffsetRange> RangePair;

// A map from granules to byte ranges.  If a range is not specified
// for a granule g, the range associated with g
// is the one mapped to the next lower granule.
typedef map<ogg_int64_t,OffsetRange> RangeMap;

// Maps a track's serialno to its seek range map.
typedef map<ogg_uint32_t, RangeMap*> SeekBlockIndex;

// Frees all memory stored in the seek block index.
void ClearSeekBlockIndex(SeekBlockIndex& index);

// Decodes an index packet, storing the decoded index in the SeekBlockIndex,
// mapped to by the track's serialno.
bool DecodeIndex(SeekBlockIndex& index, ogg_packet* packet);

enum StreamType {
  TYPE_UNKNOWN = 0,
  TYPE_VORBIS = 1,
  TYPE_THEORA = 2,
  TYPE_KATE = 3,
  TYPE_SKELETON = 4,
  TYPE_UNSUPPORTED = 5
};

// Superclass for indexer-decoder.
class Decoder {
protected:
  ogg_stream_state mState;

  // Serial of the stream we're decoding.
  ogg_uint32_t mSerial;
  
  // Last granulepos of any packet in this stream _in presentation order_
  // i.e. the granulepos that maximizes GranuleposToTime()
  ogg_int64_t mLastGranulepos;
  
  // Initialize decoder.
  Decoder(ogg_uint32_t serial);

public:

  virtual ~Decoder();

  // Factory, creates appropriate decoder for the give beginning of stream page.
  static Decoder* Create(ogg_page* bos_page);

  // Decode page at offset, record relevant info to index keypoints.
  virtual bool Decode(ogg_page* page, ogg_int64_t offset) = 0;

  // Returns true when we've decoded all header packets.
  virtual bool GotAllHeaders() = 0;

  // Returns the seek blocks for indexing. Call this after the entire stream
  // has been decoded.
  virtual const RangeMap& GetSeekBlocks() = 0;

  virtual StreamType Type() = 0;
  virtual const char* TypeStr() = 0;
  ogg_int64_t GetStartTime() {
    return GranuleposToTime(GetFisboneInfo().mStartGran);
  }
  ogg_int64_t GetEndTime() { return GranuleposToTime(mLastGranulepos); }
  ogg_int64_t GetLastGranulepos() { return mLastGranulepos; }
  virtual ogg_int64_t GranuleposToTime(ogg_int64_t granulepos) = 0;
  virtual ogg_int64_t GranuleposToGranule(ogg_int64_t granulepos) {
    ogg_int64_t mask = 1;
    ogg_int32_t shift = GetFisboneInfo().mGranuleShift;
    mask = (mask<<shift) - 1;
    return (granulepos >> shift) + (granulepos & mask);
  }
  ogg_uint32_t GetSerial() { return mSerial; }

  // Returns the info for this stream to be stored in the skeleton fisbone
  // packet.
  virtual FisboneInfo GetFisboneInfo() = 0;

};

typedef map<ogg_uint32_t, Decoder*> DecoderMap;

#define SKELETON_VERSION_MAJOR_OFFSET 8
#define SKELETON_VERSION_MINOR_OFFSET 10
#define SKELETON_PRES_TIME_DENOM_OFFSET 20
#define SKELETON_BASE_TIME_DENOM_OFFSET 36
#define SKELETON_FILE_LENGTH_OFFSET 64
#define SKELETON_CONTENT_OFFSET 72

#define INDEX_SERIALNO_OFFSET 6
#define INDEX_NUM_SEEKPOINTS_OFFSET 10
#define INDEX_LAST_GRANPOS 18
#define INDEX_GRANULE_ROUNDOFF 26
#define INDEX_GRANULE_RICE_PARAM 27
#define INDEX_OFFSET_ROUNDOFF 28
#define INDEX_OFFSET_RICE_PARAM 29
#define INDEX_MAX_EXCESS_BYTES 30
#define INDEX_INIT_OFFSET 38
#define INDEX_INIT_GRANULE 46
#define INDEX_SEEKPOINT_OFFSET 54

// Skeleton decoder. Must have public interface, as we use this in the
// skeleton encoder as well.
class SkeletonDecoder : public Decoder {
public:

  SkeletonDecoder(ogg_uint32_t serial);
  virtual ~SkeletonDecoder() {}

  virtual const char* TypeStr() { return "S"; }  

  virtual StreamType Type() { return TYPE_SKELETON; }

  virtual ogg_int64_t GranuleposToTime(ogg_int64_t granulepos) {
    return -1;
  }

  virtual ogg_int64_t GranuleposToGranule(ogg_int64_t granulepos) {
    return -1;
  }

  virtual bool Decode(ogg_page* page, ogg_int64_t offset);

  virtual ogg_int64_t GetFileLength() {return mFileLength;}
  
  virtual ogg_int64_t GetContentOffset() {return mContentOffset;}

  RangeMap mDummy;

  virtual const RangeMap& GetSeekBlocks() {
    return mDummy;
  }

  virtual bool GotAllHeaders() { return mGotAllHeaders; } 
  virtual FisboneInfo GetFisboneInfo() { return FisboneInfo(); }

  vector<ogg_packet*> mPackets;

  // Maps track serialno to seekpoint index, storing the seekpoint indexes
  // as they're read from the skeleton track.
  map<ogg_uint32_t, RangeMap*> mIndex;

  ogg_uint32_t GetVersion() { return mVersion; }

private:
  bool mGotAllHeaders;

  // Decoded stream version.
  ogg_uint16_t mVersionMajor;
  ogg_uint16_t mVersionMinor;
  ogg_uint32_t mVersion;
  
  ogg_int64_t mFileLength;
  ogg_int64_t mContentOffset;
 
};

#endif // __DECODER_HPP__
