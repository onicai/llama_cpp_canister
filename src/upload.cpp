#include "upload.h"
#include "utils.h"
#include "ready.h"
#include "http.h"
#include "auth.h"

#include <fstream>
#include <iostream>
#include <stdio.h>
#include <string>

#include "ic_api.h"

void print_file_upload_summary(const std::string &filename,
                               const std::vector<uint8_t> &v,
                               const std::uint64_t &offset) {
  std::string msg;
  msg += filename + ": chunksize=" + std::to_string(v.size()) +
         "; offset=" + std::to_string(offset);
  std::cout << msg << std::endl;
}

void reset_model() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  ready_for_inference = false;

  // TODO: free & reset the global model pointers...
  // Likely by calling main_ with a flag !
  std::cout << std::string(__func__) << "TODO: implement reset of model" << std::endl;

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code",
                            CandidTypeNat16{Http::StatusCode::OK});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}

void file_upload_chunk() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  // Get filename and the chunk to write to it
  std::string filename{""};
  std::vector<uint8_t> v;
  uint64_t chunksize{0};
  uint64_t offset{0};

  CandidTypeRecord r_in;
  r_in.append("filename", CandidTypeText{&filename});
  r_in.append("chunk", CandidTypeVecNat8{&v});
  r_in.append("chunksize", CandidTypeNat64{&chunksize});
  r_in.append("offset", CandidTypeNat64{&offset});
  ic_api.from_wire(r_in);

  // Open an ofstream
  std::ofstream of_stream;
  std::string msg;
  std::ios_base::openmode mode = std::ios::binary;
  if (offset == 0) {
    mode |= std::ios::trunc; // truncate the file to zero length
  }
  if (!open_ofstream(filename, mode, of_stream, msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{std::string(__func__) +
                                                         ": " + msg}}});
    return;
  }

  // Write 'v' to 'filename', starting at 'offset'
  of_stream.seekp(offset);
  of_stream.write(reinterpret_cast<const char *>(v.data()), v.size());

  print_file_upload_summary(filename, v, offset);

  // Return the status over the wire
  CandidTypeRecord file_upload_record;
  file_upload_record.append("filesize", CandidTypeNat64{offset + v.size()});
  ic_api.to_wire(CandidTypeVariant{"Ok", CandidTypeRecord{file_upload_record}});
}