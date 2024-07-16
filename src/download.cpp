#include "download.h"
#include "utils.h"

#include <fstream>
#include <iostream>
#include <stdio.h>
#include <string>

#include "ic_api.h"

void print_file_download_summary(const std::string &filename,
                                 const std::uint64_t &filesize,
                                 const std::vector<uint8_t> &v,
                                 const std::uint64_t &offset,
                                 const bool &done) {
  std::string msg;
  msg += filename + ": filesize=" + std::to_string(filesize) +
         ": chunksize=" + std::to_string(v.size()) +
         "; offset=" + std::to_string(offset) +
         "; done=" + std::to_string(done);
  std::cout << msg << std::endl;
}

void file_download_chunk() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);

  // Get filename to download and the chunksize
  std::string filename{""};
  uint64_t chunksize{0};
  uint64_t offset{0};

  CandidTypeRecord r_in;
  r_in.append("filename", CandidTypeText{&filename});
  r_in.append("chunksize", CandidTypeNat64{&chunksize});
  r_in.append("offset", CandidTypeNat64{&offset});
  ic_api.from_wire(r_in);

  // Open an ifstream
  std::ifstream if_stream;
  std::string msg;
  if (!open_ifstream(filename, std::ios::binary, if_stream, msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{std::string(__func__) +
                                                         ": " + msg}}});
    return;
  }

  // Calculate total file size
  if_stream.seekg(0, std::ios::end);
  std::uint64_t filesize = if_stream.tellg();
  if_stream.seekg(0, std::ios::beg);

  // Read 'chunksize' bytes from 'filename', starting at 'offset' and store them in 'v'
  std::vector<uint8_t> v(chunksize);
  if_stream.seekg(offset, std::ios::beg);
  if_stream.read(reinterpret_cast<char *>(v.data()), chunksize);
  v.resize(if_stream.gcount()); // Adjust vector size to actual bytes read

  // Are we done for this file
  bool done = offset + chunksize >= filesize;

  print_file_download_summary(filename, filesize, v, offset, done);

  // Return the chunk over the wire
  CandidTypeRecord file_download_record;
  file_download_record.append("chunk", CandidTypeVecNat8{v});
  file_download_record.append("chunksize", CandidTypeNat64{v.size()});
  file_download_record.append("filesize", CandidTypeNat64{filesize});
  file_download_record.append("offset", CandidTypeNat64{offset});
  file_download_record.append("done", CandidTypeBool{done});
  ic_api.to_wire(
      CandidTypeVariant{"Ok", CandidTypeRecord{file_download_record}});
}