#ifndef _MD5CALC_H_
#define _MD5CALC_H_

/* C++ standard include files first */
#include <iostream>
#include <fstream>

using namespace std;

/* C system include files next */
#include <sys/stat.h>
#include <openssl/md5.h>

/* C standard include files next - none for this program*/
#include "util.h"


/**
 * Helper function
 */
string HexDump(unsigned char *buf, int len);

/**
 * Returns the hash given the file path
 */
string calcMd5(string filepath);

#endif