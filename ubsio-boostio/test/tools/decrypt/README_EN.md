# Custom Decryption Library Example

## Overview

This example demonstrates how to create a custom decryption library that can be loaded by the ubs-io project to decrypt passwords for TLS private keys.

The system attempts to load a dynamic library named `libdecrypt.so` and looks for a function named `DecryptPassword` within it.

## File Description

- `decrypt_example.cpp`: implementation code of the decryption library example
- `CMakeLists.txt`: CMake build configuration file
- `build.sh`: build script

## Function Interface

The signature of the decryption function is as follows:

```cpp
int DecryptPassword(const char* cipherText, const size_t cipherTextLen, char *plainText, size_t *plainTextLen);
```

Parameter description:

- `cipherText`: encrypted text (input)
- `cipherTextLen`: length of the encrypted text (input). Note: The length cannot exceed 10,000 characters.
- `plainText`: buffer for decrypted text (output)
- `plainTextLen`: size of the buffer for decrypted text (output)

Return values:

- 0: success
- Non-zero values: failure

## Building Description

1. Ensure that CMake and the compiler have been installed.
2. Run the build script:

   ```bash
   chmod +x build.sh
   ./build.sh
   ```

3. After the build is complete, the `libdecrypt.so` file is generated in the current directory.

## Custom Implementation

You can modify the decryption logic in the `decrypt_example.cpp` file as required. For example, you can:

1. Implement a real cryptographic algorithm (such as AES).
2. Retrieve decryption keys from external sources.
3. Integrate with a hardware security machine (HSM).
