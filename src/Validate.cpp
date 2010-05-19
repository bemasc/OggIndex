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
 * Validate.cpp - ogg index validator.
 *
 * Contributor(s): 
 *   Chris Pearce <chris@pearce.org.nz>
 *   ogg.k.ogg.k <ogg.k.ogg.k@googlemail.com>
 */
 
#include <list>
#include <limits.h>
#include <stdlib.h>
#include <ogg/ogg.h>
#include <theora/theora.h>
#include <theora/theoradec.h>
#include <vorbis/codec.h>
#ifdef HAVE_KATE
#include <kate/oggkate.h>
#endif
#include <assert.h>
#include <string.h>
#include <algorithm>
#include "Utils.hpp"
#include "Decoder.hpp"
#include "SkeletonEncoder.hpp"

using namespace std;

static bool ReadAllHeaders(DecoderMap& decoders)
{
  bool gotAllHeaders = true;
  DecoderMap::iterator itr = decoders.begin();
  while (itr != decoders.end()) {
    Decoder* d = itr->second;
    if (d && !d->GotAllHeaders()) {
      gotAllHeaders = false;
      break;
    }
    itr++;
  }
  return gotAllHeaders;
}

static bool IsCover(OffsetRange original, OffsetRange cover) {
  return cover.start <= original.start && cover.end >= original.end;
}

static bool IsCovermap(const RangeMap& original, const RangeMap& cover) {
  RangeMap::const_iterator it = original.begin();
  while (it != original.end()) {
    if (!IsCover(it->second, (--cover.upper_bound(it->first))->second)) {
      return false;
    }
    ++it;
  }
  return true;
}

static ogg_int64_t MaxWindow(const RangeMap& m) {
  ogg_int64_t max_window = 0;
  RangeMap::const_iterator it = m.begin();
  while (it != m.end()) {
    max_window = max(max_window, it->second.end - it->second.start);
    ++it;
  }
  return max_window;
}

bool ValidateIndexedOgg(const string& filename) {
  ifstream input(filename.c_str(), ios::in | ios::binary);
  ogg_sync_state state;
  ogg_int32_t ret = ogg_sync_init(&state);
  assert(ret==0);

  DecoderMap decoders;
  ogg_page page;
  memset(&page, 0, sizeof(ogg_page));
  ogg_uint64_t bytesRead = 0;
  SkeletonDecoder* skeleton = 0;
  bool index_valid = true;
  ogg_int64_t offset = 0, contentOffset = 0;
  
  while (ReadPage(&state, &page, input, bytesRead))
  {
    int serialno = ogg_page_serialno(&page);
    Decoder* decoder = 0;
    if (ogg_page_bos(&page)) {
      decoders[serialno] = Decoder::Create(&page);
    }
    ogg_int64_t length = page.header_len + page.body_len;
    decoder = decoders[serialno];
    if (!ReadAllHeaders(decoders)) {
      contentOffset += length;
      if (decoder->GotAllHeaders()) {
        cerr << "FAIL: A content page appeared in stream serialno=" << serialno
             << " before all header pages were received." << endl;
        index_valid = false;
      }
    }
    if (!decoder) {
      cout << "WARNING: Unknown stream type, serialno=" << serialno << endl;
      continue;
    }
    if (decoder->Type() == TYPE_SKELETON) {
      skeleton = (SkeletonDecoder*) decoder;
    }
    if (!decoder->Decode(&page, offset)) {
      index_valid = false;
    }
    offset += length;
  }

  if (!skeleton) {
    cerr << "FAIL: No skeleton track so therefore no keyframe indexes!" << endl;
    return false;
  }

  if (skeleton->GetContentOffset() != contentOffset) {
    cerr << "FAIL: skeleton header's reported content offset (" << skeleton->GetContentOffset()
         << ") does not match actual content offset (" << contentOffset << ")" << endl;
    index_valid = false;
  }

  ogg_int64_t fileLength = FileLength(filename.c_str());
  if (skeleton->GetFileLength() != fileLength) {
    cerr << "FAIL: index's reported file length (" << skeleton->GetFileLength()
         << ") doesn't match file's actual length (" << fileLength << ")" << endl;
    index_valid = false;
  }

  SeekBlockIndex::iterator itr = skeleton->mIndex.begin();
  if (itr == skeleton->mIndex.end()) {
    cerr << "WARNING: No tracks in skeleton index." << endl;
  }

  while (itr != skeleton->mIndex.end()) {
    RangeMap* v = itr->second;
    ogg_uint32_t serialno = itr->first;
    itr++;
    Decoder* decoder = decoders[serialno];

    if (!decoder) {
      cerr << "WARNING: No decoder for track s="
           << serialno << endl;
      continue;
    }

    if (v->size() == 0) {
      cerr << "WARNING: " << decoder->Type() << "/" <<  serialno
           << " index has no keyframes" << endl;
      continue;
    }

    cout << decoder->Type() << "/" << serialno
         << " index has " << v->size() << " keypoints." << endl;

    bool valid = IsCovermap(decoder->GetSeekBlocks(), *v);

    if (valid) {
      cout << decoder->Type() << "/" << serialno
           << " index is accurate, with max seek window of "
           << MaxWindow(*v) << " bytes, compared to an optimal window of "
           << MaxWindow(decoder->GetSeekBlocks()) << "." << endl;
    } else {
      cout << "FAIL: " << decoder->Type() << "/" << serialno
           << " index is NOT accurate." << endl;
      index_valid = false;
    }
  }

  ogg_sync_clear(&state);
  
  return index_valid;
}
