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
      std::filesystem::create_directories(directory);
      std::cout << "Directories created: " << directory << '\n';
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

bool delete_directory(const std::filesystem::path &directory) {
  try {
    if (std::filesystem::exists(directory)) {
      std::filesystem::remove_all(directory);
      std::cout << "Directory deleted: " << directory << '\n';
      return true;
    } else {
      std::cout << "Directory not found: " << directory << '\n';
      return true; // No error if directory does not exist
    }
  } catch (const std::exception &ex) {
    std::cerr << "Exception while deleting directory: " << ex.what() << '\n';
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
  // Create random binary files
  std::filesystem::path top_directory = "Work/test_files_qa";

  bool success = delete_directory(top_directory);
  if (!success) {
    std::cout << "FATAL ERROR: Failed to delete directory " << top_directory
              << " for testing.\n";
    std::cout << "FATAL: Exiting program due to failed directory deletion.\n";
    std::exit(EXIT_FAILURE);
  }

  std::string filename = "random_data.bin";
  std::size_t file_size = 1010 * 1024; // 1.01 MB
  std::filesystem::path directory = top_directory;
  for (int i = 0; i < 10; ++i) {

    if (i > 0) {
      filename = "random_data_" + std::to_string(i) + ".bin";
      file_size += 1010 * 1024; // Increase file size for each iteration
    }
    if (i > 5) {
      // After 5 iterations, write to a new directory
      directory = directory / ("subdir_" + std::to_string(i - 5));
    }
    success = create_random_binary_file(directory, filename, file_size);
    if (!success) {
      std::cout << "FATAL ERROR: Failed to create random binary file "
                << filename << " for testing.\n";
      std::cout << "FATAL: Exiting program due to failed file creation.\n";
      std::exit(EXIT_FAILURE);
    }
  }

  // -----------------------------------------------------------------------------
  // recursive_dir_content test for a non-existing dir
  // '(record {dir = "does_not_exist"})' ->
  /*
  (
    variant {
        Err = record {
        msg = "recursive_dir_content: Directory does not exist: does_not_exist\n";
        }
    },
  )
  */
  mockIC.run_test(
      std::string(__func__) + ": " + "recursive_dir_content - non-existing dir",
      recursive_dir_content,
      "4449444c016c01cdfab0027101000e646f65735f6e6f745f6578697374",
      "4449444c026b01b0ad8fcd0c716b01c5fed2010001010000407265637572736976655f6469725f636f6e74656e743a204469726563746f727920646f6573206e6f742065786973743a20646f65735f6e6f745f65786973740a",
      silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // recursive_dir_content test for an existing dir - must fail for anonymous user
  // '(record {dir = "Work/test_files_qa"})' -> '(variant { Err = record { Other = "Access Denied"} })'
  mockIC.run_test(
      std::string(__func__) + ": " +
          "recursive_dir_content - existing dir anonymous",
      recursive_dir_content,
      "4449444c016c01cdfab00271010012576f726b2f746573745f66696c65735f7161",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, anonymous_principal);

  // -----------------------------------------------------------------------------
  // recursive_dir_content test for an existing dir - must fail for anonymous user
  // '(record {dir = "Work/test_files_qa"})' -> '(variant { Err = record { Other = "Access Denied"} })'
  mockIC.run_test(
      std::string(__func__) + ": " +
          "recursive_dir_content - existing dir non-controller",
      recursive_dir_content,
      "4449444c016c01cdfab00271010012576f726b2f746573745f66696c65735f7161",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, non_controller_principal);

  // -----------------------------------------------------------------------------
  // recursive_dir_content test for an existing dir - works for controller
  // '(record {dir = "Work/test_files_qa"})' ->
  mockIC.run_test(
      std::string(__func__) + ": " +
          "recursive_dir_content - existing dir non-controller",
      recursive_dir_content,
      "4449444c016c01cdfab00271010012576f726b2f746573745f66696c65735f7161",
      "4449444c156c03c7dda8bb0701bdbaf9d50702b6decedb07016d716d786c006c03c7dda8bb0771bdbaf9d50778b6decedb07716d046c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716c03c7dda8bb0771bdbaf9d50778b6decedb07716b01bc8a01050114000e24576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174615f352e62696e00b05e00000000000466696c6524576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174615f342e62696e00e84e00000000000466696c6524576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174615f332e62696e00203f00000000000466696c6524576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174615f322e62696e00582f00000000000466696c6524576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174615f312e62696e00901f00000000000466696c6522576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174612e62696e00c80f00000000000466696c651b576f726b2f746573745f66696c65735f71612f7375626469725f310000000000000000096469726563746f72792d576f726b2f746573745f66696c65735f71612f7375626469725f312f72616e646f6d5f646174615f362e62696e00786e00000000000466696c6524576f726b2f746573745f66696c65735f71612f7375626469725f312f7375626469725f320000000000000000096469726563746f727936576f726b2f746573745f66696c65735f71612f7375626469725f312f7375626469725f322f72616e646f6d5f646174615f372e62696e00407e00000000000466696c652d576f726b2f746573745f66696c65735f71612f7375626469725f312f7375626469725f322f7375626469725f330000000000000000096469726563746f727936576f726b2f746573745f66696c65735f71612f7375626469725f312f7375626469725f322f7375626469725f332f7375626469725f340000000000000000096469726563746f727948576f726b2f746573745f66696c65735f71612f7375626469725f312f7375626469725f322f7375626469725f332f7375626469725f342f72616e646f6d5f646174615f392e62696e00d09d00000000000466696c653f576f726b2f746573745f66696c65735f71612f7375626469725f312f7375626469725f322f7375626469725f332f72616e646f6d5f646174615f382e62696e00088e00000000000466696c65",
      silent_on_trap, my_principal);

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
  // '(record {filename = "Work/test_files_qa/random_data.bin"})' -> '(variant { Err = record { Other = "Access Denied"} })'
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_file_size - existing file anonymous",
      filesystem_file_size,
      "4449444c016c01c7dda8bb0771010022576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174612e62696e",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, anonymous_principal);

  // -----------------------------------------------------------------------------
  // filesystem_file_size test for an existing file - must fail for non-controller user
  // '(record {filename = "Work/test_files_qa/random_data.bin"})' -> '(variant { Err = record { Other = "Access Denied"} })'
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_file_size - existing file non-controller",
      filesystem_file_size,
      "4449444c016c01c7dda8bb0771010022576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174612e62696e",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, non_controller_principal);

  // -----------------------------------------------------------------------------
  // filesystem_file_size test for an existing file - works for controller
  // '(record {filename = "Work/test_files_qa/random_data.bin"})' ->
  /*
  (
    variant {
        Ok = record {
        msg = "File exists: Work/test_files_qa/random_data.bin\nFile size: 1034240 bytes\n";
        filename = "Work/test_files_qa/random_data.bin";
        filesize = 1_034_240 : nat64;
        exists = true;
        }
    },
  )
  */
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_file_size - existing file controller",
      filesystem_file_size,
      "4449444c016c01c7dda8bb0771010022576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174612e62696e",
      "4449444c026c04c1b4cc0271c7dda8bb0771bdbaf9d50778dcbb80ff0b7e6b01bc8a01000101004946696c65206578697374733a20576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174612e62696e0a46696c652073697a653a20313033343234302062797465730a22576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174612e62696e00c80f000000000001",
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
  // '(record {filename = "Work/test_files_qa/random_data.bin"})' -> '(variant { Err = record { Other = "Access Denied"} })'
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_remove - existing file anonymous",
      filesystem_remove,
      "4449444c016c01c7dda8bb0771010022576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174612e62696e",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, anonymous_principal);

  // -----------------------------------------------------------------------------
  // filesystem_remove test for an existing file - must fail for non-controller user
  // '(record {filename = "Work/test_files_qa/random_data.bin"})' -> '(variant { Err = record { Other = "Access Denied"} })'
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_remove - existing file non-controller",
      filesystem_remove,
      "4449444c016c01c7dda8bb0771010022576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174612e62696e",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, non_controller_principal);

  // -----------------------------------------------------------------------------
  // filesystem_remove test for an existing file - works for controller
  // '(record {filename = "Work/test_files_qa/random_data.bin"})' ->
  /*
  (
    variant {
        Ok = record {
        msg = "File removed successfully: Work/test_files_qa/random_data.bin";
        filename = "Work/test_files_qa/random_data.bin";
        filesize = 1_034_240 : nat64;
        exists = true;
        removed = true;
        }
    },
  )
  */
  mockIC.run_test(
      std::string(__func__) + ": " +
          "filesystem_remove - existing file controller",
      filesystem_remove,
      "4449444c016c01c7dda8bb0771010022576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174612e62696e",
      "4449444c026c05c1b4cc0271c7dda8bb0771bdbaf9d50778dcbb80ff0b7ea0bf80980f7e6b01bc8a01000101003d46696c652072656d6f766564207375636365737366756c6c793a20576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174612e62696e22576f726b2f746573745f66696c65735f71612f72616e646f6d5f646174612e62696e00c80f00000000000101",
      silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // Remove the test files created for testing (If all Ok, this should not be needed. It is already removed)
  delete_directory(top_directory);
}