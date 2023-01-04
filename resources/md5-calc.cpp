

/* C++ standard include files first */
#include <iostream>
#include <fstream>

using namespace std;

/* C system include files next */
#include <sys/stat.h>
#include <openssl/md5.h>

/* C standard include files next - none for this program*/
#include "util.h"
#include "md5-calc.h"

string calcMd5(string filepath){
    ifstream myfile;

    myfile.open(filepath, ifstream::in|ios::binary);
    if (myfile.fail()) {
        cerr << "Cannot open '" << filepath << "' for reading." << endl;
        exit(-1);
    }
    int bytes_remaining = get_file_size(filepath);
    MD5_CTX md5_ctx;

    MD5_Init(&md5_ctx);
    while (bytes_remaining > 0) {
        char buf[0x1000]; /* 4KB buffer */

        int bytes_to_read = ((bytes_remaining > (int)sizeof(buf)) ? sizeof(buf) : bytes_remaining);
        myfile.read(buf, bytes_to_read);
        if (myfile.fail()) {
            break;
        }
        MD5_Update(&md5_ctx, buf, bytes_to_read);
        bytes_remaining -= bytes_to_read;
    }
    myfile.close();
    unsigned char md5_buf[MD5_DIGEST_LENGTH];

    MD5_Final(md5_buf, &md5_ctx);

    string md5 = HexDump(md5_buf, sizeof(md5_buf));
    return md5;
}

string HexDump(unsigned char *buf, int len)
{
    string s;
    static char hexchar[]="0123456789abcdef";

    for (int i=0; i < len; i++) {
        unsigned char ch=buf[i];
        unsigned int hi_nibble=(unsigned int)((ch>>4)&0x0f);
        unsigned int lo_nibble=(unsigned int)(ch&0x0f);

        s += hexchar[hi_nibble];
        s += hexchar[lo_nibble];
    }
    return s;
}