#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

#include "../src/auth.h"
#include "../src/files.h"
#include "../src/health.h"
#include "../src/logs.h"
#include "../src/model.h"
#include "../src/promptcache.h"
#include "../src/ready.h"
#include "../src/run.h"
#include "../src/upload.h"
#include "../src/whoami.h"

// The Mock IC
#include "mock_ic.h"

// Helper function to create directory and write random binary file
bool create_random_binary_file(const std::filesystem::path &directory,
                               const std::string &filename,
                               std::size_t size_in_bytes) {
  try {
    // Create the directory if it doesn't exist
    if (!std::filesystem::exists(directory)) {
      std::filesystem::create_directory(directory);
      std::cout << "Directory created: " << directory << '\n';
    } else {
      std::cout << "Directory already exists: " << directory << '\n';
    }

    std::filesystem::path filePath = directory / filename;
    std::ofstream outFile(filePath, std::ios::binary);
    if (!outFile) {
      std::cerr << "Failed to open file for writing: " << filePath << '\n';
      return false;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned char> dist(0, 255);

    for (std::size_t i = 0; i < size_in_bytes; ++i) {
      unsigned char byte = dist(gen);
      outFile.write(reinterpret_cast<const char *>(&byte), 1);
    }

    outFile.close();
    std::cout << "Binary file written: " << filePath << " (" << size_in_bytes
              << " bytes)\n";
    return true;

  } catch (const std::exception &ex) {
    std::cerr << "Exception: " << ex.what() << '\n';
    return false;
  }
}

bool delete_binary_file(const std::filesystem::path &directory,
                        const std::string &filename) {
  try {
    std::filesystem::path filePath = directory / filename;
    if (std::filesystem::exists(filePath)) {
      std::filesystem::remove(filePath);
      std::cout << "File deleted: " << filePath << '\n';
      return true;
    } else {
      std::cout << "File not found: " << filePath << '\n';
      return false;
    }
  } catch (const std::exception &ex) {
    std::cerr << "Exception while deleting file: " << ex.what() << '\n';
    return false;
  }
}

void test_files(MockIC &mockIC) {

  // -----------------------------------------------------------------------------
  // Configs for the tests:

  // The pretend principals of the caller
  std::string my_principal{
      MOCKIC_CONTROLLER}; // MockIC hardcodes this as the controller: "expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae"
  std::string anonymous_principal{
      "2vxsx-fae"}; // The anonymous user principal - an ICP standard
  std::string non_controller_principal{
      "itk7v-ihlxk-ktdrh-fcnst-vkoou-orj77-52ogl-jqwj5-zpfdv-az3lr-xqe"}; // A random, non-controller user

  bool silent_on_trap = true;

  // -----------------------------------------------------------------------------
  // Create a random binary file to the filesystem of exactly 1.01 MB
  constexpr std::size_t file_size = 1010 * 1024; // 1.01 MB
  std::filesystem::path directory = "Work";
  std::string filename = "random_data.bin";

  bool success = create_random_binary_file(directory, filename, file_size);
  if (!success) {
    std::cout
        << "FATAL ERROR: Failed to create random binary file for testing.\n";
    std::cout << "FATAL: Exiting program due to failed file creation.\n";
    std::exit(EXIT_FAILURE);
  }

  // -----------------------------------------------------------------------------
  // filesystem_file_size test for a non-existing file
  // '(record {filename = "does_not_exist.bin"})' ->
  /*
  (
    variant {
        Ok = record {
        msg = "File does not exist: does_not_exist.bin\n";
        filename = "does_not_exist.bin";
        filesize = 0 : nat64;
        exists = false;
        }
    },
  )
  */
  mockIC.run_test(
      std::string(__func__) + ": " + "filesystem_file_size - non-existing file",
      filesystem_file_size,
      "4449444c016c01c7dda8bb0771010012646f65735f6e6f745f65786973742e62696e",
      "4449444c026c04c1b4cc0271c7dda8bb0771bdbaf9d50778dcbb80ff0b7e6b01bc8a01000101002846696c6520646f6573206e6f742065786973743a20646f65735f6e6f745f65786973742e62696e0a12646f65735f6e6f745f65786973742e62696e000000000000000000",
      silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // filesystem_file_size test for an existing file - must fail for anonymous user
  // '(record {filename = "Work/random_data.bin"})' -> '(variant { Err = record { Other = "Access Denied"} })'
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_file_size - existing file anonymous",
      filesystem_file_size,
      "4449444c016c01c7dda8bb0771010014576f726b2f72616e646f6d5f646174612e62696e",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, anonymous_principal);

  // -----------------------------------------------------------------------------
  // filesystem_file_size test for an existing file - must fail for non-controller user
  // '(record {filename = "Work/random_data.bin"})' -> '(variant { Err = record { Other = "Access Denied"} })'
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_file_size - existing file non-controller",
      filesystem_file_size,
      "4449444c016c01c7dda8bb0771010014576f726b2f72616e646f6d5f646174612e62696e",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, non_controller_principal);

  // -----------------------------------------------------------------------------
  // filesystem_file_size test for an existing file - works for controller
  // '(record {filename = "Work/random_data.bin"})' ->
  /*
  (
    variant {
        Ok = record {
        msg = "File exists: Work/random_data.bin\nFile size: 1034240 bytes\n";
        filename = "Work/random_data.bin";
        filesize = 1_034_240 : nat64;
        exists = true;
        }
    },
  )
  */
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_file_size - existing file non-controller",
      filesystem_file_size,
      "4449444c016c01c7dda8bb0771010014576f726b2f72616e646f6d5f646174612e62696e",
      "4449444c026c04c1b4cc0271c7dda8bb0771bdbaf9d50778dcbb80ff0b7e6b01bc8a01000101003b46696c65206578697374733a20576f726b2f72616e646f6d5f646174612e62696e0a46696c652073697a653a20313033343234302062797465730a14576f726b2f72616e646f6d5f646174612e62696e00c80f000000000001",
      silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // filesystem_remove test for a non-existing file
  // '(record {filename = "does_not_exist.bin"})' ->
  /*
  (
    variant {
        Ok = record {
        msg = "File does not exist: does_not_exist.bin\n";
        filename = "does_not_exist.bin";
        filesize = 0 : nat64;
        exists = false;
        removed = false;
        }
    },
  )
  */
  mockIC.run_test(
      std::string(__func__) + ": " + "filesystem_remove - non-existing file",
      filesystem_remove,
      "4449444c016c01c7dda8bb0771010012646f65735f6e6f745f65786973742e62696e",
      "4449444c026c05c1b4cc0271c7dda8bb0771bdbaf9d50778dcbb80ff0b7ea0bf80980f7e6b01bc8a01000101002846696c6520646f6573206e6f742065786973743a20646f65735f6e6f745f65786973742e62696e0a12646f65735f6e6f745f65786973742e62696e00000000000000000000",
      silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // filesystem_remove test for an existing file - must fail for anonymous user
  // '(record {filename = "Work/random_data.bin"})' -> '(variant { Err = record { Other = "Access Denied"} })'
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_remove - existing file anonymous",
      filesystem_remove,
      "4449444c016c01c7dda8bb0771010014576f726b2f72616e646f6d5f646174612e62696e",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, anonymous_principal);

  // -----------------------------------------------------------------------------
  // filesystem_remove test for an existing file - must fail for non-controller user
  // '(record {filename = "Work/random_data.bin"})' -> '(variant { Err = record { Other = "Access Denied"} })'
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_remove - existing file non-controller",
      filesystem_remove,
      "4449444c016c01c7dda8bb0771010014576f726b2f72616e646f6d5f646174612e62696e",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, non_controller_principal);

  // -----------------------------------------------------------------------------
  // filesystem_remove test for an existing file - works for controller
  // '(record {filename = "Work/random_data.bin"})' ->
  /*
  (
    variant {
        Ok = record {
        msg = "File removed successfully: Work/random_data.bin";
        filename = "Work/random_data.bin";
        filesize = 1_034_240 : nat64;
        exists = true;
        removed = true;
        }
    },
  )
  */
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_remove - existing file non-controller",
      filesystem_remove,
      "4449444c016c01c7dda8bb0771010014576f726b2f72616e646f6d5f646174612e62696e",
      "4449444c026c05c1b4cc0271c7dda8bb0771bdbaf9d50778dcbb80ff0b7ea0bf80980f7e6b01bc8a01000101002f46696c652072656d6f766564207375636365737366756c6c793a20576f726b2f72616e646f6d5f646174612e62696e14576f726b2f72616e646f6d5f646174612e62696e00c80f00000000000101",
      silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // Remove the test files created for testing (If all Ok, this should not be needed. It is already removed)
  delete_binary_file(directory, filename);
}